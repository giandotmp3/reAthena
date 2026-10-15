// Acceptance tests: BR-INTER-016 (inter-server secret from env, never a literal),
// the credential half of BR-LOGIN-025 / BR-CHAR-030 (server registration), and S1 (DB URLs).
//
// All credential values below are FAKE same-shape values. No legacy credential literal is used;
// the legacy shipped defaults are covered through their SHA-256 digests only.
#include <gtest/gtest.h>

#include <sstream>

#include "rathena/core/config.hpp"
#include "rathena/core/secrets.hpp"
#include "rathena/core/sha256.hpp"

using namespace rathena::core;

namespace {

constexpr const char* kFakeUser = "fake_inter_user";
constexpr const char* kFakePass = "fake-inter-pass-0123456789";
constexpr const char* kFakeDbUrl = "mysql://rathena:fake-db-pass-0000@db.invalid:3306/rathena_main";

MapEnvironment good_env() {
	MapEnvironment env;
	env.set("INTER_SERVER_USER", kFakeUser);
	env.set("INTER_SERVER_PASS", kFakePass);
	env.set("DATABASE_URL", kFakeDbUrl);
	return env;
}

SecretsErrc inter_error(const Environment& env, const CredentialPolicy& policy = {}) {
	try {
		(void)load_inter_server_credentials(env, policy);
	} catch (const SecretsError& e) {
		return e.code();
	}
	ADD_FAILURE() << "expected SecretsError";
	return SecretsErrc::Missing;
}

} // namespace

