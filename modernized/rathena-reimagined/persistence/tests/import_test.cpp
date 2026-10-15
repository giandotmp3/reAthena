// `rathena-migrate import` (migrate/import.hpp): the pure value/column/order rules without any
// database, then whole imports from a fake legacy source into SQLite (always) and PostgreSQL
// (with RATHENA_TEST_PG_URL), using the generated schema files in sql-files/.
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <sstream>

#include "pg_test_support.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/migrate/import.hpp"
#include "rathena/persistence/migrate/schema.hpp"
#include "rathena/persistence/sqlite.hpp"

using namespace rathena::persistence;
using namespace rathena::persistence::migrate;

namespace {

SourceColumn src_col(std::string name, std::string data_type, bool nullable = false, bool auto_inc = false) {
	SourceColumn c;
	c.name = std::move(name);
	c.data_type = std::move(data_type);
	c.column_type = c.data_type;
	c.nullable = nullable;
	c.auto_increment = auto_inc;
	return c;
}

TargetColumn tgt_col(std::string name, TargetType type, bool nullable = false) {
	TargetColumn c;
	c.name = std::move(name);
	c.type = type;
	c.nullable = nullable;
	return c;
}

} // namespace

// ---------------------------------------------------------------- values

TEST(ImportValues, ZeroDatesAreRecognized) {
	EXPECT_TRUE(is_zero_date("0000-00-00 00:00:00"));
	EXPECT_TRUE(is_zero_date("0000-00-00"));
	EXPECT_TRUE(is_zero_date("2024-00-10 12:00:00")) << "partial zero";
	EXPECT_TRUE(is_zero_date("2024-05-00"));
	EXPECT_FALSE(is_zero_date("2024-05-01 00:00:00"));
	EXPECT_FALSE(is_zero_date("1970-01-01"));
	EXPECT_FALSE(is_zero_date(""));
	EXPECT_FALSE(is_zero_date("hello world"));
}

TEST(ImportValues, ZeroDateBecomesNullOrTheEpoch) {
	const auto dt = src_col("lastlogin", "datetime", true);
	auto r = convert_value(dt, tgt_col("lastlogin", TargetType::Timestamp, true), std::string("0000-00-00 00:00:00"));
	EXPECT_TRUE(std::holds_alternative<std::monostate>(r.value));
	EXPECT_EQ(r.adjustment, "zero date -> NULL");
	r = convert_value(dt, tgt_col("time", TargetType::Timestamp, false), std::string("0000-00-00 00:00:00"));
	EXPECT_EQ(std::get<std::string>(r.value), "1970-01-01 00:00:00");
	EXPECT_EQ(r.adjustment, "zero date -> 1970-01-01 00:00:00");
	r = convert_value(src_col("birthdate", "date"), tgt_col("birthdate", TargetType::Text, false), std::string("0000-00-00"));
	EXPECT_EQ(std::get<std::string>(r.value), "1970-01-01") << "SQLite keeps dates as text";
	r = convert_value(dt, tgt_col("lastlogin", TargetType::Text, true), std::string("2024-05-01 10:20:30"));
	EXPECT_EQ(std::get<std::string>(r.value), "2024-05-01 10:20:30");
	EXPECT_TRUE(r.adjustment.empty());
	r = convert_value(src_col("note", "varchar"), tgt_col("note", TargetType::Text), std::string("0000-00-00"));
	EXPECT_EQ(std::get<std::string>(r.value), "0000-00-00") << "only date columns are dates";
}

TEST(ImportValues, TextIsCopiedByteForByte) {
	const std::string latin1 = "\xC7\xD1\xB1\xDB \xE9\xFF";
	const auto r = convert_value(src_col("name", "varchar"), tgt_col("name", TargetType::Text), latin1);
	EXPECT_EQ(std::get<std::string>(r.value), latin1);
	EXPECT_TRUE(r.adjustment.empty());
}

