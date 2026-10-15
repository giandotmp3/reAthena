// Live-database tests for rathena-migrate's MysqlMigrationTarget (architecture §5).
//
// Run only with RATHENA_TEST_DATABASE_URL pointing at a DISPOSABLE database: every test drops
// all of its tables and reloads the shipped legacy sql-files/*.sql.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "rathena/persistence/migrate/migrator.hpp"
#include "rathena/persistence/migrate/mysql_target.hpp"
#include "rathena/persistence/migrate/plan.hpp"
#include "rathena/persistence/migrate/schema.hpp"
#include "rathena/persistence/mysql.hpp"

using namespace rathena::persistence;
namespace mg = rathena::persistence::migrate;

namespace {

/// Splits a legacy .sql file into statements: `--` comment lines dropped, `;` at end of line ends
/// a statement (the shipped files contain no DELIMITER blocks or multi-line string literals).
std::vector<std::string> sql_statements(const std::string& file) {
	std::ifstream in(std::filesystem::path(RATHENA_LEGACY_SQL_DIR) / file);
	if (!in) throw std::runtime_error("cannot read sql-files/" + file);
	std::vector<std::string> out;
	std::string current, line;
	while (std::getline(in, line)) {
		if (line.rfind("--", 0) == 0) continue;
		current += line + "\n";
		auto last = line.find_last_not_of(" \t\r");
		if (last != std::string::npos && line[last] == ';') {
			current.erase(current.find_last_of(';'));
			if (current.find_first_not_of(" \t\r\n") != std::string::npos) out.push_back(current);
			current.clear();
		}
	}
	return out;
}

class MysqlMigrationIT : public ::testing::Test {
protected:
	void SetUp() override {
		if (!mysql_client_available()) GTEST_SKIP() << "built without a MySQL client";
		const char* url = std::getenv("RATHENA_TEST_DATABASE_URL");
		if (url == nullptr || *url == '\0') GTEST_SKIP() << "set RATHENA_TEST_DATABASE_URL to a disposable database to run";
		conn_ = std::make_unique<MysqlConnection>(parse_db_url(url));
		drop_all_tables();
	}

	void drop_all_tables() {
		const auto rows = conn_->query({"SELECT TABLE_NAME AS name FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE()", {}});
		conn_->execute({"SET FOREIGN_KEY_CHECKS = 0", {}}); // main.sql ships mail_attachments -> mail
		for (const auto& r : rows)
			conn_->execute({"DROP TABLE IF EXISTS " + quote_identifier(std::get<std::string>(r.at("name"))), {}});
		conn_->execute({"SET FOREIGN_KEY_CHECKS = 1", {}});
	}

	void load(const std::string& file) {
		for (const auto& st : sql_statements(file)) conn_->execute({st, {}});
	}

	std::string fingerprint() {
		mg::MysqlMigrationTarget target(*conn_, TableNames{});
		return mg::schema_fingerprint(target.inspect_schema());
	}

	std::unique_ptr<MysqlConnection> conn_;
};

} // namespace

TEST_F(MysqlMigrationIT, KnownBaselinesMatchShippedSqlFiles) {
	load("main.sql");
	const auto main_only = fingerprint();
	load("web.sql");
	const auto main_web = fingerprint();
	load("logs.sql"); // log tables are not part of the fingerprint
	EXPECT_EQ(fingerprint(), main_web);

	const auto& known = mg::known_legacy_baselines();
	EXPECT_TRUE(known.contains(main_only)) << "main.sql baseline changed; update known_legacy_baselines(): " << main_only;
	EXPECT_TRUE(known.contains(main_web)) << "main.sql+web.sql baseline changed; update known_legacy_baselines(): " << main_web;
	EXPECT_NE(main_only, main_web);
}

TEST_F(MysqlMigrationIT, UnknownDriftIsRefusedBeforeAnyChange) {
	load("main.sql");
	conn_->execute({"ALTER TABLE `login` ADD COLUMN `custom_column` INT NOT NULL DEFAULT 0", {}});
	mg::MysqlMigrationTarget target(*conn_, TableNames{});
	mg::MigrationOptions opts;
	opts.known_baselines = mg::known_legacy_baselines();
	opts.dry_run = false;
	const auto run = mg::Migrator(target, opts).run();
	EXPECT_TRUE(run.refused);
	EXPECT_NE(run.refusal_reason.find("unknown schema drift"), std::string::npos) << run.refusal_reason;
	EXPECT_TRUE(target.applied_versions().empty()) << "nothing may be recorded on a refused run";
}

TEST_F(MysqlMigrationIT, ApplyRunsUntilFirstClosedGateAndIsIdempotent) {
	load("main.sql");
	load("web.sql");
	load("logs.sql");
	mg::MysqlMigrationTarget target(*conn_, TableNames{});
	mg::MigrationOptions opts;
	opts.known_baselines = mg::known_legacy_baselines();
	opts.dry_run = false;

	const auto first = mg::Migrator(target, opts).run();
	ASSERT_FALSE(first.refused) << first.refusal_reason;
	ASSERT_NE(first.find(mg::StepId::M6), nullptr);
	EXPECT_EQ(first.find(mg::StepId::M0)->outcome, mg::StepOutcome::Applied);
	EXPECT_EQ(first.find(mg::StepId::M6)->outcome, mg::StepOutcome::Applied);
	EXPECT_EQ(first.find(mg::StepId::M7)->outcome, mg::StepOutcome::BlockedByGate);
	const auto recorded = target.applied_versions();
	EXPECT_FALSE(recorded.empty());

	// The M6 columns now exist and existing rows were migrated to pass_scheme 'plain'.
	const auto rows = conn_->query({"SELECT COUNT(*) AS n FROM `login` WHERE `pass_scheme` = 'plain' AND `pass_hash` IS NOT NULL", {}});
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_GE(std::get<std::int64_t>(rows[0].at("n")), 1) << "the seeded server account must be carried over";

	const auto second = mg::Migrator(target, opts).run();
	ASSERT_FALSE(second.refused) << second.refusal_reason;
	EXPECT_EQ(second.find(mg::StepId::M0)->outcome, mg::StepOutcome::AlreadyApplied);
	EXPECT_EQ(second.find(mg::StepId::M6)->outcome, mg::StepOutcome::AlreadyApplied);
	EXPECT_EQ(target.applied_versions(), recorded) << "a second run must not record anything new";
}

TEST_F(MysqlMigrationIT, M1OrphanReportFindsDanglingReferencesButIgnoresZeroSentinel) {
	load("main.sql");
	mg::MysqlMigrationTarget target(*conn_, TableNames{});
	EXPECT_TRUE(target.orphan_report().clean());

	// A character in guild 0 ("none") is fine; one pointing at a guild that does not exist is not.
	conn_->execute({"INSERT INTO `login` (`account_id`, `userid`, `sex`) VALUES (2000000, 'it_player', 'F')", {}});
	conn_->execute({"INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `guild_id`) VALUES (150000, 2000000, 0, 'NoGuild', 0)", {}});
	EXPECT_TRUE(target.orphan_report().clean());
	conn_->execute({"INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `guild_id`) VALUES (150001, 2000000, 1, 'GhostGuild', 999)", {}});
	const auto report = target.orphan_report();
	ASSERT_FALSE(report.clean());
	bool found = false;
	for (const auto& f : report.findings)
		if (f.child_table == "char" && f.column == "guild_id" && f.parent_table == "guild") found = f.count == 1;
	EXPECT_TRUE(found) << "expected one char.guild_id -> guild orphan";
}
