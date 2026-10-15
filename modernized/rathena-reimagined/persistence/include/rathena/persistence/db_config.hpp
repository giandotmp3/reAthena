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

/// Parsed `mysql://user:password@host[:port]/database`.
struct DbEndpoint {
	std::string user;
	std::string password; ///< never printed; see redacted()
	std::string host;
	std::uint16_t port = 3306;
	std::string database;

	/// `mysql://user:***@host:port/database` -- safe for logs.
	[[nodiscard]] std::string redacted() const;
};

/// Throws DbConfigError on a malformed URL. The error text never contains the password.
DbEndpoint parse_db_url(std::string_view url);

enum class ServerProcess { Login, Char, Map, Web, Migrate };

struct DbConnectionConfig {
	DbEndpoint main;
	DbEndpoint log; ///< may be a separate DB (logs.sql tables are excluded from M2)

	/// Throws DbConfigError if DATABASE_URL (or the process override) is unset/empty/malformed.
	static DbConnectionConfig from_environment(const Environment& env,
	                                           ServerProcess process = ServerProcess::Migrate);
};

} // namespace rathena::persistence
