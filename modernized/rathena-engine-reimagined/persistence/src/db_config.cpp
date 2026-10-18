#include "rathena/persist/db_config.hpp"

#include <cstdlib>

#include "rathena/persist/errors.hpp"

namespace rathena::persist {
namespace {
bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
} // namespace

DbConfig parse_db_url(std::string_view url) {
	if (url.empty()) throw DbConfigError("database url is empty");
	if (starts_with(url, "mysql:") || starts_with(url, "mariadb:"))
		throw DbConfigError("MySQL/MariaDB is not a supported backend; use sqlite: or postgresql:// "
		                    "(import legacy data with the one-time migrate tool)");
	if (starts_with(url, "sqlite:")) return DbConfig{std::string(url), Backend::Sqlite};
	if (starts_with(url, "postgresql://") || starts_with(url, "postgres://"))
		return DbConfig{std::string(url), Backend::Postgres};
	throw DbConfigError("unsupported database url scheme");
}

DbConfig load_db_config_from_env(const char* var_name) {
	const char* v = std::getenv(var_name);
	if (v == nullptr || *v == '\0')
		throw DbConfigError(std::string("environment variable ") + var_name + " is not set");
	return parse_db_url(v);
}

std::string redact_url(std::string_view url) {
	const auto scheme_end = url.find("://");
	if (scheme_end == std::string_view::npos) return std::string(url); // sqlite:path has no secret
	const auto auth_start = scheme_end + 3;
	const auto at = url.find('@', auth_start);
	const auto slash = url.find('/', auth_start);
	if (at == std::string_view::npos || (slash != std::string_view::npos && slash < at))
		return std::string(url);
	const auto colon = url.find(':', auth_start);
	std::string out(url.substr(0, auth_start));
	if (colon != std::string_view::npos && colon < at) {
		out += url.substr(auth_start, colon - auth_start);
		out += ":***";
	} else {
		out += url.substr(auth_start, at - auth_start);
	}
	out += url.substr(at);
	return out;
}

} // namespace rathena::persist
