#include "rathena/persistence/target_schema.hpp"

#include <stdexcept>
#include <string>

namespace rathena::persistence {
namespace {

/// Logical column types, mapped per dialect. Integers that legacy stores as UNSIGNED INT go to
/// BIGINT on PostgreSQL (no unsigned types there); SQLite INTEGER is 64-bit.
enum class Type { Int, BigInt, Text };

struct Column {
	std::string_view name;
	Type type;
	std::string_view constraints; ///< dialect-neutral: NOT NULL, DEFAULT, CHECK with lower()/length()
};

std::string type_sql(Type t, SqlDialect d) {
	switch (t) {
	case Type::Int: return "INTEGER";
	case Type::BigInt: return d == SqlDialect::Postgres ? "BIGINT" : "INTEGER";
	case Type::Text: return "TEXT";
	}
	return "TEXT";
}

std::string create_table(SqlDialect d, std::string_view table, const std::vector<Column>& cols,
                         std::string_view table_constraints) {
	auto q = [d](std::string_view n) { return quote_identifier(n, d); };
	std::string sql = "CREATE TABLE IF NOT EXISTS " + q(table) + " (";
	bool first = true;
	for (const auto& c : cols) {
		sql += (first ? "\n  " : ",\n  ") + q(c.name) + " " + type_sql(c.type, d);
		if (!c.constraints.empty()) sql += " " + std::string(c.constraints);
		first = false;
	}
	if (!table_constraints.empty()) sql += ",\n  " + std::string(table_constraints);
	sql += "\n)";
	if (d == SqlDialect::Sqlite) sql += " STRICT"; // reject values of the wrong type
	return sql;
}

} // namespace

std::vector<Statement> target_schema_statements(SqlDialect d, const TableNames& t) {
	auto q = [d](std::string_view n) { return quote_identifier(n, d); };
	std::vector<Statement> out;

	out.push_back({create_table(d, t.schema_migrations,
	                            {{"version", Type::Text, "NOT NULL PRIMARY KEY"},
	                             {"applied_at", Type::BigInt, "NOT NULL"}},
	                            ""),
	               {}});

	// login: legacy columns that the rebuilt login-server owns, with times as UNIX seconds.
	// No `user_pass`: the new schema only holds pass_hash/pass_scheme (HITL #1 drop decision).
	out.push_back({create_table(d, t.login,
	                            {
	                                {"account_id", Type::BigInt, "NOT NULL PRIMARY KEY CHECK (\"account_id\" BETWEEN 1 AND 100000000)"},
	                                {"userid", Type::Text, "NOT NULL CHECK (length(\"userid\") BETWEEN 1 AND 23)"},
	                                {"pass_hash", Type::Text, "NOT NULL DEFAULT ''"},
	                                {"pass_scheme", Type::Text, "NOT NULL DEFAULT 'argon2id' CHECK (\"pass_scheme\" IN ('plain', 'md5', 'argon2id'))"},
	                                {"sex", Type::Text, "NOT NULL DEFAULT 'M' CHECK (\"sex\" IN ('M', 'F', 'S'))"},
	                                {"email", Type::Text, "NOT NULL DEFAULT '' CHECK (length(\"email\") <= 39)"},
	                                {"group_id", Type::Int, "NOT NULL DEFAULT 0"},
	                                {"state", Type::BigInt, "NOT NULL DEFAULT 0 CHECK (\"state\" >= 0)"},
	                                {"unban_time", Type::BigInt, "NOT NULL DEFAULT 0"},
	                                {"expiration_time", Type::BigInt, "NOT NULL DEFAULT 0"},
	                                {"logincount", Type::BigInt, "NOT NULL DEFAULT 0 CHECK (\"logincount\" >= 0)"},
	                                {"lastlogin", Type::BigInt, ""},
	                                {"last_ip", Type::Text, "NOT NULL DEFAULT ''"},
	                                {"birthdate", Type::Text, ""},
	                                {"character_slots", Type::Int, "NOT NULL DEFAULT 0 CHECK (\"character_slots\" BETWEEN 0 AND 255)"},
	                                {"pincode", Type::Text, "NOT NULL DEFAULT ''"},
	                                {"pincode_change", Type::BigInt, "NOT NULL DEFAULT 0"},
	                                {"vip_time", Type::BigInt, "NOT NULL DEFAULT 0"},
	                                {"old_group", Type::Int, "NOT NULL DEFAULT 0"},
	                                {"web_auth_token", Type::Text, "UNIQUE"},
	                                {"web_auth_token_enabled", Type::Int, "NOT NULL DEFAULT 0 CHECK (\"web_auth_token_enabled\" IN (0, 1))"},
	                            },
	                            ""),
	               {}});
	// BR-LOGIN-038 case-insensitive lookups use lower(userid); keep them indexed.
	out.push_back({"CREATE INDEX IF NOT EXISTS " + q(t.login + "_userid_lower") + " ON " + q(t.login) + " (lower(" +
	                   q("userid") + "))",
	               {}});

	// `##` registries: owned rows, removed with the account (BR-LOGIN-037).
	const std::string fk = "FOREIGN KEY (" + q("account_id") + ") REFERENCES " + q(t.login) + " (" + q("account_id") +
	                       ") ON DELETE CASCADE";
	const std::string pk = "PRIMARY KEY (" + q("account_id") + ", " + q("key") + ", " + q("index") + ")";
	out.push_back({create_table(d, t.global_acc_reg_num,
	                            {{"account_id", Type::BigInt, "NOT NULL"},
	                             {"key", Type::Text, "NOT NULL CHECK (length(\"key\") BETWEEN 1 AND 32)"},
	                             {"index", Type::BigInt, "NOT NULL DEFAULT 0 CHECK (\"index\" >= 0)"},
	                             {"value", Type::BigInt, "NOT NULL DEFAULT 0"}},
	                            pk + ",\n  " + fk),
	               {}});
	out.push_back({create_table(d, t.global_acc_reg_str,
	                            {{"account_id", Type::BigInt, "NOT NULL"},
	                             {"key", Type::Text, "NOT NULL CHECK (length(\"key\") BETWEEN 1 AND 32)"},
	                             {"index", Type::BigInt, "NOT NULL DEFAULT 0 CHECK (\"index\" >= 0)"},
	                             {"value", Type::Text, "NOT NULL DEFAULT '' CHECK (length(\"value\") <= 254)"}},
	                            pk + ",\n  " + fk),
	               {}});
	return out;
}

bool create_target_schema(SqlConnection& conn, const TableNames& names) {
	auto statements = target_schema_statements(conn.dialect(), names);
	const auto d = conn.dialect();
	auto q = [d](std::string_view n) { return quote_identifier(n, d); };
	// The application clock is not available here; the schema creation time is informational.
	const std::string now = d == SqlDialect::Postgres ? "CAST(EXTRACT(EPOCH FROM now()) AS BIGINT)" : "CAST(strftime('%s', 'now') AS INTEGER)";
	statements.push_back({"INSERT INTO " + q(names.schema_migrations) + " (" + q("version") + ", " + q("applied_at") +
	                          ") VALUES (?, " + now + ") ON CONFLICT (" + q("version") + ") DO NOTHING",
	                      {std::string(kTargetSchemaVersion)}});
	return conn.execute_in_transaction(statements);
}

} // namespace rathena::persistence