TEST(ImportValues, IntegersAreBoundAsIntegers) {
	const auto c = src_col("zeny", "int");
	EXPECT_EQ(std::get<std::int64_t>(convert_value(c, tgt_col("zeny", TargetType::Integer), std::string("-42")).value), -42);
	EXPECT_EQ(std::get<std::int64_t>(convert_value(c, tgt_col("x", TargetType::Integer), std::string("4294967295")).value),
	          4294967295);
	const auto bad = convert_value(c, tgt_col("x", TargetType::Integer), std::string("12abc"));
	EXPECT_EQ(std::get<std::int64_t>(bad.value), 0);
	EXPECT_EQ(bad.adjustment, "'12abc' is not an integer -> 0");
	EXPECT_EQ(std::get<std::int64_t>(convert_value(c, tgt_col("x", TargetType::Integer), std::nullopt).value), 0)
	    << "NULL into NOT NULL";
	EXPECT_TRUE(std::holds_alternative<std::monostate>(convert_value(c, tgt_col("x", TargetType::Integer, true), std::nullopt).value));
}

TEST(ImportValues, BlobsStayBinary) {
	const std::string bytes("\x00\x01\xff\x80", 4);
	const auto r = convert_value(src_col("emblem_data", "blob", true), tgt_col("emblem_data", TargetType::Blob, true), bytes);
	EXPECT_EQ(std::get<Blob>(r.value).bytes, bytes);
}

TEST(ImportValues, ValuesOutsideTheCheckListAreMappedAndReported) {
	auto sex = tgt_col("sex", TargetType::Text);
	sex.allowed = {"M", "F", "S"};
	sex.default_literal = "M";
	const auto e = src_col("sex", "enum");
	EXPECT_TRUE(convert_value(e, sex, std::string("F")).adjustment.empty());
	auto r = convert_value(e, sex, std::string("")); // MySQL stores '' for an invalid enum value
	EXPECT_EQ(std::get<std::string>(r.value), "M");
	EXPECT_EQ(r.adjustment, "'' not allowed -> 'M'");
	sex.default_literal.reset();
	EXPECT_EQ(std::get<std::string>(convert_value(e, sex, std::string("x")).value), "M") << "first allowed value";
	sex.nullable = true;
	EXPECT_TRUE(std::holds_alternative<std::monostate>(convert_value(e, sex, std::string("x")).value));
}

TEST(ImportValues, NulBytesAndOverlongTextAreCut) {
	auto r = convert_value(src_col("name", "varchar"), tgt_col("name", TargetType::Text), std::string("ab\0cd", 5));
	EXPECT_EQ(std::get<std::string>(r.value), "ab");
	EXPECT_EQ(r.adjustment, "cut at a NUL byte");
	auto limited = tgt_col("name", TargetType::Text);
	limited.max_length = 4;
	r = convert_value(src_col("name", "varchar"), limited, std::string("abcdef"));
	EXPECT_EQ(std::get<std::string>(r.value), "abcd");
	EXPECT_EQ(r.adjustment, "longer than 4 bytes -> cut");
}

// ---------------------------------------------------------------- columns, order, parsing

TEST(ImportPlan, ColumnsAreTheIntersectionByName) {
	SourceTable s{"login", {src_col("account_id", "int"), src_col("UserID", "varchar"), src_col("custom", "int")}, 0};
	TargetTable t{"login", {tgt_col("account_id", TargetType::Integer), tgt_col("userid", TargetType::Text),
	                        tgt_col("web_auth_token", TargetType::Text, true)}, 0};
	const auto plan = plan_columns(s, t);
	EXPECT_EQ(plan.copy, (std::vector<std::pair<std::size_t, std::size_t>>{{0, 0}, {1, 1}}));
	EXPECT_EQ(plan.source_only, std::vector<std::string>{"custom"});
	EXPECT_EQ(plan.target_only, std::vector<std::string>{"web_auth_token"});
}

TEST(ImportPlan, ParentsAreCopiedBeforeChildren) {
	const auto order = copy_order({"mail_attachments", "zeta", "mail", "alpha"}, {{"mail_attachments", "mail"}});
	EXPECT_EQ(order, (std::vector<std::string>{"alpha", "mail", "mail_attachments", "zeta"}));
	EXPECT_EQ(copy_order({"a_child", "z_parent"}, {{"a_child", "z_parent"}}), (std::vector<std::string>{"z_parent", "a_child"}));
	EXPECT_EQ(copy_order({"b", "a"}, {{"a", "b"}, {"b", "a"}}), (std::vector<std::string>{"a", "b"})) << "cycle";
	EXPECT_EQ(copy_order({"t"}, {{"t", "t"}}), std::vector<std::string>{"t"}) << "self reference";
}

