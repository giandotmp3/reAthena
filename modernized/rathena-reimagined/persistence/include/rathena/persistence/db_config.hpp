#pragma once
// Database connection config. Read from the environment ONLY (S1): ${DATABASE_URL} (required)
// and ${LOG_DB_URL} (defaults to DATABASE_URL). Per-process overrides from spec §3.5
// (${LOGIN_DB_URL}, ${CHAR_DB_URL}, ${MAP_DB_URL}, ${WEB_DB_URL}) also default to DATABASE_URL.
// There is no config-file or compiled-in default for any credential.
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rathena::persistence {

class DbConfigError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

/// Minimal environment abstraction (tests inject a map; production reads getenv).
class Environment {
public:
	virtual ~Environment() = default;
	[[nodiscard]] virtual std::optional<std::string> get(std::string_view name) const = 0;
};

class ProcessEnvironment final : public Environment {
public:
	[[nodiscard]] std::optional<std::string> get(std::string_view name) const override;
};

class MapEnvironment final : public Environment {
public:
	void set(std::string name, std::string value) { vars_[std::move(name)] = std::move(value); }
	void unset(const std::string& name) { vars_.erase(name); }
	[[nodiscard]] std::optional<std::string> get(std::string_view name) const override;

private:
	std::map<std::string, std::string, std::less<>> vars_;
};

/// Network database URL schemes. MySQL is only ever a *source* for `rathena-migrate import`.
enum class DbScheme { Postgres, MySql };

/// Parsed `postgresql://user:password@host[:port]/database[?param=value&...]` (also
/// `postgres://`), or `mysql://...` for the importer's source. Percent-escapes (%40 = '@') are
/// decoded in user, password and database. Default ports: 5432 (PostgreSQL), 3306 (MySQL).
struct DbEndpoint {
	DbScheme scheme = DbScheme::Postgres;
	std::string user;
	std::string password; ///< never printed; see redacted()
	std::string host;
	std::uint16_t port = 5432;
	std::string database;
	/// Connection parameters from the query string (PostgreSQL: sslmode, sslrootcert,
	/// application_name, connect_timeout, options); anything else is rejected.
	std::map<std::string, std::string, std::less<>> params;

	/// `postgresql://user:***@host:port/database` -- safe for logs.
	[[nodiscard]] std::string redacted() const;
};

/// Throws DbConfigError on a malformed URL. The error text never contains the password.
DbEndpoint parse_db_url(std::string_view url);

/// Error text for a mysql:// URL anywhere a runtime database is expected.
inline constexpr std::string_view kMysqlNotSupported =
    "MySQL/MariaDB is no longer supported as a database backend; use PostgreSQL (postgresql://...) or "
    "SQLite (sqlite:...). Move an existing MySQL database once with: rathena-migrate import "
    "--from mysql://user:pass@host:3306/db --to <postgresql://...|sqlite:path>";

/// True for mysql:// URLs (no longer a runtime backend).
bool is_mysql_url(std::string_view url) noexcept;
/// True for postgres:// and postgresql:// URLs.
bool is_postgres_url(std::string_view url) noexcept;

enum class ServerProcess { Login, Char, Map, Web, Migrate };

struct DbConnectionConfig {
	std::string main_url; ///< as given; open with open_connection() (any backend)
	std::string log_url;  ///< LOG_DB_URL, or main_url when unset
	DbEndpoint main;      ///< parsed for postgres:// URLs; empty for sqlite: URLs
	DbEndpoint log;       ///< may be a separate database

	/// Throws DbConfigError if DATABASE_URL (or the process override) is unset, empty, malformed
	/// or a mysql:// URL (MySQL is no longer supported; `rathena-migrate import` moves the data).
	static DbConnectionConfig from_environment(const Environment& env,
	                                           ServerProcess process = ServerProcess::Migrate);
};

} // namespace rathena::persistence
