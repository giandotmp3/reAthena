// rathena-migrate: plan §5 ordering, gating, idempotency, forward-only history and per-step DDL.
// Test names carry the migration step id (M0..M3b). A fake MigrationTarget replaces MySQL.
#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

#include "rathena/persistence/migrate/migrator.hpp"

using namespace rathena::persistence;
using namespace rathena::persistence::migrate;

namespace {

class FakeTarget final : public MigrationTarget {
public:
	SchemaSnapshot schema;
	std::vector<std::string> recorded;
	OrphanReport orphans;
	std::vector<std::string> apply_log;
	std::string fail_version;

	SchemaSnapshot inspect_schema() override { return schema; }
	std::vector<std::string> applied_versions() override { return recorded; }
	OrphanReport orphan_report() override { return orphans; }
	void apply(std::string_view version, const std::vector<Statement>&) override {
		if (version == fail_version) throw std::runtime_error("simulated DDL failure");
		apply_log.emplace_back(version);
		recorded.emplace_back(version);
	}
};

SchemaSnapshot legacy_schema() {
	SchemaSnapshot s;
	s.tables = {
	    {"login", "MyISAM", {{"account_id", "int(11) unsigned", false}, {"user_pass", "varchar(32)", false}}},
	    {"char", "MyISAM", {{"char_id", "int(11) unsigned", false}, {"zeny", "int(11) unsigned", false},
	                        {"party_id", "int(11) unsigned", false}}},
	    {"inventory", "MyISAM", {{"id", "int(11) unsigned", false}, {"amount", "int(11) unsigned", false}}},
	    {"mail_attachments", "InnoDB", {{"id", "bigint(20) unsigned", false}}},
	    {"auction", "MyISAM", {{"auction_id", "bigint(20) unsigned", false}}},
	    {"homunculus", "MyISAM", {{"homun_id", "int(11)", false}}},
	    {"party", "MyISAM", {{"party_id", "int(11) unsigned", false}}},
	    {"picklog", "MyISAM", {{"id", "int(11)", false}}},
	    {"zenylog", "MyISAM", {{"id", "int(11)", false}}},
	};
	return s;
}

MigrationOptions apply_opts(const SchemaSnapshot& s) {
	MigrationOptions o;
	o.dry_run = false;
	o.known_baselines.insert(schema_fingerprint(s));
	return o;
}

std::vector<StepId> ids_with(const MigrationRun& r, StepOutcome o) {
	std::vector<StepId> out;
	for (const auto& s : r.steps)
		if (s.outcome == o) out.push_back(s.id);
	return out;
}

std::string version(StepId id) { return std::string(step_info(id).version); }

bool any_sql_contains(const std::vector<Statement>& st, std::string_view needle) {
	return std::any_of(st.begin(), st.end(), [&](const Statement& s) { return s.sql.find(needle) != std::string::npos; });
}

} // namespace

TEST(MigrationPlan, PlanOrderMatchesArchitectureSection5) {
	std::vector<StepId> order;
	for (const auto& s : migration_plan()) order.push_back(s.id);
	EXPECT_EQ(order, (std::vector<StepId>{StepId::M0, StepId::M1, StepId::M6, StepId::M6b, StepId::M7, StepId::M2,
	                                      StepId::M4, StepId::M3a, StepId::M3b}));
	EXPECT_EQ(step_info(StepId::M7).gate, Gate::CharRefactorReachedMail);
	EXPECT_EQ(step_info(StepId::M3a).gate, Gate::NoLegacyWriters);
	EXPECT_EQ(step_info(StepId::M3b).gate, Gate::M3aAppliedAndOrphansClean);
}

TEST(MigrationRun, M0_UnknownSchemaFingerprintRefusesEverything) {
	FakeTarget t;
	t.schema = legacy_schema();
	MigrationOptions o;
	o.dry_run = false; // known_baselines empty -> unknown drift
	const auto run = Migrator(t, o).run();
	EXPECT_TRUE(run.refused);
	EXPECT_EQ(run.find(StepId::M0)->outcome, StepOutcome::Refused);
	EXPECT_NE(run.refusal_reason.find("unknown schema drift"), std::string::npos);
	EXPECT_TRUE(t.apply_log.empty());
	EXPECT_EQ(ids_with(run, StepOutcome::NotReached).size(), migration_plan().size() - 1);
}