TEST(ImportPlan, SchemaTextParsing) {
	EXPECT_EQ(quoted_literals("'M'::character varying"), std::vector<std::string>{"M"});
	EXPECT_EQ(quoted_literals("CHECK (((sex)::text = ANY ((ARRAY['F'::character varying, 'M'::character varying])::text[])))"),
	          (std::vector<std::string>{"F", "M"}));
	EXPECT_EQ(quoted_literals("'it''s', \"'not'\""), std::vector<std::string>{"it's"});
	const std::string create = "CREATE TABLE \"login\" (\n  \"sex\" TEXT NOT NULL DEFAULT 'M' CHECK (\"sex\" IN ('M','F','S')),\n"
	                           "  \"state\" TEXT CHECK (\"state\" IN ('0','1'))\n)";
	EXPECT_EQ(check_in_values(create, "sex"), (std::vector<std::string>{"M", "F", "S"}));
	EXPECT_EQ(check_in_values(create, "state"), (std::vector<std::string>{"0", "1"}));
	EXPECT_TRUE(check_in_values(create, "userid").empty());
	const std::string file = "CREATE TABLE IF NOT EXISTS \"a\" (\n);\nINSERT OR IGNORE INTO \"a\" VALUES (1);\n"
	                         "INSERT INTO \"a\" VALUES (2) ON CONFLICT DO NOTHING;\nCREATE TABLE IF NOT EXISTS \"b\" (\n);\n"
	                         "INSERT INTO sqlite_sequence (name, seq) SELECT 'a', 1;\n";
	EXPECT_EQ(schema_file_tables(file), (std::set<std::string>{"a", "b"}));
	EXPECT_EQ(schema_file_seed_rows(file), (std::map<std::string, std::uint64_t>{{"a", 2}}));
	EXPECT_EQ(target_type("character varying", SqlDialect::Postgres), TargetType::Text);
	EXPECT_EQ(target_type("USER-DEFINED", SqlDialect::Postgres), TargetType::Text) << "citext";
	EXPECT_EQ(target_type("timestamp without time zone", SqlDialect::Postgres), TargetType::Timestamp);
	EXPECT_EQ(target_type("bytea", SqlDialect::Postgres), TargetType::Blob);
	EXPECT_EQ(target_type("smallint", SqlDialect::Postgres), TargetType::Integer);
	EXPECT_EQ(target_type("INTEGER", SqlDialect::Sqlite), TargetType::Integer);
	EXPECT_EQ(target_type("BLOB", SqlDialect::Sqlite), TargetType::Blob);
	EXPECT_EQ(target_type("TEXT", SqlDialect::Sqlite), TargetType::Text);
}

TEST(ImportPlan, FingerprintExclusionsAndBaselines) {
	EXPECT_TRUE(excluded_from_fingerprint("zenylog"));
	EXPECT_TRUE(excluded_from_fingerprint("item_db_re"));
	EXPECT_TRUE(excluded_from_fingerprint("mob_skill_db2"));
	EXPECT_TRUE(excluded_from_fingerprint("schema_migrations"));
	EXPECT_FALSE(excluded_from_fingerprint("login"));
	EXPECT_EQ(known_legacy_baselines().size(), 2u);
}

// ---------------------------------------------------------------- whole imports

namespace {

/// An in-memory legacy database: tables plus raw rows (all values as MySQL sends them).
class FakeSource final : public LegacySource {
public:
	struct Table {
		SourceTable meta;
		std::vector<RawRow> rows;
	};
	std::vector<Table> data;

