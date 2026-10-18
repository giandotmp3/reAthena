#include <stdexcept>

#include "rathena/persist/db_config.hpp"
#include "rathena/persist/errors.hpp"
#include "rathena/persist/sql_connection.hpp"

namespace rathena::persist {

std::string_view to_string(SqlDialect d) noexcept {
	return d == SqlDialect::Sqlite ? "sqlite" : "postgres";
}

std::string quote_identifier(std::string_view name, SqlDialect) {
	if (name.empty()) throw std::invalid_argument("empty identifier");
	for (char c : name) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		if (!ok) throw std::invalid_argument("invalid identifier");
	}
	return "\"" + std::string(name) + "\"";
}

std::unique_ptr<SqlConnection> open_connection(std::string_view url) {
	(void)parse_db_url(url); // validates scheme; refuses mysql
	throw NotImplementedError("SqliteConnection/PostgresConnection are not lifted yet (README: Lift plan)");
}

} // namespace rathena::persist