TEST(MigrationRun, M0_FingerprintIsOrderAndCaseIndependent) {
	auto a = legacy_schema();
	auto b = legacy_schema();
	std::reverse(b.tables.begin(), b.tables.end());
	b.tables[0].name = "ZENYLOG";
	b.tables[0].engine = "InnoDB"; // engine is not part of the fingerprint
	EXPECT_EQ(schema_fingerprint(a), schema_fingerprint(b));
	b.tables[1].columns.push_back({"extra", "int(11)", true});
	EXPECT_NE(schema_fingerprint(a), schema_fingerprint(b));
}

TEST(MigrationRun, M0_FingerprintIgnoresIntegerDisplayWidth) {
	// MariaDB reports `int(11) unsigned`, MySQL 8 reports `int unsigned` for the same column.
	SchemaSnapshot mariadb{{{"login", "MyISAM", {{"account_id", "int(11) unsigned", false}, {"group_id", "tinyint(3)", false}}}}};
	SchemaSnapshot mysql8{{{"login", "MyISAM", {{"account_id", "int unsigned", false}, {"group_id", "tinyint", false}}}}};
	EXPECT_EQ(schema_fingerprint(mariadb), schema_fingerprint(mysql8));
	SchemaSnapshot widened{{{"login", "MyISAM", {{"account_id", "bigint unsigned", false}, {"group_id", "tinyint", false}}}}};
	EXPECT_NE(schema_fingerprint(mariadb), schema_fingerprint(widened));
	SchemaSnapshot varchar{{{"login", "MyISAM", {{"account_id", "int(11) unsigned", false}, {"group_id", "varchar(3)", false}}}}};
	EXPECT_NE(schema_fingerprint(mariadb), schema_fingerprint(varchar)) << "non-integer lengths still count";
}

TEST(MigrationRun, StepsApplyInPlanOrderUntilFirstClosedGate) {
	FakeTarget t;
	t.schema = legacy_schema();
	const auto run = Migrator(t, apply_opts(t.schema)).run();
	EXPECT_FALSE(run.refused);
	// M7 is gated (char refactor reached mail) -> blocks M7 and everything after it.
	EXPECT_EQ(t.apply_log, (std::vector<std::string>{version(StepId::M0), version(StepId::M6), version(StepId::M6b)}));
	EXPECT_EQ(run.find(StepId::M1)->outcome, StepOutcome::ReportOnly);
	EXPECT_EQ(run.find(StepId::M7)->outcome, StepOutcome::BlockedByGate);
	EXPECT_EQ(ids_with(run, StepOutcome::NotReached),
	          (std::vector<StepId>{StepId::M2, StepId::M4, StepId::M3a, StepId::M3b}));
}

TEST(MigrationRun, M3a_IsGatedOnNoLegacyWriters) {
	FakeTarget t;
	t.schema = legacy_schema();
	auto o = apply_opts(t.schema);
	o.gate_char_refactor_reached_mail = true;
	const auto run = Migrator(t, o).run();
	EXPECT_EQ(run.find(StepId::M4)->outcome, StepOutcome::Applied);
	EXPECT_EQ(run.find(StepId::M3a)->outcome, StepOutcome::BlockedByGate);
	EXPECT_EQ(run.find(StepId::M3b)->outcome, StepOutcome::NotReached);
}

TEST(MigrationRun, M3b_IsGatedOnCleanOrphanReport) {
	FakeTarget t;
	t.schema = legacy_schema();
	t.orphans.findings.push_back({"inventory", "char_id", "char", 3});
	auto o = apply_opts(t.schema);
	o.gate_char_refactor_reached_mail = true;
	o.gate_no_legacy_writers = true;
	const auto run = Migrator(t, o).run();
	EXPECT_EQ(run.find(StepId::M3a)->outcome, StepOutcome::Applied);
	EXPECT_EQ(run.find(StepId::M3b)->outcome, StepOutcome::BlockedByGate);
	EXPECT_FALSE(run.orphans.clean());
}

TEST(MigrationRun, M3b_RunsWhenM3aAppliedAndOrphansClean) {
	FakeTarget t;
	t.schema = legacy_schema();
	auto o = apply_opts(t.schema);
	o.gate_char_refactor_reached_mail = true;
	o.gate_no_legacy_writers = true;
	const auto run = Migrator(t, o).run();
	EXPECT_EQ(run.find(StepId::M3b)->outcome, StepOutcome::Applied);
	EXPECT_EQ(t.apply_log.size(), migration_plan().size() - 1); // all but the M1 report
}

