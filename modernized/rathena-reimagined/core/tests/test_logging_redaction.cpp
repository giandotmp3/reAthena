// Acceptance tests: BR-WEB-017 [DEFECT fixed] (failed auth never logs the raw token) and
// S2 (tokens and passwords are never logged). The logger is the single choke point, so
// every unit that logs through rathena-core inherits this guarantee.
#include <gtest/gtest.h>

#include <memory>

#include "rathena/core/logging.hpp"

using namespace rathena::core;

namespace {

constexpr const char* kFakeToken = "FAKETOKEN0123456"; // 16 chars, same shape as a web token
constexpr const char* kFakeInterPass = "fake-inter-pass-0123456789";

struct Harness {
	std::shared_ptr<MemorySink> sink = std::make_shared<MemorySink>();
	std::shared_ptr<Redactor> redactor = std::make_shared<Redactor>();
	Logger logger{sink, redactor, LogLevel::Debug};

	std::string last() const {
		auto r = sink->records();
		return r.empty() ? std::string{} : r.back().message;
	}
};

bool contains(const std::string& hay, std::string_view needle) { return hay.find(needle) != std::string::npos; }

} // namespace

// ------------------------------------------------------------------- BR-WEB-017

TEST(BR_WEB_017_TokenNeverLogged, BR_WEB_017_FailedAuthFormFieldTokenMasked) {
	Harness h;
	// Shape of the legacy auth.cpp failure log line, with a fake token.
	h.logger.warn("web", std::string("Invalid auth: AID=2000001 AuthToken=") + kFakeToken);
	const auto msg = h.last();
	EXPECT_FALSE(contains(msg, kFakeToken)) << msg;
	EXPECT_TRUE(contains(msg, "AID=2000001")) << "non-secret context must survive: " << msg;
	EXPECT_TRUE(contains(msg, "AuthToken=***")) << msg;
}

TEST(BR_WEB_017_TokenNeverLogged, BR_WEB_017_UrlEncodedBodyTokenMasked) {
	Harness h;
	h.logger.warn("web", std::string("body: AID=2000001&AuthToken=") + kFakeToken + "&GDID=7");
	const auto msg = h.last();
	EXPECT_FALSE(contains(msg, kFakeToken)) << msg;
	EXPECT_TRUE(contains(msg, "&GDID=7")) << msg;
}

TEST(BR_WEB_017_TokenNeverLogged, BR_WEB_017_JsonTokenMasked) {
	Harness h;
	h.logger.error("web", std::string(R"({"AID":2000001,"AuthToken":")") + kFakeToken + R"("})");
	EXPECT_FALSE(contains(h.last(), kFakeToken)) << h.last();
}

TEST(BR_WEB_017_TokenNeverLogged, BR_WEB_017_GuildAuthFailureTokenMasked) {
	Harness h;
	h.logger.warn("web", std::string("guild auth failed GDID=7 web_auth_token: ") + kFakeToken);
	EXPECT_FALSE(contains(h.last(), kFakeToken)) << h.last();
}

TEST(BR_WEB_017_TokenNeverLogged, BR_WEB_017_RedactionAppliesAtEveryLevel) {
	Harness h;
	for (auto lvl : {LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error}) {
		h.logger.log(lvl, "web", std::string("token=") + kFakeToken);
	}
	for (const auto& r : h.sink->records()) {
		EXPECT_FALSE(contains(r.message, kFakeToken)) << r.message;
	}
	EXPECT_EQ(h.sink->records().size(), 4u);
}

// ------------------------------------------------------------------------- S2

TEST(S2_LogRedaction, S2_PasswordKeysMasked) {
	Harness h;
	h.logger.info("login", "password=fake-pw-1 passwd: fake-pw-2 user_pass=fake-pw-3 log_db_pw: fake-pw-4 pincode=1357");
	const auto msg = h.last();
	for (const auto* v : {"fake-pw-1", "fake-pw-2", "fake-pw-3", "fake-pw-4", "1357"}) {
		EXPECT_FALSE(contains(msg, v)) << v << " leaked: " << msg;
	}
}

TEST(S2_LogRedaction, S2_SessionTicketIdsMasked) {
	Harness h;
	h.logger.info("login", "auth ok aid=2000001 login_id1=123456789 login_id2=987654321");
	const auto msg = h.last();
	EXPECT_FALSE(contains(msg, "123456789")) << msg;
	EXPECT_FALSE(contains(msg, "987654321")) << msg;
	EXPECT_TRUE(contains(msg, "aid=2000001")) << msg;
}

TEST(S2_LogRedaction, S2_UrlCredentialsMasked) {
	Harness h;
	h.logger.error("sql", "connect failed: mysql://rathena:fake-db-pass-0000@db.invalid:3306/rathena_main");
	const auto msg = h.last();
	EXPECT_FALSE(contains(msg, "fake-db-pass-0000")) << msg;
	EXPECT_TRUE(contains(msg, "mysql://rathena:***@db.invalid")) << msg;
}

TEST(S2_LogRedaction, S2_RegisteredSecretMaskedAnywhereInText) {
	Harness h;
	h.redactor->register_secret(kFakeInterPass);
	h.logger.warn("inter", std::string("handshake 0x2710 bad credential '") + kFakeInterPass + "' from 10.0.0.5");
	const auto msg = h.last();
	EXPECT_FALSE(contains(msg, kFakeInterPass)) << msg;
	EXPECT_TRUE(contains(msg, "10.0.0.5")) << msg;
}

TEST(S2_LogRedaction, S2_StartupSecretsRegistered) {
	Harness h;
	InterServerCredentials inter{"fake_inter_user", Secret(kFakeInterPass)};
	DatabaseUrls db;
	db.login = db.char_db = db.map = db.web = parse_database_url("mysql://u:fake-db-pass-0000@h/d", "DATABASE_URL");
	db.log = parse_database_url("mysql://u:fake-log-pass-0000@h/d", "LOG_DB_URL");
	h.redactor->register_startup_secrets(inter, db);
	h.logger.info("x", std::string(kFakeInterPass) + " fake-db-pass-0000 fake-log-pass-0000");
	EXPECT_EQ(h.last(), "*** *** ***");
}

TEST(S2_LogRedaction, S2_TooShortRegisteredSecretIgnored) {
	Harness h;
	h.redactor->register_secret("ab");
	h.logger.info("x", "about abc");
	EXPECT_EQ(h.last(), "about abc");
}

TEST(S2_LogRedaction, S2_LoggerWithoutExplicitRedactorStillRedacts) {
	auto sink = std::make_shared<MemorySink>();
	Logger logger(sink, nullptr);
	logger.info("web", std::string("AuthToken=") + kFakeToken);
	ASSERT_EQ(sink->records().size(), 1u);
	EXPECT_FALSE(contains(sink->records()[0].message, kFakeToken));
}

TEST(S2_LogRedaction, S2_NonSecretFieldsPreserved) {
	Harness h;
	h.logger.info("login", "userid=bob123 ip=10.0.0.5 result=0 ping=20");
	EXPECT_EQ(h.last(), "userid=bob123 ip=10.0.0.5 result=0 ping=20");
}

TEST(S2_LogRedaction, S2_LevelFilterDropsBelowMinimum) {
	auto sink = std::make_shared<MemorySink>();
	Logger logger(sink, std::make_shared<Redactor>(), LogLevel::Warn);
	logger.info("x", "dropped");
	logger.warn("x", "kept");
	ASSERT_EQ(sink->records().size(), 1u);
	EXPECT_EQ(sink->records()[0].message, "kept");
}
