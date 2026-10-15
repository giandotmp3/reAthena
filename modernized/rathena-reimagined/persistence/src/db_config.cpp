#include "rathena/persistence/db_config.hpp"

#include <array>
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

namespace {

constexpr std::string_view kMysqlScheme = "mysql://";
constexpr std::string_view kPostgresSchemes[] = {"postgresql://", "postgres://"};
constexpr std::array<std::string_view, 5> kPostgresParams = {"sslmode", "sslrootcert", "application_name",
                                                             "connect_timeout", "options"};

int hex_value(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/// RFC 3986 percent-decoding; a '%' not followed by two hex digits is kept as is.
std::string percent_decode(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (std::size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '%' && i + 2 < s.size()) {
			const int hi = hex_value(s[i + 1]);
			const int lo = hex_value(s[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>(hi * 16 + lo));
				i += 2;
				continue;
			}
		}
		out.push_back(s[i]);
	}
	return out;
}

} // namespace

bool is_mysql_url(std::string_view url) noexcept { return url.starts_with(kMysqlScheme); }

bool is_postgres_url(std::string_view url) noexcept {
	for (const auto p : kPostgresSchemes)
		if (url.starts_with(p)) return true;
	return false;
}

std::string DbEndpoint::redacted() const {
	return std::string(scheme == DbScheme::MySql ? "mysql://" : "postgresql://") + user + ":***@" + host + ":" +
	       std::to_string(port) + "/" + database;
}

DbEndpoint parse_db_url(std::string_view url) {
	DbEndpoint ep;
	std::string_view rest;
	if (is_mysql_url(url)) {
		ep.scheme = DbScheme::MySql;
		ep.port = 3306;
		rest = url.substr(kMysqlScheme.size());
	} else if (is_postgres_url(url)) {
		ep.scheme = DbScheme::Postgres;
		ep.port = 5432;
		rest = url.substr(url.find("://") + 3);
	} else {
		throw DbConfigError("database URL must start with postgresql://, postgres:// or mysql://");
	}

	// The password may contain '@' or ':'; split userinfo on the LAST '@'.
	const auto at = rest.rfind('@');
	if (at == std::string_view::npos) throw DbConfigError("database URL is missing user credentials");
	const std::string_view userinfo = rest.substr(0, at);
	rest = rest.substr(at + 1);
	const auto colon = userinfo.find(':');
	ep.user = percent_decode(userinfo.substr(0, colon));
	if (colon != std::string_view::npos) ep.password = percent_decode(userinfo.substr(colon + 1));
	if (ep.user.empty()) throw DbConfigError("database URL has an empty user");

	const auto slash = rest.find('/');
	if (slash == std::string_view::npos) throw DbConfigError("database URL is missing /database");
	std::string_view hostport = rest.substr(0, slash);
	std::string_view dbq = rest.substr(slash + 1);
	if (const auto qm = dbq.find('?'); qm != std::string_view::npos) {
		std::string_view query = dbq.substr(qm + 1);
		dbq = dbq.substr(0, qm);
		if (ep.scheme == DbScheme::MySql && !query.empty()) throw DbConfigError("mysql:// URLs take no parameters");
		while (!query.empty()) {
			const auto amp = query.find('&');
			const std::string_view kv = query.substr(0, amp);
			query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
			if (kv.empty()) continue;
			const auto eq = kv.find('=');
			const std::string key(kv.substr(0, eq));
			bool known = false;
			for (const auto k : kPostgresParams) known = known || k == key;
			if (!known || eq == std::string_view::npos) throw DbConfigError("database URL has an unsupported parameter");
			ep.params[key] = percent_decode(kv.substr(eq + 1));
		}
	}
	ep.database = percent_decode(dbq);
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

DbEndpoint endpoint_of(const std::string& url) {
	if (is_mysql_url(url)) throw DbConfigError(std::string(kMysqlNotSupported));
	if (is_postgres_url(url)) return parse_db_url(url);
	return {}; // sqlite: carries no credentials; open_connection() validates it
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
	cfg.main = endpoint_of(cfg.main_url);
	cfg.log = endpoint_of(cfg.log_url);
	return cfg;
}

} // namespace rathena::persistence