TEST(MigrationRun, M3b_NeverRunsWithoutM3aEvenIfOrphansClean) {
	// Gate M3b requires M3a; with M3a's gate closed the run stops at M3a.
	FakeTarget t;
	t.schema = legacy_schema();
	auto o = apply_opts(t.schema);
	o.gate_char_refactor_reached_mail = true;
	const auto run = Migrator(t, o).run();
	EXPECT_NE(run.find(StepId::M3b)->outcome, StepOutcome::Applied);
}

TEST(MigrationRun, IdempotentSecondRunAppliesNothing) {
	FakeTarget t;
	t.schema = legacy_schema();
	auto o = apply_opts(t.schema);
	o.gate_char_refactor_reached_mail = true;
	o.gate_no_legacy_writers = true;
	(void)Migrator(t, o).run();
	t.apply_log.clear();
	const auto run = Migrator(t, o).run();
	EXPECT_TRUE(t.apply_log.empty());
	EXPECT_EQ(ids_with(run, StepOutcome::AlreadyApplied).size(), migration_plan().size() - 1);
	EXPECT_EQ(run.find(StepId::M1)->outcome, StepOutcome::ReportOnly); // recurring integrity check
}

TEST(MigrationRun, DryRunIsTheDefaultAndExecutesNothing) {
	FakeTarget t;
	t.schema = legacy_schema();
	MigrationOptions o;
	o.known_baselines.insert(schema_fingerprint(t.schema));
	ASSERT_TRUE(o.dry_run);
	const auto run = Migrator(t, o).run();
	EXPECT_TRUE(t.apply_log.empty());
	EXPECT_EQ(run.find(StepId::M6)->outcome, StepOutcome::WouldApply);
	EXPECT_FALSE(run.find(StepId::M6)->statements.empty());
}

TEST(MigrationRun, ForwardOnlyRefusesUnknownRecordedVersion) {
	FakeTarget t;
	t.schema = legacy_schema();
	t.recorded = {version(StepId::M0), "9999_from_a_newer_tool"};
	const auto run = Migrator(t, apply_opts(t.schema)).run();
	EXPECT_TRUE(run.refused);
	EXPECT_TRUE(t.apply_log.empty());
}

TEST(MigrationRun, ForwardOnlyRefusesOutOfOrderHistory) {
	FakeTarget t;
	t.schema = legacy_schema();
	t.recorded = {version(StepId::M0), version(StepId::M2)}; // M6, M6b, M7 missing
	const auto run = Migrator(t, apply_opts(t.schema)).run();
	EXPECT_TRUE(run.refused);
	EXPECT_NE(run.refusal_reason.find("out of order"), std::string::npos);
}

TEST(MigrationRun, FailedStepIsNotRecordedAndStopsTheRun) {
	FakeTarget t;
	t.schema = legacy_schema();
	t.fail_version = version(StepId::M6b);
	const auto run = Migrator(t, apply_opts(t.schema)).run();
	EXPECT_TRUE(run.refused);
	EXPECT_EQ(run.find(StepId::M6b)->outcome, StepOutcome::Refused);
	EXPECT_EQ(std::count(t.recorded.begin(), t.recorded.end(), version(StepId::M6b)), 0);
	EXPECT_EQ(run.find(StepId::M7)->outcome, StepOutcome::NotReached);
}

// ---------------------------------------------------------------- per-step statements

TEST(MigrationStatements, M6_AddsPassHashAndSchemeWithParameterizedBackfill) {
	MigrationOptions o;
	o.legacy_pass_scheme = PassScheme::Md5;
	const auto st = Migrator::statements_for(StepId::M6, o, legacy_schema());
	EXPECT_TRUE(any_sql_contains(st, "`pass_hash`"));
	EXPECT_TRUE(any_sql_contains(st, "ENUM('plain','md5','argon2id')"));
	ASSERT_EQ(st.size(), 2u);
	EXPECT_EQ(std::get<std::string>(st[1].params.at(0)), "md5");
}

TEST(MigrationStatements, M6b_Md5ShimIsOffByDefault) {
	const MigrationOptions o;
	EXPECT_FALSE(o.md5_challenge_shim);
	const auto st = Migrator::statements_for(StepId::M6b, o, legacy_schema());
	EXPECT_FALSE(any_sql_contains(st, "challenge_secret"));
	EXPECT_TRUE(any_sql_contains(st, "legacy_pass_deadline"));
	EXPECT_TRUE(any_sql_contains(st, "`sex` <> 'S'")); // server accounts excluded
}

