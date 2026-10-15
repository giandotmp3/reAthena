// Acceptance tests: rules/anticheat, BR-MAP-056..061 (spec §5.4, CAP-20).
#include <gtest/gtest.h>

#include "rathena/map_rules/anticheat.hpp"

namespace mr = rathena::map_rules;
namespace ac = rathena::map_rules::anticheat;

TEST(AntiCheat, BR_MAP_056_LogoutLockUsesPreventLogoutNotHardcodedTenSeconds) {
	mr::BattleConfig cfg;
	cfg.prevent_logout = 25000; // [DEFECT fixed]: legacy always waited 10,000 ms
	EXPECT_FALSE(ac::can_logout(1000 + 10001, 1000, cfg));
	EXPECT_FALSE(ac::can_logout(1000 + 25000, 1000, cfg));
	EXPECT_TRUE(ac::can_logout(1000 + 25001, 1000, cfg));
	EXPECT_EQ(ac::logout_lock_remaining(1000 + 5000, 1000, cfg), 20001);
}

TEST(AntiCheat, BR_MAP_056_LockDisabledOrNeverInCombat) {
	mr::BattleConfig cfg;
	EXPECT_TRUE(ac::can_logout(5000, 0, cfg)); // never hit
	cfg.prevent_logout = 0;
	EXPECT_TRUE(ac::can_logout(5000, 4999, cfg));
}

TEST(AntiCheat, BR_MAP_057_ChatSpeakerNameCannotBeSpoofed) {
	const auto ok = ac::validate_chat_line("Alice", "Alice : hello");
	ASSERT_TRUE(ok.ok);
	EXPECT_EQ(ok.message, "hello");
	EXPECT_FALSE(ac::validate_chat_line("Alice", "GM_Bob : free items at prontera").ok);
	EXPECT_FALSE(ac::validate_chat_line("Alice", "Alice: hello").ok);
	EXPECT_FALSE(ac::validate_chat_line("Alice", "Alice : ").ok);
	EXPECT_FALSE(ac::validate_chat_line("Alice", "Alicex : hi").ok);
}

TEST(AntiCheat, BR_MAP_058_NameRequestForHiddenGmIsFlagged) {
	EXPECT_TRUE(ac::is_suspicious_name_request({.invisible = true}, 0, 60));
	EXPECT_FALSE(ac::is_suspicious_name_request({.invisible = true}, 60, 60));     // GM may see
	EXPECT_FALSE(ac::is_suspicious_name_request({.invisible = true, .is_npc = true}, 0, 60));
	EXPECT_FALSE(ac::is_suspicious_name_request({.invisible = false}, 0, 60));
}

TEST(AntiCheat, BR_MAP_059_VariableLengthFramingBounds) {
	EXPECT_EQ(ac::check_frame(-1, 2, 0).verdict, ac::FrameVerdict::NeedMore);
	EXPECT_EQ(ac::check_frame(-1, 4, 3).verdict, ac::FrameVerdict::Disconnect);
	EXPECT_EQ(ac::check_frame(-1, 40000, 32769).verdict, ac::FrameVerdict::Disconnect);
	const auto ok = ac::check_frame(-1, 8, 8);
	EXPECT_EQ(ok.verdict, ac::FrameVerdict::Ok);
	EXPECT_EQ(ok.length, 8);
	EXPECT_EQ(ac::check_frame(0, 8, 0).verdict, ac::FrameVerdict::Disconnect); // unknown opcode
	EXPECT_EQ(ac::check_frame(19, 10, 0).verdict, ac::FrameVerdict::Disconnect); // truncated fixed packet
}

TEST(AntiCheat, BR_MAP_059_PacketsBeforeLoadEndAreRejected) {
	using ac::PacketKind;
	using ac::SessionPhase;
	EXPECT_TRUE(ac::packet_allowed(SessionPhase::NoSession, PacketKind::WantToConnection));
	EXPECT_FALSE(ac::packet_allowed(SessionPhase::NoSession, PacketKind::Other));
	EXPECT_TRUE(ac::packet_allowed(SessionPhase::Authed, PacketKind::LoadEndAck));
	EXPECT_FALSE(ac::packet_allowed(SessionPhase::Authed, PacketKind::Other));
	EXPECT_TRUE(ac::packet_allowed(SessionPhase::OnMap, PacketKind::Other));
	EXPECT_FALSE(ac::packet_allowed(SessionPhase::OnMap, PacketKind::WantToConnection));
}

TEST(AntiCheat, BR_MAP_060_ConnectPacketSanity) {
	// Synthetic ids in the valid ranges; not taken from any real account.
	const ac::ConnectPacket good{.account_id = 2000001, .char_id = 150001, .login_id1 = 12345, .sex = 1};
	EXPECT_EQ(ac::validate_connect(19, 19, good, false), ac::ConnectError::None);
	EXPECT_EQ(ac::validate_connect(18, 19, good, false), ac::ConnectError::WrongLength);
	auto p = good;
	p.account_id = 1999999;
	EXPECT_EQ(ac::validate_connect(19, 19, p, false), ac::ConnectError::InvalidAccount);
	p = good;
	p.char_id = 0;
	EXPECT_EQ(ac::validate_connect(19, 19, p, false), ac::ConnectError::InvalidChar);
	p = good;
	p.sex = 2;
	EXPECT_EQ(ac::validate_connect(19, 19, p, false), ac::ConnectError::InvalidSex);
	EXPECT_EQ(ac::validate_connect(19, 19, good, true), ac::ConnectError::AlreadyLoggedIn);
}

TEST(AntiCheat, BR_MAP_061_VendingTradingBuyingStoreMutuallyExclusive) {
	EXPECT_TRUE(ac::can_begin({}, ac::Activity::Trading));
	EXPECT_FALSE(ac::can_begin({.vending = true}, ac::Activity::Trading));
	EXPECT_FALSE(ac::can_begin({.trading = true}, ac::Activity::Vending));
	EXPECT_FALSE(ac::can_begin({.buying_store = true}, ac::Activity::Vending));
	EXPECT_FALSE(ac::can_begin({.vending = true}, ac::Activity::BuyingStore));
}
