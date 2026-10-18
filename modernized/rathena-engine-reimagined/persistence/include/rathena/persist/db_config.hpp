#pragma once
// Connection configuration from the ENVIRONMENT ONLY (${DATABASE_URL}). No default URL exists.
#include <string>
#include <string_view>

namespace rathena::persist {

enum class Backend { Sqlite, Postgres };

struct DbConfig {
	std::string url; ///< may contain a password: never log it, use redact_url()
	Backend backend{Backend::Sqlite};
};

/// Classifies a URL. Throws DbConfigError for mysql/mariadb, unknown or empty schemes. The error
/// text never contains the URL.
DbConfig parse_db_url(std::string_view url);

/// Reads the named environment variable (default DATABASE_URL). Throws DbConfigError if it is
/// unset or empty. There is deliberately no fallback value.
DbConfig load_db_config_from_env(const char* var_name = "DATABASE_URL");

/// scheme://user:***@host/db -- safe for logs.
std::string redact_url(std::string_view url);

} // namespace rathena::persist
