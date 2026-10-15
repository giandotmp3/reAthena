// Secrets from the environment (S1, BR-INTER-016; credential half of BR-LOGIN-025 / BR-CHAR-030).
//
// Contract:
//   - The inter-server credential comes only from ${INTER_SERVER_USER} / ${INTER_SERVER_PASS}.
//   - DB connections come only from ${LOGIN_DB_URL}, ${CHAR_DB_URL}, ${MAP_DB_URL}, ${WEB_DB_URL},
//     ${LOG_DB_URL}, each falling back to ${DATABASE_URL}.
//   - Startup REFUSES (throws SecretsError) when a credential is missing, is a known shipped
//     default, or is too weak. There is no "allow defaults" switch.
//   - A config file that carries a literal value for a secret key is refused; secret keys
//     may only hold ${VAR} placeholders.
//   - Error messages name the variable or key, never the value.
#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/core/config.hpp"
#include "rathena/core/env.hpp"

namespace rathena::core {

/// Environment variable names (architecture §4, spec §3.5).
namespace env_names {
inline constexpr std::string_view kInterServerUser = "INTER_SERVER_USER";
inline constexpr std::string_view kInterServerPass = "INTER_SERVER_PASS";
inline constexpr std::string_view kDatabaseUrl = "DATABASE_URL";
inline constexpr std::string_view kLoginDbUrl = "LOGIN_DB_URL";
inline constexpr std::string_view kCharDbUrl = "CHAR_DB_URL";
inline constexpr std::string_view kMapDbUrl = "MAP_DB_URL";
inline constexpr std::string_view kWebDbUrl = "WEB_DB_URL";
inline constexpr std::string_view kLogDbUrl = "LOG_DB_URL";
} // namespace env_names

enum class SecretsErrc {
	Missing,         ///< variable unset or empty
	KnownDefault,    ///< value is on the shipped-default denylist
	TooWeak,         ///< fails the length / distinctness policy
	LiteralInConfig, ///< a config file holds a literal for a secret key
	MalformedUrl,    ///< DB URL is not mysql://user:pass@host[:port]/db
};

class SecretsError : public std::runtime_error {
public:
	SecretsError(SecretsErrc code, std::string subject, const std::string& what)
		: std::runtime_error(what), code_(code), subject_(std::move(subject)) {}
	SecretsErrc code() const noexcept { return code_; }
	/// Name of the offending variable / config key (never its value).
	const std::string& subject() const noexcept { return subject_; }

private:
	SecretsErrc code_;
	std::string subject_;
};

/// Opaque secret string. Streams and redacted() print "***"; reveal() is the only way out.
class Secret {
public:
	Secret() = default;
	explicit Secret(std::string value) : value_(std::move(value)) {}

	const std::string& reveal() const noexcept { return value_; }
	bool empty() const noexcept { return value_.empty(); }
	static constexpr std::string_view redacted() noexcept { return "***"; }

	/// Constant-time comparison (length leak only).
	bool equals(std::string_view candidate) const noexcept;

private:
	std::string value_;
};
std::ostream& operator<<(std::ostream& os, const Secret& s);

/// Policy applied at startup. Defaults are the secure defaults; tests may add
/// extra denylist digests but cannot remove the built-in ones.
struct CredentialPolicy {
	std::size_t min_user_length = 4;
	std::size_t min_pass_length = 16;
	std::size_t min_db_pass_length = 12;
	/// Extra SHA-256 hex digests to refuse in addition to built_in_denylist().
	std::vector<std::string> extra_denylist_sha256;

	/// SHA-256 digests of the credential values shipped in legacy conf/ (userid/passwd in
	/// char_athena.conf / map_athena.conf, *_id / *_pw in inter_athena.conf). Stored as
	/// digests so no legacy credential literal exists in this codebase.
	static const std::vector<std::string>& built_in_denylist();

	bool is_denylisted(std::string_view value) const;
};

struct InterServerCredentials {
	std::string user;
	Secret pass;

	/// Constant-time check used by the 0x2710 (login) and 0x2af8 (char) handshakes.
	bool matches(std::string_view user_candidate, std::string_view pass_candidate) const noexcept;
};

struct DatabaseUrl {
	std::string scheme;
	std::string user;
	Secret password;
	std::string host;
	std::uint16_t port = 3306;
	std::string database;
	std::string source_var; ///< which env var supplied it (e.g. "CHAR_DB_URL" or "DATABASE_URL")

	/// mysql://user:***@host:port/db
	std::string redacted() const;
};

struct DatabaseUrls {
	DatabaseUrl login, char_db, map, web, log;
};

/// Parses mysql://user:pass@host[:port]/database. Throws SecretsError(MalformedUrl).
DatabaseUrl parse_database_url(std::string_view url, std::string_view source_var);

/// BR-INTER-016: reads and validates the inter-server credential. Throws SecretsError.
InterServerCredentials load_inter_server_credentials(const Environment& env, const CredentialPolicy& policy = {});

/// S1: reads and validates the five DB URLs (each falls back to ${DATABASE_URL}). Throws SecretsError.
DatabaseUrls load_database_urls(const Environment& env, const CredentialPolicy& policy = {});

/// True for keys that may only carry ${VAR} placeholders in config files
/// (userid, passwd, *_pw, *_password, *_db_url, *secret*, *token*).
bool is_secret_key(std::string_view key);

/// S1: refuses any secret key whose value came from a literal rather than ${VAR}.
void assert_no_literal_secrets(const Config& config);

} // namespace rathena::core
