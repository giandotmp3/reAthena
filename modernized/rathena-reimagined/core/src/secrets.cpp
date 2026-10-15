#include "rathena/core/secrets.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ostream>

#include "rathena/core/sha256.hpp"

namespace rathena::core {

namespace {

bool ct_equal(std::string_view a, std::string_view b) noexcept {
	// Compare over the longer length so timing depends on max(len), not on the first mismatch.
	const std::size_t n = std::max(a.size(), b.size());
	unsigned diff = static_cast<unsigned>(a.size() ^ b.size());
	for (std::size_t i = 0; i < n; ++i) {
		const auto x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0u;
		const auto y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0u;
		diff |= x ^ y;
	}
	return diff == 0;
}

std::string lower(std::string_view s) {
	std::string out(s);
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

std::string require_env(const Environment& env, std::string_view name) {
	auto v = env.get(name);
	if (!v || v->empty()) {
		throw SecretsError(SecretsErrc::Missing, std::string(name),
		                   "required secret ${" + std::string(name) + "} is not set; refusing to start");
	}
	return *v;
}

void refuse_if_denylisted(const CredentialPolicy& policy, std::string_view value, std::string_view name) {
	if (policy.is_denylisted(value)) {
		throw SecretsError(SecretsErrc::KnownDefault, std::string(name),
		                   "${" + std::string(name) + "} is a known shipped default credential; refusing to start");
	}
}

void refuse_if_short(std::string_view value, std::size_t min, std::string_view name) {
	if (value.size() < min) {
		throw SecretsError(SecretsErrc::TooWeak, std::string(name),
		                   "${" + std::string(name) + "} is shorter than " + std::to_string(min) + " characters; refusing to start");
	}
}

} // namespace

bool Secret::equals(std::string_view candidate) const noexcept { return ct_equal(value_, candidate); }

std::ostream& operator<<(std::ostream& os, const Secret&) { return os << Secret::redacted(); }

const std::vector<std::string>& CredentialPolicy::built_in_denylist() {
	static const std::vector<std::string> list = {
		// Legacy shipped inter-server / DB credential values (conf/char_athena.conf, conf/map_athena.conf,
		// conf/inter_athena.conf @ d4b8e7b). Digests only; the literals are intentionally absent.
		"f64551fcd6f07823cb87971cfb91446425da18286b3ab1ef935e0cbd7a69f68a",
		"e8bc163c82eee18733288c7d4ac636db3a6deb013ef2d37b68322be20edc45cc",
		"ac3160b0a933ac03d7fb269baf8443e65936aa4322881e30c60443d7dda152d5",
		// Generic well-known weak values.
		"5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8",
		"057ba03d6c44104863dc7361fe4578965d1887360f90a0895882e58a6248fc86",
		"8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918",
		"4813494d137e1631bba301d5acab6e7bb7aa74ce1185d456565ef51d737677b2",
		"8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92",
	};
	return list;
}

bool CredentialPolicy::is_denylisted(std::string_view value) const {
	const auto digest = sha256_hex(value);
	const auto& builtin = built_in_denylist();
	return std::find(builtin.begin(), builtin.end(), digest) != builtin.end() ||
	       std::find(extra_denylist_sha256.begin(), extra_denylist_sha256.end(), digest) != extra_denylist_sha256.end();
}

bool InterServerCredentials::matches(std::string_view user_candidate, std::string_view pass_candidate) const noexcept {
	// Evaluate both comparisons unconditionally (no short-circuit on the user name).
	const bool u = ct_equal(user, user_candidate);
	const bool p = pass.equals(pass_candidate);
	return u & p;
}

std::string DatabaseUrl::redacted() const {
	return scheme + "://" + user + ":" + std::string(Secret::redacted()) + "@" + host + ":" + std::to_string(port) + "/" + database;
}

DatabaseUrl parse_database_url(std::string_view url, std::string_view source_var) {
	const auto fail = [&](const char* why) {
		return SecretsError(SecretsErrc::MalformedUrl, std::string(source_var),
		                    "${" + std::string(source_var) + "} is not a valid database URL (" + why + ")");
	};
	DatabaseUrl out;
	out.source_var = std::string(source_var);

	const auto sep = url.find("://");
	if (sep == std::string_view::npos) {
		throw fail("missing scheme");
	}
	out.scheme = lower(url.substr(0, sep));
	if (out.scheme != "mysql" && out.scheme != "mariadb") {
		throw fail("scheme must be mysql or mariadb");
	}
	auto rest = url.substr(sep + 3);
	const auto at = rest.rfind('@');
	if (at == std::string_view::npos) {
		throw fail("missing credentials");
	}
	const auto cred = rest.substr(0, at);
	rest = rest.substr(at + 1);
	const auto colon = cred.find(':');
	if (colon == std::string_view::npos) {
		throw fail("missing password");
	}
	out.user = std::string(cred.substr(0, colon));
	out.password = Secret(std::string(cred.substr(colon + 1)));
	if (out.user.empty()) {
		throw fail("empty user");
	}

	const auto slash = rest.find('/');
	if (slash == std::string_view::npos || slash + 1 >= rest.size()) {
		throw fail("missing database name");
	}
	out.database = std::string(rest.substr(slash + 1));
	auto hostport = rest.substr(0, slash);
	if (const auto pc = hostport.rfind(':'); pc != std::string_view::npos) {
		const auto ps = hostport.substr(pc + 1);
		unsigned port = 0;
		const auto [p, ec] = std::from_chars(ps.data(), ps.data() + ps.size(), port);
		if (ec != std::errc{} || p != ps.data() + ps.size() || port == 0 || port > 65535) {
			throw fail("bad port");
		}
		out.port = static_cast<std::uint16_t>(port);
		hostport = hostport.substr(0, pc);
	}
	if (hostport.empty()) {
		throw fail("empty host");
	}
	out.host = std::string(hostport);
	return out;
}

InterServerCredentials load_inter_server_credentials(const Environment& env, const CredentialPolicy& policy) {
	using namespace env_names;
	InterServerCredentials c;
	c.user = require_env(env, kInterServerUser);
	c.pass = Secret(require_env(env, kInterServerPass));

	refuse_if_denylisted(policy, c.user, kInterServerUser);
	refuse_if_denylisted(policy, c.pass.reveal(), kInterServerPass);
	refuse_if_short(c.user, policy.min_user_length, kInterServerUser);
	refuse_if_short(c.pass.reveal(), policy.min_pass_length, kInterServerPass);
	if (c.pass.equals(c.user)) {
		throw SecretsError(SecretsErrc::TooWeak, std::string(kInterServerPass),
		                   "${INTER_SERVER_PASS} must differ from ${INTER_SERVER_USER}; refusing to start");
	}
	return c;
}

DatabaseUrls load_database_urls(const Environment& env, const CredentialPolicy& policy) {
	using namespace env_names;
	const auto load_one = [&](std::string_view specific) {
		std::string_view source = specific;
		auto v = env.get(specific);
		if (!v || v->empty()) {
			source = kDatabaseUrl;
			v = env.get(kDatabaseUrl);
		}
		if (!v || v->empty()) {
			throw SecretsError(SecretsErrc::Missing, std::string(specific),
			                   "neither ${" + std::string(specific) + "} nor ${DATABASE_URL} is set; refusing to start");
		}
		auto url = parse_database_url(*v, source);
		refuse_if_denylisted(policy, url.password.reveal(), source);
		refuse_if_short(url.password.reveal(), policy.min_db_pass_length, source);
		return url;
	};
	DatabaseUrls urls;
	urls.login = load_one(kLoginDbUrl);
	urls.char_db = load_one(kCharDbUrl);
	urls.map = load_one(kMapDbUrl);
	urls.web = load_one(kWebDbUrl);
	urls.log = load_one(kLogDbUrl);
	return urls;
}

bool is_secret_key(std::string_view key) {
	const auto k = lower(key);
	const auto ends_with = [&](std::string_view s) { return k.size() >= s.size() && k.compare(k.size() - s.size(), s.size(), s) == 0; };
	return k == "userid" || k == "passwd" || k == "password" || k == "secret" || k == "token" ||
	       ends_with("_pw") || ends_with("_pass") || ends_with("_passwd") || ends_with("_password") ||
	       ends_with("_db_url") || k == "database_url" || ends_with("_secret") || ends_with("_token");
}

namespace {
// Exactly one placeholder and nothing else: "${NAME}".
bool is_pure_placeholder(std::string_view raw) {
	return raw.size() > 3 && raw.substr(0, 2) == "${" && raw.back() == '}' &&
	       raw.find('}') == raw.size() - 1 && raw.find("${", 2) == std::string_view::npos;
}
} // namespace

void assert_no_literal_secrets(const Config& config) {
	for (const auto& e : config.entries()) {
		if (is_secret_key(e.key) && !is_pure_placeholder(e.raw)) {
			throw SecretsError(SecretsErrc::LiteralInConfig, e.key,
			                   "config key '" + e.key + "' (" + e.file.generic_string() + ":" + std::to_string(e.line) +
			                       ") holds a literal secret; use a ${VAR} placeholder");
		}
	}
}

} // namespace rathena::core