	[[nodiscard]] std::string describe() const override { return "fake://legacy"; }
	std::vector<SourceTable> tables() override {
		std::vector<SourceTable> out;
		for (const auto& t : data) out.push_back(t.meta);
		return out;
	}
	std::uint64_t row_count(const SourceTable& table) override { return find(table.name).rows.size(); }
	void read_rows(const SourceTable& table, const std::vector<const SourceColumn*>& columns, bool, std::size_t batch_size,
	               const std::function<void(std::vector<RawRow>&)>& sink) override {
		const auto& t = find(table.name);
		std::vector<std::size_t> idx;
		for (const auto* c : columns)
			for (std::size_t i = 0; i < t.meta.columns.size(); ++i)
				if (t.meta.columns[i].name == c->name) idx.push_back(i);
		std::vector<RawRow> batch;
		for (const auto& row : t.rows) {
			RawRow r;
			for (const auto i : idx) r.push_back(row[i]);
			batch.push_back(std::move(r));
			if (batch.size() == batch_size) {
				sink(batch);
				batch.clear();
			}
		}
		if (!batch.empty()) sink(batch);
	}

private:
	const Table& find(const std::string& name) const {
		return *std::find_if(data.begin(), data.end(), [&](const Table& t) { return t.meta.name == name; });
	}
};

const std::string kHangul = "\xC7\xD1\xB1\xDB"; // EUC-KR bytes as the client sent them
const std::string kCp1252 = "Jos\xE9";

std::string emblem_bytes() {
	std::string b;
	for (int c = 0; c < 256; ++c) b.push_back(static_cast<char>(c));
	return b;
}

FakeSource legacy_database() {
	FakeSource s;
	auto col = [](std::string n, std::string type, bool nullable = false, bool ai = false) {
		return src_col(std::move(n), std::move(type), nullable, ai);
	};
	s.data.push_back({{"login",
	                   {col("account_id", "int", false, true), col("userid", "varchar"), col("user_pass", "varchar"),
	                    col("sex", "enum"), col("lastlogin", "datetime", true), col("birthdate", "date", true),
	                    col("legacy_extra", "int")},
	                   2'000'010},
	                  {{"1", "s1", "p1", "S", std::nullopt, std::nullopt, "0"},
	                   {"2000000", "hero", "pw", "M", "0000-00-00 00:00:00", "0000-00-00", "0"},
	                   {"2000001", kHangul, "pw", "", "2024-05-01 10:20:30", "1990-02-03", "0"}}});
	s.data.push_back({{"char",
	                   {col("char_id", "int", false, true), col("account_id", "int"), col("name", "varchar"),
	                    col("sex", "enum"), col("last_login", "datetime", true)},
	                   150'020},
	                  {{"150000", "2000000", kCp1252, "M", "0000-00-00 00:00:00"},
	                   {"150001", "2000001", kHangul, "F", "2024-05-01 10:20:30"}}});
	s.data.push_back({{"guild",
	                   {col("guild_id", "int", false, true), col("name", "varchar"), col("char_id", "int"),
	                    col("emblem_data", "blob", true)},
	                   12},
	                  {{"11", "Guild" + kHangul, "150000", emblem_bytes()}}});
	s.data.push_back({{"mail", {col("id", "bigint", false, true), col("dest_id", "int"), col("title", "varchar")}, 4},
	                  {{"1", "150000", "hi"}, {"2", "150001", "there"}, {"3", "150001", "again"}}});
	// One attachment per mail: the generated SQLite schema currently makes mail_attachments.id the
	// rowid key on its own (MySQL's key is (id, index)), so a second attachment of one mail fails.
	s.data.push_back({{"mail_attachments", {col("id", "bigint"), col("index", "tinyint"), col("nameid", "int")}, 0},
	                  {{"1", "0", "501"}, {"2", "0", "502"}, {"3", "0", "503"}}});
	s.data.push_back({{"zenylog",
	                   {col("id", "int", false, true), col("time", "datetime"), col("type", "enum"), col("amount", "int")},
	                   5},
	                  {{"4", "0000-00-00 00:00:00", "S", "100"}}});
	s.data.push_back({{"db_roulette", {col("index", "int"), col("level", "smallint"), col("item_id", "int")}, 0}, {}});
	s.data.push_back({{"item_db", {col("id", "int"), col("name_english", "varchar")}, 0}, {{"501", "Red Potion"}}});
	return s;
}

class ImportRun : public ::testing::TestWithParam<SqlDialect> {
protected:
	void SetUp() override {
		if (GetParam() == SqlDialect::Postgres) {
			if (const auto why = pgtest::pg_unavailable()) GTEST_SKIP() << *why;
			pg_ = std::make_unique<pgtest::PgScratchSchema>();
			const auto enc = pg_->conn().query({"SELECT pg_encoding_to_char(encoding) AS e FROM pg_database"
			                                    " WHERE datname = current_database()",
			                                    {}});
			if (enc.empty() || std::get<std::string>(enc[0].at("e")) != "SQL_ASCII")
				GTEST_SKIP() << "RATHENA_TEST_PG_URL database is not SQL_ASCII (rAthena databases are)";
			conn_ = &pg_->conn();
		} else {
			sqlite_ = std::make_unique<SqliteConnection>(":memory:");
			conn_ = sqlite_.get();
		}
		options.schema_dir = RATHENA_LEGACY_SQL_DIR;
		options.batch_size = 2; // several batches per table
	}

	/// Imports `src`, accepting its fingerprint (the fake is not a real legacy schema).
	ImportReport import(FakeSource& src) {
		std::ostringstream log;
		auto first = run_import(src, conn(), options, log);
		if (!first.refused || first.refusal.find("unknown source schema") == std::string::npos) return first;
		options.known_baselines.insert(first.fingerprint);
		return run_import(src, conn(), options, log);
	}

	std::string text(const std::string& sql) {
		const auto rows = conn().query({sql, {}});
		if (rows.empty() || std::holds_alternative<std::monostate>(rows[0].begin()->second)) return "(null)";
		const auto& v = rows[0].begin()->second;
		if (const auto* s = std::get_if<std::string>(&v)) return *s;
		if (const auto* b = std::get_if<Blob>(&v)) return b->bytes;
		return std::to_string(std::get<std::int64_t>(v));
	}

	SqlConnection& conn() { return *conn_; }
	ImportOptions options;

private:
	std::unique_ptr<SqliteConnection> sqlite_;
	std::unique_ptr<pgtest::PgScratchSchema> pg_;
	SqlConnection* conn_ = nullptr;
};

const TableReport& table(const ImportReport& r, const std::string& name) {
	return *std::find_if(r.tables.begin(), r.tables.end(), [&](const TableReport& t) { return t.table == name; });
}

} // namespace

INSTANTIATE_TEST_SUITE_P(Targets, ImportRun, ::testing::Values(SqlDialect::Sqlite, SqlDialect::Postgres),
                         [](const ::testing::TestParamInfo<SqlDialect>& info) { return std::string(to_string(info.param)); });

TEST_P(ImportRun, UnknownSourceSchemaIsRefusedBeforeAnyWrite) {
	auto src = legacy_database();
	std::ostringstream log;
	const auto r = run_import(src, conn(), options, log);
	ASSERT_TRUE(r.refused);
	EXPECT_NE(r.refusal.find("--baseline=" + r.fingerprint), std::string::npos) << r.refusal;
	EXPECT_TRUE(inspect_target(conn()).tables.empty()) << "nothing was created";
}

TEST_P(ImportRun, CopiesEveryCommonTableByteForByte) {
	auto src = legacy_database();
	const auto r = import(src);
	std::ostringstream summary;
	print_summary(r, summary);
	ASSERT_TRUE(r.ok()) << summary.str();
	EXPECT_TRUE(r.created_schema);

	EXPECT_EQ(table(r, "login").target_rows, 3u) << "the seeded s1 account was replaced by the source's rows";
	EXPECT_EQ(table(r, "char").target_rows, 2u);
	EXPECT_EQ(table(r, "mail_attachments").target_rows, 3u) << "copied after its parent mail";
	EXPECT_EQ(table(r, "db_roulette").status, TableReport::Status::KeptDefaults);
	EXPECT_GT(table(r, "db_roulette").target_rows, 0u);
	EXPECT_EQ(r.source_only, std::vector<std::string>{"item_db"});
	EXPECT_NE(std::find(r.target_only.begin(), r.target_only.end(), "inventory"), r.target_only.end());

	// Raw client bytes survive unchanged (latin1 columns on MySQL; SQL_ASCII / SQLite text here).
	EXPECT_EQ(text("SELECT \"userid\" FROM \"login\" WHERE \"account_id\" = 2000001"), kHangul);
	EXPECT_EQ(text("SELECT \"name\" FROM \"char\" WHERE \"char_id\" = 150000"), kCp1252);
	EXPECT_EQ(text("SELECT \"name\" FROM \"guild\" WHERE \"guild_id\" = 11"), "Guild" + kHangul);
	EXPECT_EQ(text("SELECT \"emblem_data\" FROM \"guild\" WHERE \"guild_id\" = 11"), emblem_bytes());

	// Zero dates, invalid enum values.
	EXPECT_EQ(text("SELECT \"lastlogin\" FROM \"login\" WHERE \"account_id\" = 2000000"), "(null)");
	EXPECT_EQ(text("SELECT \"birthdate\" FROM \"login\" WHERE \"account_id\" = 2000000"), "(null)");
	EXPECT_EQ(text("SELECT \"time\" FROM \"zenylog\" WHERE \"id\" = 4"), "1970-01-01 00:00:00");
	EXPECT_EQ(text("SELECT \"sex\" FROM \"login\" WHERE \"account_id\" = 2000001"), "M");
	EXPECT_EQ(text("SELECT \"lastlogin\" FROM \"login\" WHERE \"account_id\" = 2000001"), "2024-05-01 10:20:30");
	EXPECT_EQ(table(r, "login").adjustments.at("sex: '' not allowed -> 'M'"), 1u);
	EXPECT_EQ(table(r, "login").adjustments.at("lastlogin: zero date -> NULL"), 1u);
	EXPECT_EQ(table(r, "zenylog").adjustments.at("time: zero date -> 1970-01-01 00:00:00"), 1u);
	EXPECT_NE(std::find(table(r, "login").notes.begin(), table(r, "login").notes.end(),
	                    "column legacy_extra is not in the target: dropped"),
	          table(r, "login").notes.end());

	// New rows continue where the source's AUTO_INCREMENT stood, not at max + 1.
	conn().execute({"INSERT INTO \"char\" (\"account_id\", \"name\") VALUES (2000000, 'newbie')", {}});
	EXPECT_EQ(text("SELECT \"char_id\" FROM \"char\" WHERE \"name\" = 'newbie'"), "150020");
	conn().execute({"INSERT INTO \"login\" (\"userid\", \"user_pass\") VALUES ('fresh', 'x')", {}});
	EXPECT_EQ(text("SELECT \"account_id\" FROM \"login\" WHERE \"userid\" = 'fresh'"), "2000010");
	conn().execute({"INSERT INTO \"mail\" (\"dest_id\") VALUES (150000)", {}});
	EXPECT_EQ(text("SELECT MAX(\"id\") FROM \"mail\""), "4");

	// The legacy servers' case-insensitive name lookups still work on the copied rows.
	EXPECT_EQ(text("SELECT \"account_id\" FROM \"login\" WHERE \"userid\" = 'HERO'"), "2000000");
}

TEST_P(ImportRun, ASecondImportIntoTheFilledTargetIsRefused) {
	auto src = legacy_database();
	ASSERT_TRUE(import(src).ok());
	std::ostringstream log;
	const auto again = run_import(src, conn(), options, log);
	ASSERT_TRUE(again.refused);
	EXPECT_NE(again.refusal.find("already hold rows"), std::string::npos) << again.refusal;
}

TEST_P(ImportRun, OrphanRowsFailTheImportAndAreReported) {
	auto src = legacy_database();
	for (auto& t : src.data)
		if (t.meta.name == "mail_attachments") t.rows.push_back({"99", "0", "504"}); // no mail 99
	const auto r = import(src);
	EXPECT_FALSE(r.ok());
	const auto& att = table(r, "mail_attachments");
	EXPECT_EQ(att.failed_rows, 1u);
	EXPECT_EQ(att.status, TableReport::Status::Mismatch);
	EXPECT_EQ(att.target_rows, 3u) << "the good rows of the failed batch were still copied";
	EXPECT_EQ(att.errors.size(), 1u);
	std::ostringstream summary;
	print_summary(r, summary);
	EXPECT_NE(summary.str().find("import FAILED"), std::string::npos) << summary.str();
}

TEST_P(ImportRun, TargetThatIsNotAnRathenaDatabaseIsRefused) {
	conn().execute({"CREATE TABLE \"unrelated\" (\"x\" INTEGER)", {}});
	auto src = legacy_database();
	const auto r = import(src);
	ASSERT_TRUE(r.refused);
	EXPECT_NE(r.refusal.find("neither empty nor an rAthena database"), std::string::npos) << r.refusal;
}

TEST_P(ImportRun, LogOnlySourceIsAccepted) {
	FakeSource logs;
	const auto all = legacy_database();
	for (const auto& t : all.data)
		if (t.meta.name == "zenylog") logs.data.push_back(t);
	std::ostringstream log;
	const auto r = run_import(logs, conn(), options, log); // no baseline needed
	std::ostringstream summary;
	print_summary(r, summary);
	ASSERT_TRUE(r.ok()) << summary.str();
	EXPECT_EQ(r.fingerprint, "(log tables only)");
	EXPECT_EQ(table(r, "zenylog").target_rows, 1u);
}