TEST(MigrationStatements, M6b_ShimOnKeepsChallengeSecret) {
	MigrationOptions o;
	o.md5_challenge_shim = true;
	const auto st = Migrator::statements_for(StepId::M6b, o, legacy_schema());
	EXPECT_TRUE(any_sql_contains(st, "challenge_secret"));
}

TEST(MigrationStatements, M7_ArchivesBeforeDropping) {
	const auto st = Migrator::statements_for(StepId::M7, MigrationOptions{}, legacy_schema());
	ASSERT_EQ(st.size(), 3u);
	EXPECT_NE(st[0].sql.find("_archive_auction"), std::string::npos);
	EXPECT_NE(st[1].sql.find("INSERT INTO `_archive_auction`"), std::string::npos);
	EXPECT_EQ(st[2].sql, "DROP TABLE `auction`");
}

TEST(MigrationStatements, M7_NoAuctionTableMeansNoStatements) {
	auto s = legacy_schema();
	std::erase_if(s.tables, [](const TableSchema& t) { return t.name == "auction"; });
	EXPECT_TRUE(Migrator::statements_for(StepId::M7, MigrationOptions{}, s).empty());
}

TEST(MigrationStatements, M2_ExcludesLogsTablesAndInnoDbTables) {
	const auto st = Migrator::statements_for(StepId::M2, MigrationOptions{}, legacy_schema());
	EXPECT_TRUE(any_sql_contains(st, "ALTER TABLE `login` ENGINE=InnoDB"));
	EXPECT_TRUE(any_sql_contains(st, "ALTER TABLE `char` ENGINE=InnoDB"));
	EXPECT_FALSE(any_sql_contains(st, "picklog"));
	EXPECT_FALSE(any_sql_contains(st, "zenylog"));
	EXPECT_FALSE(any_sql_contains(st, "mail_attachments")); // already InnoDB
	EXPECT_EQ(log_tables().size(), 10u);
}

TEST(MigrationStatements, M4_ZenyCheckAndUnsignedAmount) {
	const auto st = Migrator::statements_for(StepId::M4, MigrationOptions{}, legacy_schema());
	EXPECT_TRUE(any_sql_contains(st, "CHECK (`zeny` >= 0)"));
	EXPECT_TRUE(any_sql_contains(st, "MODIFY `zeny` INT NOT NULL")); // stays INT, no BIGINT rewrite
	EXPECT_FALSE(any_sql_contains(st, "BIGINT"));
	EXPECT_TRUE(any_sql_contains(st, "ALTER TABLE `inventory` MODIFY `amount` INT UNSIGNED"));
	EXPECT_TRUE(any_sql_contains(st, "`homun_id` INT UNSIGNED"));
}

TEST(MigrationStatements, M3a_ConvertsZeroToNull) {
	const auto st = Migrator::statements_for(StepId::M3a, MigrationOptions{}, legacy_schema());
	EXPECT_TRUE(any_sql_contains(st, "UPDATE `char` SET `party_id` = NULL WHERE `party_id` = 0"));
	EXPECT_TRUE(any_sql_contains(st, "MODIFY `guild_id` INT UNSIGNED NULL"));
}

TEST(MigrationStatements, M3b_AddsForeignKeysOnlyForPresentTables) {
	const auto st = Migrator::statements_for(StepId::M3b, MigrationOptions{}, legacy_schema());
	EXPECT_TRUE(any_sql_contains(st, "ALTER TABLE `inventory` ADD CONSTRAINT `fk_inventory_char_id`"));
	EXPECT_TRUE(any_sql_contains(st, "REFERENCES `party` (`party_id`) ON DELETE SET NULL"));
	EXPECT_FALSE(any_sql_contains(st, "`quest`")); // not in this snapshot
}

TEST(MigrationStatements, StatementsUseConfiguredTableNames) {
	MigrationOptions o;
	ASSERT_TRUE(o.tables.set("char_db", "char_custom"));
	auto s = legacy_schema();
	for (auto& t : s.tables)
		if (t.name == "char") t.name = "char_custom";
	const auto st = Migrator::statements_for(StepId::M4, o, s);
	EXPECT_TRUE(any_sql_contains(st, "ALTER TABLE `char_custom`"));
}
