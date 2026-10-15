// BR-WEB-001, BR-WEB-002, BR-WEB-003, BR-WEB-017 acceptance tests.
#include <chrono>

#include "rathena/web/auth_rules.hpp"
#include "rathena/web/log.hpp"
#include "test_support.hpp"

namespace rathena::web::test {
namespace {

using namespace std::chrono_literals;

const Timestamp kNow{std::chrono::seconds{1'700'000'000}};

WebAuthTokenRecord token_row(bool enabled = true, std::optional<Timestamp> logged_out = std::nullopt) {
    return {kAidMaster, kTokenMaster, enabled, logged_out};
}

// ------------------------------------------------------------------ BR-WEB-001

TEST(BR_WEB_001_TokenAuth, AcceptsEnabledMatchingTokenForAid) {
    EXPECT_EQ(check_web_token(token_row(), kAidMaster, kTokenMaster, kNow), AuthResult::Ok);
}

TEST(BR_WEB_001_TokenAuth, RejectsWrongToken) {
    EXPECT_EQ(check_web_token(token_row(), kAidMaster, "FAKETOKENWRONG99", kNow), AuthResult::UnknownToken);
}

TEST(BR_WEB_001_TokenAuth, RejectsTokenPresentedForAnotherAid) {
    EXPECT_EQ(check_web_token(token_row(), kAidMember, kTokenMaster, kNow), AuthResult::UnknownToken);
}

TEST(BR_WEB_001_TokenAuth, RejectsUnknownAccount) {
    EXPECT_EQ(check_web_token(std::nullopt, kAidNoToken, kTokenMaster, kNow), AuthResult::UnknownToken);
}

TEST(BR_WEB_001_TokenAuth, RejectsDisabledToken) {
    EXPECT_EQ(check_web_token(token_row(false), kAidMaster, kTokenMaster, kNow), AuthResult::TokenDisabled);
}

TEST(BR_WEB_001_TokenAuth, RejectsAccountLoggedOutFor15Seconds) {
    // Spec example: logged out for 15 s -> rejected, even if the login server
    // has not (yet) flipped web_auth_token_enabled.
    EXPECT_EQ(check_web_token(token_row(true, kNow - 15s), kAidMaster, kTokenMaster, kNow),
              AuthResult::TokenExpired);
}

TEST(BR_WEB_001_TokenAuth, RejectsMalformedTokenShape) {
    EXPECT_EQ(check_web_token(token_row(), kAidMaster, "", kNow), AuthResult::MalformedCredentials);
    EXPECT_EQ(check_web_token(token_row(), kAidMaster, kTokenMaster + "X", kNow),
              AuthResult::MalformedCredentials);
}

TEST(BR_WEB_001_TokenAuth, ConstantTimeEqualsSemantics) {
    EXPECT_TRUE(constant_time_equals("FAKETOKENMASTR01", "FAKETOKENMASTR01"));
    EXPECT_FALSE(constant_time_equals("FAKETOKENMASTR01", "FAKETOKENMASTR02"));
    EXPECT_FALSE(constant_time_equals("FAKE", "FAKETOKEN"));
}

using BR_WEB_001_Handler = WebApiFixture;

TEST_F(BR_WEB_001_Handler, MissingAuthTokenIs400) {
    auto r = call(req("/userconfig/save", {{"AID", std::to_string(kAidMaster)}, {"WorldName", kWorld}}));
    EXPECT_EQ(r.status, kHttpBadRequest);
    EXPECT_EQ(r.body, "Error");
}

TEST_F(BR_WEB_001_Handler, MissingAidIs400) {
    auto r = call(req("/userconfig/save", {{"AuthToken", kTokenMaster}, {"WorldName", kWorld}}));
    EXPECT_EQ(r.status, kHttpBadRequest);
}

TEST_F(BR_WEB_001_Handler, NonNumericAidIs400NotACrash) {
    auto r = call(req("/userconfig/save", {{"AID", "12abc"}, {"AuthToken", kTokenMaster}, {"WorldName", kWorld}}));
    EXPECT_EQ(r.status, kHttpBadRequest);
}

TEST_F(BR_WEB_001_Handler, LoggedOut15sRejectedByEveryRoute) {
    store.put_token({kAidMaster, kTokenMaster, true, clock.now() - 15s});
    for (const auto& path : Router::paths()) {
        auto r = call(as_master(path, {{"GID", std::to_string(kGidMaster)},
                                       {"GDID", std::to_string(kGuild)},
                                       {"Type", "0"}}));
        EXPECT_EQ(r.status, kHttpBadRequest) << path;
    }
}

// ------------------------------------------------------------------ BR-WEB-002

TEST(BR_WEB_002_GuildMaster, MasterCharacterOfAccountIsAuthorized) {
    EXPECT_TRUE(is_guild_master(kAidMaster, GuildRecord{kGuild, kGidMaster}, CharacterRecord{kGidMaster, kAidMaster}));
}

TEST(BR_WEB_002_GuildMaster, OtherAccountIsNotAuthorized) {
    EXPECT_FALSE(is_guild_master(kAidMember, GuildRecord{kGuild, kGidMaster}, CharacterRecord{kGidMaster, kAidMaster}));
}

TEST(BR_WEB_002_GuildMaster, UnknownGuildOrCharacterIsNotAuthorized) {
    EXPECT_FALSE(is_guild_master(kAidMaster, std::nullopt, CharacterRecord{kGidMaster, kAidMaster}));
    EXPECT_FALSE(is_guild_master(kAidMaster, GuildRecord{kGuild, kGidMaster}, std::nullopt));
}

TEST(BR_WEB_002_GuildMaster, CharacterRowMustBeTheMasterCharacter) {
    // A mismatched character row (not guild.char_id) never authorizes.
    EXPECT_FALSE(is_guild_master(kAidMaster, GuildRecord{kGuild, kGidMaster}, CharacterRecord{kGidMasterAlt, kAidMaster}));
}

// ------------------------------------------------------------------ BR-WEB-003

TEST(BR_WEB_003_TokenWindow, ValidWhileLoggedIn) {
    EXPECT_EQ(check_web_token(token_row(true, std::nullopt), kAidMaster, kTokenMaster, kNow), AuthResult::Ok);
}

TEST(BR_WEB_003_TokenWindow, ValidWithinGraceAfterLogout) {
    EXPECT_EQ(check_web_token(token_row(true, kNow - 5s), kAidMaster, kTokenMaster, kNow), AuthResult::Ok);
    EXPECT_EQ(check_web_token(token_row(true, kNow - 9s), kAidMaster, kTokenMaster, kNow), AuthResult::Ok);
}

TEST(BR_WEB_003_TokenWindow, InvalidOnceGraceElapsed) {
    EXPECT_EQ(check_web_token(token_row(true, kNow - 10s), kAidMaster, kTokenMaster, kNow), AuthResult::TokenExpired);
}

TEST(BR_WEB_003_TokenWindow, LoginDisablingTheTokenEndsTheWindowImmediately) {
    EXPECT_EQ(check_web_token(token_row(false, kNow - 1s), kAidMaster, kTokenMaster, kNow), AuthResult::TokenDisabled);
}

using BR_WEB_003_Handler = WebApiFixture;

TEST_F(BR_WEB_003_Handler, ClockDrivenWindow) {
    store.put_token({kAidMaster, kTokenMaster, true, clock.now()});
    EXPECT_EQ(call(as_master("/userconfig/load")).status, kHttpOk);
    clock.advance(9s);
    EXPECT_EQ(call(as_master("/userconfig/load")).status, kHttpOk);
    clock.advance(1s);
    EXPECT_EQ(call(as_master("/userconfig/load")).status, kHttpBadRequest);
}

// ------------------------------------------------------------------ BR-WEB-017

TEST(BR_WEB_017_TokenNeverLogged, RedactsKeyValueForms) {
    const std::string t = "FAKETOKENLEAK123";
    for (const std::string msg : {"AuthToken=" + t, "authtoken: " + t, "{\"AuthToken\":\"" + t + "\"}",
                                  "token=" + t + "&AID=1", "web_auth_token = " + t}) {
        const std::string out = redact_secrets(msg);
        EXPECT_EQ(out.find(t), std::string::npos) << msg << " -> " << out;
        EXPECT_NE(out.find(kRedacted), std::string::npos) << out;
    }
}

TEST(BR_WEB_017_TokenNeverLogged, LeavesOrdinaryTextAlone) {
    EXPECT_EQ(redact_secrets("web auth rejected: path=/party/add aid=2000001 reason=unknown_token"),
              "web auth rejected: path=/party/add aid=2000001 reason=unknown_token");
}

using BR_WEB_017_Handler = WebApiFixture;

TEST_F(BR_WEB_017_Handler, FailedAuthLogsReasonButNeverTheToken) {
    const std::string presented = "FAKETOKENBADX999";
    auto r = call(req("/userconfig/save", {{"AID", std::to_string(kAidMaster)},
                                           {"AuthToken", presented},
                                           {"WorldName", kWorld}}));
    ASSERT_EQ(r.status, kHttpBadRequest);
    ASSERT_FALSE(log.records().empty());
    EXPECT_TRUE(log.contains("reason=unknown_token"));
    EXPECT_FALSE(log.contains(presented));
    EXPECT_FALSE(log.contains(kTokenMaster));
}

TEST_F(BR_WEB_017_Handler, FailedGuildMasterAuthNeverLogsTheToken) {
    auto r = call(as_member("/emblem/upload", {{"GDID", std::to_string(kGuild)},
                                               {"ImgType", "BMP"},
                                               {"Img", make_bmp24(24, 24, 0)}}));
    ASSERT_EQ(r.status, kHttpBadRequest);
    EXPECT_TRUE(log.contains("reason=not_guild_master"));
    EXPECT_FALSE(log.contains(kTokenMember));
}

TEST_F(BR_WEB_017_Handler, NoRouteEverLogsAToken) {
    for (const auto& path : Router::paths()) {
        call(req(path, {{"AID", std::to_string(kAidMember)}, {"AuthToken", "FAKETOKENBADX999"}}));
        call(as_member(path));
    }
    EXPECT_FALSE(log.contains("FAKETOKENBADX999"));
    EXPECT_FALSE(log.contains(kTokenMember));
}

}  // namespace
}  // namespace rathena::web::test
