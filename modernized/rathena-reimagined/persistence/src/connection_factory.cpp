#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/mysql.hpp"
#include "rathena/persistence/sql_connection.hpp"
#include "rathena/persistence/sqlite.hpp"

namespace rathena::persistence {

std::unique_ptr<SqlConnection> open_connection(std::string_view url) {
	if (url == "sqlite::memory:") return std::make_unique<SqliteConnection>(":memory:");
	if (url.starts_with("sqlite://")) {
		const auto path = url.substr(std::string_view("sqlite://").size());
		if (!path.starts_with("/")) throw DbConfigError("sqlite:// URLs take an absolute path (sqlite:///abs/file.db)");
		return std::make_unique<SqliteConnection>(std::string(path));
	}
	if (url.starts_with("sqlite:")) {
		const auto path = url.substr(std::string_view("sqlite:").size());
		if (path.empty()) throw DbConfigError("sqlite: URL is missing a file path");
		return std::make_unique<SqliteConnection>(std::string(path));
	}
	if (url.starts_with("mysql://")) return std::make_unique<MysqlConnection>(parse_db_url(url));
	if (url.starts_with("postgres://") || url.starts_with("postgresql://"))
		throw DbConfigError("the PostgreSQL backend is not built yet (DATABASE_DECISION.md increment 2)");
	throw DbConfigError("unknown database URL scheme (expected sqlite:, mysql:// or postgres://)");
}

} // namespace rathena::persistence
