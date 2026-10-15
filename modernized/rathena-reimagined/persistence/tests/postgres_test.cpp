// PostgreSQL backend: placeholder rewriting (always), connection errors (needs libpq), and live
// behaviour against RATHENA_TEST_PG_URL (skipped without it): binding, byte-exact text in a
// SQL_ASCII database, bytea, scripts, transactions and visibility across connections.
#include <gtest/gtest.h>

#include "pg_test_support.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/postgres.hpp"
#include "rathena/persistence/sql_repositories.hpp"
#include "rathena/persistence/target_schema.hpp"

using namespace rathena::persistence;

TEST(PostgresPlaceholders, RewritesQuestionMarksInOrder) {
	std::size_t n = 0;
	EXPECT_EQ(to_postgres_placeholders("SELECT a FROM t WHERE b = ? AND c IN (?, ?)", &n),
	          "SELECT a FROM t WHERE b = $1 AND c IN ($2, $3)");
	EXPECT_EQ(n, 3u);
}

TEST(PostgresPlaceholders, LeavesLiteralsIdentifiersAndCommentsAlone) {
	std::size_t n = 0;
	EXPECT_EQ(to_postgres_placeholders("SELECT '?', 'it''s ?', \"q?\" FROM t -- why?\nWHERE x = ? /* ? */", &n),
	          "SELECT '?', 'it''s ?', \"q?\" FROM t -- why?\nWHERE x = $1 /* ? */");
	EXPECT_EQ(n, 1u);
	EXPECT_EQ(to_postgres_placeholders("CREATE FUNCTION f() AS $$ SELECT ? $$", &n), "CREATE FUNCTION f() AS $$ SELECT ? $$");
	EXPECT_EQ(n, 0u);
}

TEST(PostgresConnection, ConnectFailureNeverEchoesThePassword) {
	if (!postgres_client_available()) GTEST_SKIP() << "built without libpq";
	// Port 1 on loopback: refused immediately, no server needed.
	PostgresConnection conn(parse_db_url("postgresql://rathena:fake-db-pass-0000@127.0.0.1:1/rathena?connect_timeout=2"));
	try {
		conn.connect();
		ADD_FAILURE() << "connected to port 1?";
	} catch (const RepositoryError& e) {
		const std::string what = e.what();
		EXPECT_EQ(what.find("fake-db-pass-0000"), std::string::npos) << "password leaked: " << what;
		EXPECT_NE(what.find("postgresql://rathena:***@127.0.0.1:1/rathena"), std::string::npos) << what;
	}
}

class PostgresLive : public ::testing::Test {
protected:
	void SetUp() override {
		if (const auto why = pgtest::pg_unavailable()) GTEST_SKIP() << *why;
		pg_ = std::make_unique<pgtest::PgScratchSchema>();
	}
	PostgresConnection& conn() { return pg_->conn(); }
	std::unique_ptr<pgtest::PgScratchSchema> pg_;
};

TEST_F(PostgresLive, ValuesRoundTripWithTheirTypes) {
	conn().execute({"CREATE TABLE \"t\" (\"i\" integer, \"b\" bigint, \"s\" text, \"d\" bytea, \"n\" text)", {}});
	std::string all_bytes;
	for (int c = 0; c < 256; ++c) all_bytes.push_back(static_cast<char>(c));
	conn().execute({"INSERT INTO \"t\" VALUES (?, ?, ?, ?, ?)",
	                {std::int64_t{-7}, std::uint64_t{4'000'000'000}, std::string("a?'b"), Blob{all_bytes}, std::monostate{}}});
	EXPECT_EQ(conn().affected_rows(), 1u);
	const auto rows = conn().query({"SELECT * FROM \"t\" WHERE \"s\" = ?", {std::string("a?'b")}});
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(std::get<std::int64_t>(rows[0].at("i")), -7);
	EXPECT_EQ(std::get<std::int64_t>(rows[0].at("b")), 4'000'000'000);
	EXPECT_EQ(std::get<std::string>(rows[0].at("s")), "a?'b");
	EXPECT_EQ(std::get<Blob>(rows[0].at("d")).bytes, all_bytes) << "bytea is byte exact, including NUL";
	EXPECT_TRUE(std::holds_alternative<std::monostate>(rows[0].at("n")));
	EXPECT_THROW(conn().execute({"INSERT INTO \"t\" (\"s\") VALUES (?)", {std::string("a\0b", 3)}}), RepositoryError)
	    << "PostgreSQL text cannot hold NUL";
	EXPECT_THROW(conn().execute({"SELECT ?", {}}), RepositoryError) << "parameter count is checked";
}

TEST_F(PostgresLive, HighBitTextIsByteExactInASqlAsciiDatabase) {
	const auto enc = conn().query({"SELECT pg_encoding_to_char(encoding) AS e FROM pg_database WHERE datname = current_database()", {}});
	ASSERT_EQ(enc.size(), 1u);
	if (std::get<std::string>(enc[0].at("e")) != "SQL_ASCII")
		GTEST_SKIP() << "RATHENA_TEST_PG_URL database is not SQL_ASCII (rAthena databases are)";
	conn().execute({"CREATE TABLE \"names\" (\"name\" varchar(24))", {}});
	const std::string euc_kr = "\xC7\xD1\xB1\xDB\xB0\xFA"; // raw client bytes, not UTF-8
	conn().execute({"INSERT INTO \"names\" VALUES (?)", {euc_kr}});
	const auto rows = conn().query({"SELECT \"name\", length(\"name\") AS n FROM \"names\"", {}});
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(std::get<std::string>(rows[0].at("name")), euc_kr);
	EXPECT_EQ(std::get<std::int64_t>(rows[0].at("n")), 6) << "length counts bytes, as MySQL latin1 did";
}

TEST_F(PostgresLive, ScriptsRunAtomically) {
	conn().execute_script("CREATE TABLE \"a\" (\"x\" integer); INSERT INTO \"a\" VALUES (1);"
	                      " CREATE FUNCTION f() RETURNS integer LANGUAGE sql AS $$ SELECT 1; $$;");
	EXPECT_EQ(conn().query({"SELECT \"x\" FROM \"a\"", {}}).size(), 1u);
	EXPECT_THROW(conn().execute_script("CREATE TABLE \"b\" (\"x\" integer); SELECT * FROM no_such_table;"), RepositoryError);
	EXPECT_TRUE(conn().query({"SELECT 1 FROM information_schema.tables WHERE table_schema = current_schema()"
	                          " AND table_name = 'b'",
	                          {}})
	                .empty())
	    << "the failed script left nothing behind";
}

TEST_F(PostgresLive, CommittedWritesAreVisibleToAnotherConnection) {
	ASSERT_TRUE(create_target_schema(conn()));
	auto other = pg_->another_connection();
	SqlAccountRepository writer(conn(), TableNames{});
	SqlAccountRepository reader(*other, TableNames{});
	Account a;
	a.userid = "shared01";
	a.pass_scheme = PassScheme::Argon2id;
	ASSERT_TRUE(writer.create(a).ok());
	EXPECT_TRUE(reader.find(a.id).has_value());
}