// ---------------------------------------------------------------- BR-INTER-016

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_LoadsCredentialFromEnvironment) {
	const auto env = good_env();
	const auto creds = load_inter_server_credentials(env);
	EXPECT_EQ(creds.user, kFakeUser);
	EXPECT_EQ(creds.pass.reveal(), kFakePass);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_RefusesStartupWhenUserUnset) {
	auto env = good_env();
	env.unset("INTER_SERVER_USER");
	EXPECT_EQ(inter_error(env), SecretsErrc::Missing);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_RefusesStartupWhenPassUnset) {
	auto env = good_env();
	env.unset("INTER_SERVER_PASS");
	EXPECT_EQ(inter_error(env), SecretsErrc::Missing);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_RefusesStartupWhenPassEmpty) {
	auto env = good_env();
	env.set("INTER_SERVER_PASS", "");
	EXPECT_EQ(inter_error(env), SecretsErrc::Missing);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_BuiltInDenylistCoversLegacyShippedDefaults) {
	// Three distinct legacy shipped credential values (inter-server userid/passwd and the DB
	// password), stored as digests only. Each must be a well-formed SHA-256 hex digest.
	const auto& list = CredentialPolicy::built_in_denylist();
	ASSERT_GE(list.size(), 3u);
	for (const auto& d : list) {
		EXPECT_EQ(d.size(), 64u);
		EXPECT_EQ(d.find_first_not_of("0123456789abcdef"), std::string::npos);
	}
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_RefusesKnownDefaultPassword) {
	// A fake "shipped default" injected into the denylist stands in for the legacy literal.
	const std::string fake_default = "fake-shipped-default-pass-0001";
	CredentialPolicy policy;
	policy.extra_denylist_sha256 = {sha256_hex(fake_default)};
	auto env = good_env();
	env.set("INTER_SERVER_PASS", fake_default);
	EXPECT_EQ(inter_error(env, policy), SecretsErrc::KnownDefault);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_RefusesKnownDefaultUser) {
	const std::string fake_default_user = "fake_default_user";
	CredentialPolicy policy;
	policy.extra_denylist_sha256 = {sha256_hex(fake_default_user)};
	auto env = good_env();
	env.set("INTER_SERVER_USER", fake_default_user);
	EXPECT_EQ(inter_error(env, policy), SecretsErrc::KnownDefault);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_BuiltInDenylistCannotBeRelaxed) {
	// Even a policy with every length check disabled still refuses a built-in denylisted value
	// (generic weak value "password"; not a legacy literal).
	CredentialPolicy lax;
	lax.min_user_length = 0;
	lax.min_pass_length = 0;
	auto env = good_env();
	env.set("INTER_SERVER_PASS", "password");
	EXPECT_EQ(inter_error(env, lax), SecretsErrc::KnownDefault);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_RefusesShortPassword) {
	auto env = good_env();
	env.set("INTER_SERVER_PASS", "fake-short");
	EXPECT_EQ(inter_error(env), SecretsErrc::TooWeak);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_RefusesPasswordEqualToUser) {
	auto env = good_env();
	env.set("INTER_SERVER_USER", "fake-same-value-123456");
	env.set("INTER_SERVER_PASS", "fake-same-value-123456");
	EXPECT_EQ(inter_error(env), SecretsErrc::TooWeak);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_ErrorNamesVariableNeverValue) {
	const std::string fake_default = "fake-shipped-default-pass-0002";
	CredentialPolicy policy;
	policy.extra_denylist_sha256 = {sha256_hex(fake_default)};
	auto env = good_env();
	env.set("INTER_SERVER_PASS", fake_default);
	try {
		(void)load_inter_server_credentials(env, policy);
		FAIL() << "expected SecretsError";
	} catch (const SecretsError& e) {
		EXPECT_EQ(e.subject(), "INTER_SERVER_PASS");
		EXPECT_EQ(std::string(e.what()).find(fake_default), std::string::npos);
	}
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_ConfigLiteralSecretIsRefused) {
	auto env = good_env();
	const ConfigLoader loader(env);
	const auto cfg = loader.load_file(RATHENA_CORE_FIXTURES_DIR "/literal_secret.conf");
	try {
		assert_no_literal_secrets(cfg);
		FAIL() << "expected SecretsError(LiteralInConfig)";
	} catch (const SecretsError& e) {
		EXPECT_EQ(e.code(), SecretsErrc::LiteralInConfig);
		EXPECT_EQ(e.subject(), "passwd");
		EXPECT_EQ(std::string(e.what()).find("fake-literal-not-a-real-secret"), std::string::npos);
	}
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_MixedLiteralAndPlaceholderIsRefused) {
	auto env = good_env();
	const ConfigLoader loader(env);
	const auto cfg = loader.load_string("passwd: prefix-${INTER_SERVER_PASS}\n", "inline.conf");
	EXPECT_THROW(assert_no_literal_secrets(cfg), SecretsError);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_ShippedConfHoldsPlaceholdersOnly) {
	auto env = good_env();
	for (const auto* v : {"LOGIN_DB_URL", "CHAR_DB_URL", "MAP_DB_URL", "WEB_DB_URL", "LOG_DB_URL"}) {
		env.set(v, kFakeDbUrl);
	}
	const ConfigLoader loader(env);
	const auto cfg = loader.load_file(RATHENA_CORE_CONF_DIR "/inter_server.conf");
	EXPECT_NO_THROW(assert_no_literal_secrets(cfg));
	EXPECT_EQ(cfg.get("passwd").value(), kFakePass);
}

TEST(BR_INTER_016_InterServerSecret, BR_INTER_016_SecretKeyClassification) {
	for (const auto* k : {"userid", "passwd", "login_server_pw", "log_db_pw", "char_db_url", "DATABASE_URL",
	                      "challenge_secret", "web_auth_token"}) {
		EXPECT_TRUE(is_secret_key(k)) << k;
	}
	for (const auto* k : {"server_name", "web_auth_token_enabled", "login_ip", "char_maintenance"}) {
		EXPECT_FALSE(is_secret_key(k)) << k;
	}
}

// ------------------------------------------- BR-LOGIN-025 / BR-CHAR-030 (credential half)
// The sex-'S' / id < MAX_SERVERS / free-slot checks live in netproto + login-server and
// char-rules; core owns the credential comparison against the env-sourced secret.

TEST(BR_LOGIN_025_ServerRegistration, BR_LOGIN_025_CharServerHandshakeMatchesEnvCredential) {
	const auto creds = load_inter_server_credentials(good_env());
	EXPECT_TRUE(creds.matches(kFakeUser, kFakePass));
}

