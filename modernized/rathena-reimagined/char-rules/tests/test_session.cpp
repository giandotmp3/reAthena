// Acceptance tests: session handoff and save acceptance (CAP-04, CAP-07).
// BR-CHAR-023..032; BR-INTER-008, 009, 019.
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "rathena/char_rules/session.hpp"

using namespace rathena::char_rules;

namespace {
constexpr UnixTime kNow = 1'760'000'000;

AuthTicket ticket(AccountId aid = 2'000'001, CharId cid = 150'001) {
	return AuthTicket{aid, cid, 0x11111111u, 0x22222222u, Sex::Male, kNow};
}

// Fake same-shape inter-server credentials. Real values come from ${INTER_SERVER_USER} /
// ${INTER_SERVER_PASS} at runtime; never a literal from legacy conf.
InterServerCredentials fake_expected() {
	const char* u = std::getenv("RATHENA_TEST_INTER_USER");
	const char* p = std::getenv("RATHENA_TEST_INTER_PASS");
	return {u ? u : "fake-inter-user", p ? p : "fake-inter-pass"};
}
} // namespace

// ---- BR-CHAR-023 / 024 -----------------------------------------------------------------------------
TEST(CharSession, BR_CHAR_023_FullOrMaintenanceRefusesBelowGmAllowGroup) {
	SessionConfig cfg;
	EXPECT_TRUE(admit_to_char_server(0, 10'000, cfg)); // -1 = unlimited
	cfg.max_connect_user = 100;
	EXPECT_FALSE(admit_to_char_server(0, 100, cfg));
	EXPECT_TRUE(admit_to_char_server(0, 99, cfg));
	EXPECT_TRUE(admit_to_char_server(99, 100, cfg));
	cfg = {};
	cfg.maintenance = true;
	EXPECT_FALSE(admit_to_char_server(10, 0, cfg));
	EXPECT_TRUE(admit_to_char_server(99, 0, cfg));
	cfg = {};
	cfg.max_connect_user = 0;
	EXPECT_FALSE(admit_to_char_server(1, 0, cfg));
}

TEST(CharSession, BR_CHAR_024_SlotsClampedToMaxCharsAndZeroToMinChars) {
	EXPECT_EQ(clamp_char_slots(20), kMaxChars);
	EXPECT_EQ(clamp_char_slots(0), kMinChars);
	EXPECT_EQ(clamp_char_slots(12), 12);
}

// ---- BR-CHAR-025 -----------------------------------------------------------------------------------
TEST(CharSession, BR_CHAR_025_DuplicateSessionKickCode8OfflineAfter30s) {
	const auto a = on_duplicate_session(kNow, {});
	EXPECT_TRUE(a.kick_existing);
	EXPECT_EQ(a.client_refuse_code, 8);
	EXPECT_EQ(a.force_offline_at, kNow + 30);
}

// ---- BR-CHAR-026 -----------------------------------------------------------------------------------
TEST(CharSession, BR_CHAR_026_ReturningFromMapReusesLocalTicketAndPinCounts) {
	AuthTicketStore store(30);
	store.issue(ticket());
	const auto out = decide_char_auth(store, 2'000'001, 0x11111111u, 0x22222222u, false, true, kNow + 5);
	EXPECT_EQ(out.decision, CharAuthDecision::AcceptLocalTicket);
	EXPECT_TRUE(out.pin_already_entered);
	EXPECT_FALSE(store.contains(2'000'001)); // single use
}

TEST(CharSession, BR_CHAR_026_OtherwiseVerifyWithLoginOrRejectWithoutLogin) {
	AuthTicketStore store(30);
	EXPECT_EQ(decide_char_auth(store, 2'000'001, 1, 2, false, true, kNow).decision, CharAuthDecision::AskLogin);
	EXPECT_EQ(decide_char_auth(store, 2'000'001, 1, 2, false, false, kNow).decision, CharAuthDecision::Reject);
	store.issue(ticket());
	EXPECT_EQ(decide_char_auth(store, 2'000'001, 0x11111111u, 0xBADu, false, true, kNow).decision,
	          CharAuthDecision::AskLogin); // wrong id2: not reused
	EXPECT_EQ(decide_char_auth(store, 2'000'001, 0x11111111u, 0x22222222u, true, true, kNow).decision,
	          CharAuthDecision::Reject); // already authed session
}

// ---- BR-CHAR-027 / 028 / 029 -----------------------------------------------------------------------
TEST(CharSession, BR_CHAR_027_OnlineStateAndUserCountNeverNegative) {
	OnlineRegistry reg;
	reg.set_online(2'000'001, 150'001, 0);
	reg.set_online(2'000'002, 150'002, 0);
	reg.set_online(2'000'003, 150'003, 1);
	EXPECT_EQ(reg.user_count(0), 2);
	EXPECT_EQ(reg.total_users(), 3);
	reg.set_online(2'000'001, 150'001, 0); // idempotent
	EXPECT_EQ(reg.user_count(0), 2);
	reg.set_online(2'000'001, 150'001, 1); // map change
	EXPECT_EQ(reg.user_count(0), 1);
	EXPECT_EQ(reg.user_count(1), 2);
	reg.set_offline(2'000'001);
	reg.set_offline(2'000'001);
	reg.set_offline(9'999'999);
	EXPECT_EQ(reg.user_count(1), 1);
	reg.mark_server_lost(1);
	reg.set_offline(2'000'003);
	EXPECT_EQ(reg.user_count(1), 0);
	EXPECT_GE(reg.total_users(), 0);
}

TEST(CharSession, BR_CHAR_028_StartupStateIsEveryoneOffline) {
	OnlineRegistry reg;
	EXPECT_EQ(reg.entries(), 0u);
	EXPECT_EQ(reg.total_users(), 0);
}

TEST(CharSession, BR_CHAR_028_StartupClearsOnlineColumnInDatabase) {
	GTEST_SKIP() << "PENDING BR-CHAR-028: CharacterRepository::set_all_offline() at boot (persistence unit); "
	                "integration test against a ${CHAR_DB_URL} test schema";
}

TEST(CharSession, BR_CHAR_029_CleanupSkipsConnectedEntriesAndContinues) {
	OnlineRegistry reg;
	// Ordered by account id: a still-connected entry comes first (legacy stopped here: DEFECT).
	reg.set_online(2'000'001, 0, kServerCharSelect, /*fd=*/7);
	reg.set_online(2'000'002, 150'002, 3);
	reg.set_online(2'000'003, 150'003, 3);
	reg.mark_server_lost(3); // both -> -2
	const auto offline = reg.cleanup_stale();
	EXPECT_EQ(offline, (std::vector<AccountId>{2'000'002, 2'000'003}));
	EXPECT_NE(reg.find(2'000'001), nullptr);
	EXPECT_EQ(reg.find(2'000'002), nullptr);
	EXPECT_EQ(reg.find(2'000'003), nullptr);
}

TEST(CharSession, BR_CHAR_029_CleanupKeepsEntriesOnLiveMapServers) {
	OnlineRegistry reg;
	reg.set_online(2'000'004, 150'004, 2);
	EXPECT_TRUE(reg.cleanup_stale().empty());
	EXPECT_NE(reg.find(2'000'004), nullptr);
}

// ---- BR-CHAR-030 -----------------------------------------------------------------------------------
TEST(CharSession, BR_CHAR_030_MapRegistrationNeedsEnvCredentialsAndFreeSlot) {
	const auto expected = fake_expected();
	EXPECT_EQ(check_map_registration(expected.user, expected.pass, expected, true), MapRegistrationResult::Ok);
	EXPECT_EQ(check_map_registration(expected.user, expected.pass, expected, false), MapRegistrationResult::Refused);
	EXPECT_EQ(check_map_registration(expected.user, "wrong-pass", expected, true), MapRegistrationResult::Refused);
	EXPECT_EQ(check_map_registration("wrong-user", expected.pass, expected, true), MapRegistrationResult::Refused);
}

TEST(CharSession, BR_CHAR_030_EmptyExpectedCredentialsNeverMatch) {
	EXPECT_EQ(check_map_registration("", "", InterServerCredentials{}, true), MapRegistrationResult::Refused);
	EXPECT_EQ(check_map_registration("x", "", InterServerCredentials{"x", ""}, true), MapRegistrationResult::Refused);
}

// ---- BR-CHAR-031 / BR-INTER-008 --------------------------------------------------------------------
TEST(CharSession, BR_CHAR_031_MapAuthMatchesAidCharIdId1AndIsSingleUse) {
	AuthTicketStore store(30);
	store.issue(ticket());
	EXPECT_FALSE(store.consume_for_map(2'000'001, 150'999, 0x11111111u, Sex::Male, false, kNow));
	EXPECT_FALSE(store.consume_for_map(2'000'001, 150'001, 0x0u, Sex::Male, false, kNow));
	EXPECT_FALSE(store.consume_for_map(2'000'001, 150'001, 0x11111111u, Sex::Female, true, kNow)); // old client sex
	EXPECT_TRUE(store.consume_for_map(2'000'001, 150'001, 0x11111111u, Sex::Male, true, kNow));
	EXPECT_FALSE(store.consume_for_map(2'000'001, 150'001, 0x11111111u, Sex::Male, true, kNow));
}

TEST(CharSession, BR_INTER_008_AuthTicketExpiresAfter30Seconds) {
	AuthTicketStore store(SessionConfig{}.auth_ticket_ttl);
	store.issue(ticket());
	EXPECT_FALSE(store.consume_for_map(2'000'001, 150'001, 0x11111111u, Sex::Male, false, kNow + 31));
	store.issue(ticket());
	EXPECT_TRUE(store.consume_for_map(2'000'001, 150'001, 0x11111111u, Sex::Male, false, kNow + 30));
}

TEST(CharSession, BR_INTER_008_ExpireSweepDropsOnlyStaleTickets) {
	AuthTicketStore store(30);
	store.issue(ticket(2'000'001));
	auto fresh = ticket(2'000'002);
	fresh.issued_at = kNow + 20;
	store.issue(fresh);
	EXPECT_EQ(store.expire(kNow + 40), 1u);
	EXPECT_FALSE(store.contains(2'000'001));
	EXPECT_TRUE(store.contains(2'000'002));
}

// ---- BR-CHAR-032 -----------------------------------------------------------------------------------
TEST(CharSession, BR_CHAR_032_SaveAcceptedOnlyIfOnlineOnThatServerOrFinal) {
	constexpr std::size_t kSize = 4096; // stand-in for sizeof(mmo_charstatus) at the build's PACKETVER
	OnlineRegistry reg;
	reg.set_online(2'000'001, 150'001, 2);
	SaveRequest req{2'000'001, 150'001, 2, false, false, kSize};
	EXPECT_EQ(accept_save(req, reg, kSize), SaveDecision::Accept);
	req.from_server = 3;
	EXPECT_EQ(accept_save(req, reg, kSize), SaveDecision::RejectNotOnline);
	req.final_save = true;
	EXPECT_EQ(accept_save(req, reg, kSize), SaveDecision::Accept);
	req = {2'000'001, 150'009, 2, false, false, kSize};
	EXPECT_EQ(accept_save(req, reg, kSize), SaveDecision::RejectNotOnline);
	req.restore = true;
	EXPECT_EQ(accept_save(req, reg, kSize), SaveDecision::Accept);
	req.payload_size = kSize - 1;
	EXPECT_EQ(accept_save(req, reg, kSize), SaveDecision::RejectSize);
}

TEST(CharSession, BR_CHAR_032_ExpectedPayloadSizeMatchesCharStatusLayout) {
	GTEST_SKIP() << "PENDING BR-CHAR-032: expected size = sizeof(mmo_charstatus) from the verbatim legacy layout "
	                "per PACKETVER (netproto codec), wired by the char-server delegate";
}

// ---- BR-INTER-009 ----------------------------------------------------------------------------------
TEST(InterSession, BR_INTER_009_IdRangesAndNameLength) {
	EXPECT_FALSE(is_valid_account_id(1'999'999));
	EXPECT_TRUE(is_valid_account_id(2'000'000));
	EXPECT_TRUE(is_valid_account_id(100'000'000));
	EXPECT_FALSE(is_valid_account_id(100'000'001));
	EXPECT_FALSE(is_valid_char_id(149'999));
	EXPECT_TRUE(is_valid_char_id(150'000));
	EXPECT_TRUE(fits_name_field(std::string(23, 'a')));
	EXPECT_FALSE(fits_name_field(std::string(24, 'a')));
	EXPECT_EQ(kNameLength, 24u);
}

// ---- BR-INTER-019 ----------------------------------------------------------------------------------
TEST(InterSession, BR_INTER_019_ActiveCharBanIsBroadcastToMapServers) {
	const auto ban = adjust_char_ban(0, 600, kNow);
	const auto notice = char_ban_notice(150'001, ban);
	ASSERT_TRUE(notice);
	EXPECT_EQ(notice->opcode, 0x2b14);
	EXPECT_EQ(notice->char_id, 150'001u);
	EXPECT_EQ(notice->type, 2);
	EXPECT_EQ(notice->unban_time, static_cast<std::uint32_t>(kNow + 600));
}

TEST(InterSession, BR_INTER_019_LiftedOrIgnoredBanIsNotBroadcast) {
	EXPECT_FALSE(char_ban_notice(150'001, adjust_char_ban(kNow + 10, -100, kNow)));
	EXPECT_FALSE(char_ban_notice(150'001, adjust_char_ban(0, -100, kNow)));
}
