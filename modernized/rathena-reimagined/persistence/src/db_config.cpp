#include "rathena/persistence/db_config.hpp"

#include <cstdlib>

namespace rathena::persistence {

std::optional<std::string> ProcessEnvironment::get(std::string_view name) const {
	const std::string key(name);
	if (const char* v = std::getenv(key.c_str())) return std::string(v);
	return std::nullopt;
}

std::optional<std::string> MapEnvironment::get(std::string_view name) const {
	if (const auto it = vars_.find(name); it != vars_.end()) return it->second;
	return std::nullopt;
}

std::string DbEndpoint::redacted() const {
	return "mysql://" + user + ":***@" + host + ":" + std::to_string(port) + "/" + database;
}

DbEndpoint parse_db_url(std::string_view url) {
	constexpr std::string_view kScheme = "mysql://";
	if (url.substr(0, kScheme.size()) != kScheme) throw DbConfigError("database URL must start with mysql://");
	std::string_view rest = url.substr(kScheme.size());

	DbEndpoint ep;
	// The password may contain '@' or ':'; split userinfo on the LAST '@'.
	const auto at = rest.rfind('@');
	if (at == std::string_view::npos) throw DbConfigError("database URL is missing user credentials");
	const std::string_view userinfo = rest.substr(0, at);
	rest = rest.substr(at + 1);
	const auto colon = userinfo.find(':');
	ep.user = std::string(userinfo.substr(0, colon));
	if (colon != std::string_view::npos) ep.password = std::string(userinfo.substr(colon + 1));
	if (ep.user.empty()) throw DbConfigError("database URL has an empty user");

	const auto slash = rest.find('/');
	if (slash == std::string_view::npos) throw DbConfigError("database URL is missing /database");
	std::string_view hostport = rest.substr(0, slash);
	ep.database = std::string(rest.substr(slash + 1));
	if (ep.database.empty()) throw DbConfigError("database URL has an empty database name");

	const auto pc = hostport.rfind(':');
	if (pc != std::string_view::npos) {
		const std::string_view port = hostport.substr(pc + 1);
		hostport = hostport.substr(0, pc);
		if (port.empty() || port.size() > 5) throw DbConfigError("database URL has an invalid port");
		unsigned long p = 0;
		for (const char c : port) {
			if (c < '0' || c > '9') throw DbConfigError("database URL has an invalid port");
			p = p * 10 + static_cast<unsigned long>(c - '0');
		}
		if (p == 0 || p > 65535) throw DbConfigError("database URL has an invalid port");
		ep.port = static_cast<std::uint16_t>(p);
	}
	if (hostport.empty()) throw DbConfigError("database URL has an empty host");
	ep.host = std::string(hostport);
	return ep;
}

namespace {

std::string_view process_var(ServerProcess p) {
	switch (p) {
	case ServerProcess::Login: return "LOGIN_DB_URL";
	case ServerProcess::Char: return "CHAR_DB_URL";
	case ServerProcess::Map: return "MAP_DB_URL";
	case ServerProcess::Web: return "WEB_DB_URL";
	case ServerProcess::Migrate: return {};
	}
	return {};
}

std::optional<std::string> non_empty(const Environment& env, std::string_view name) {
	if (name.empty()) return std::nullopt;
	auto v = env.get(name);
	if (v && v->empty()) return std::nullopt;
	return v;
}

} // namespace

DbConnectionConfig DbConnectionConfig::from_environment(const Environment& env, ServerProcess process) {
	auto main = non_empty(env, process_var(process));
	if (!main) main = non_empty(env, "DATABASE_URL");
	if (!main) throw DbConfigError("DATABASE_URL is not set; database credentials are read from the environment only");
	DbConnectionConfig cfg;
	cfg.main_url = *main;
	const auto log = non_empty(env, "LOG_DB_URL");
	cfg.log_url = log ? *log : cfg.main_url;
	// sqlite:/postgres:// URLs carry no credentials to split out; open them with open_connection().
	auto is_mysql = [](const std::string& u) { return u.rfind("mysql://", 0) == 0; };
	if (is_mysql(cfg.main_url)) cfg.main = parse_db_url(cfg.main_url);
	if (is_mysql(cfg.log_url)) cfg.log = parse_db_url(cfg.log_url);
	return cfg;
}

} // namespace rathena::persistence