TEST(BR_LOGIN_025_ServerRegistration, BR_LOGIN_025_WrongPasswordOrUserRejected) {
	const auto creds = load_inter_server_credentials(good_env());
	EXPECT_FALSE(creds.matches(kFakeUser, "fake-inter-pass-0123456788"));
	EXPECT_FALSE(creds.matches("fake_inter_usex", kFakePass));
	EXPECT_FALSE(creds.matches(kFakeUser, ""));
	EXPECT_FALSE(creds.matches("", ""));
	EXPECT_FALSE(creds.matches(kFakeUser, std::string(kFakePass) + "x")); // prefix is not a match
}

TEST(BR_CHAR_030_MapServerRegistration, BR_CHAR_030_MapServerHandshakeUsesSameEnvCredential) {
	const auto creds = load_inter_server_credentials(good_env());
	EXPECT_TRUE(creds.matches(kFakeUser, kFakePass));
	EXPECT_FALSE(creds.matches(kFakeUser, "fake-wrong-pass-00000000"));
}

// ------------------------------------------------------------------- S1 (DB URLs)

TEST(S1_DatabaseUrls, S1_EachUrlFallsBackToDATABASE_URL) {
	const auto urls = load_database_urls(good_env());
	for (const auto* u : {&urls.login, &urls.char_db, &urls.map, &urls.web, &urls.log}) {
		EXPECT_EQ(u->source_var, "DATABASE_URL");
		EXPECT_EQ(u->host, "db.invalid");
		EXPECT_EQ(u->port, 3306);
		EXPECT_EQ(u->database, "rathena_main");
	}
}

TEST(S1_DatabaseUrls, S1_SpecificUrlOverridesFallback) {
	auto env = good_env();
	env.set("LOG_DB_URL", "mysql://logger:fake-log-pass-0000@logdb.invalid:3307/ragnarok_log");
	const auto urls = load_database_urls(env);
	EXPECT_EQ(urls.log.source_var, "LOG_DB_URL");
	EXPECT_EQ(urls.log.host, "logdb.invalid");
	EXPECT_EQ(urls.log.port, 3307);
	EXPECT_EQ(urls.char_db.source_var, "DATABASE_URL");
}

TEST(S1_DatabaseUrls, S1_MissingUrlRefusesStartup) {
	MapEnvironment env;
	try {
		(void)load_database_urls(env);
		FAIL() << "expected SecretsError";
	} catch (const SecretsError& e) {
		EXPECT_EQ(e.code(), SecretsErrc::Missing);
	}
}

TEST(S1_DatabaseUrls, S1_MalformedUrlRefused) {
	for (const auto* bad : {"rathena_main", "mysql://host/db", "mysql://user@host/db", "mysql://u:p@host:0/db",
	                        "mysql://u:p@host:3306/", "postgres://u:fake-db-pass-0000@h/db"}) {
		auto env = good_env();
		env.set("DATABASE_URL", bad);
		try {
			(void)load_database_urls(env);
			ADD_FAILURE() << "accepted malformed url";
		} catch (const SecretsError& e) {
			EXPECT_EQ(e.code(), SecretsErrc::MalformedUrl);
		}
	}
}

TEST(S1_DatabaseUrls, S1_KnownDefaultDbPasswordRefused) {
	const std::string fake_default = "fake-shipped-db-pw";
	CredentialPolicy policy;
	policy.extra_denylist_sha256 = {sha256_hex(fake_default)};
	auto env = good_env();
	env.set("DATABASE_URL", "mysql://rathena:" + fake_default + "@db.invalid/rathena_main");
	try {
		(void)load_database_urls(env, policy);
		FAIL() << "expected SecretsError";
	} catch (const SecretsError& e) {
		EXPECT_EQ(e.code(), SecretsErrc::KnownDefault);
	}
}

TEST(S1_DatabaseUrls, S1_RedactedUrlAndSecretStreamNeverShowPassword) {
	const auto urls = load_database_urls(good_env());
	EXPECT_EQ(urls.map.redacted(), "mysql://rathena:***@db.invalid:3306/rathena_main");
	std::ostringstream os;
	os << urls.map.password;
	EXPECT_EQ(os.str(), "***");
}
