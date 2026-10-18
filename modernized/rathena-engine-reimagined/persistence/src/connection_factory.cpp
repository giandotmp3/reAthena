#include <stdexcept>

#include "rathena/persist/db_config.hpp"
#include "rathena/persist/errors.hpp"
#include "rathena/persist/sql_connection.hpp"
#include "rathena/persist/sqlite_connection.hpp"

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

#ifdef RATHENA_PERSIST_HAVE_LIBPQ
std::unique_ptr<SqlConnection> make_postgres_connection(std::string_view url);  // postgres_connection.cpp
#endif

std::unique_ptr<SqlConnection> open_connection(std::string_view url) {
	const DbConfig cfg = parse_db_url(url);  // refuses mysql / unknown schemes; never echoes the URL
	if (cfg.backend == Backend::Sqlite) {
		std::string path(url.substr(std::string_view("sqlite:").size()));
		if (path.rfind("//", 0) == 0) path.erase(0, 2);  // sqlite:///abs/path and sqlite://rel
		if (path.empty()) throw DbConfigError("sqlite url has no path (use sqlite::memory: or sqlite:<file>)");
		auto conn = std::make_unique<SqliteConnection>(std::move(path));
		conn->connect();
		return conn;
	}
#ifdef RATHENA_PERSIST_HAVE_LIBPQ
	return make_postgres_connection(url);
#else
	throw BackendUnavailableError("PostgreSQL support is not compiled into this build (libpq headers were not "
	                              "found at configure time); target " + redact_url(url));
#endif
}

} // namespace rathena::persist
