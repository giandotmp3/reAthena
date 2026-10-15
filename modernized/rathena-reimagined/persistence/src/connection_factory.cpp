#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/postgres.hpp"
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
	if (is_postgres_url(url)) {
		auto endpoint = parse_db_url(url); // validate before reporting a missing client
		if (!postgres_client_available()) {
			PostgresConnection stub(endpoint);
			stub.connect(); // throws DbConfigError naming the build option
		}
		return std::make_unique<PostgresConnection>(std::move(endpoint));
	}
	if (is_mysql_url(url)) throw DbConfigError(std::string(kMysqlNotSupported));
	throw DbConfigError("unknown database URL scheme (expected sqlite: or postgresql://)");
}

} // namespace rathena::persistence
