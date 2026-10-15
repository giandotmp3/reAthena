// Acceptance tests: PIN code (CAP-06). BR-CHAR-019..022.
#include <gtest/gtest.h>

#include "rathena/char_rules/pincode.hpp"

using namespace rathena::char_rules;

namespace {
constexpr UnixTime kNow = 1'760'000'000;
}

// ---- BR-CHAR-019 -----------------------------------------------------------------------------------
TEST(Pincode, BR_CHAR_019_StateMachine) {
	PincodeConfig cfg;
	cfg.enabled = false;
	EXPECT_EQ(initial_pin_state({}, cfg, kNow), PinState::Ok);

	cfg = {};
	EXPECT_EQ(initial_pin_state({false, 0, false}, cfg, kNow), PinState::New); // forced
	cfg.force = false;
	EXPECT_EQ(initial_pin_state({false, 0, false}, cfg, kNow), PinState::Passed);

	cfg = {};
	EXPECT_EQ(initial_pin_state({true, kNow - 10, false}, cfg, kNow), PinState::Ask);
	EXPECT_EQ(initial_pin_state({true, kNow - 10, true}, cfg, kNow), PinState::Passed);

	cfg.change_time = 3600;
	EXPECT_EQ(initial_pin_state({true, kNow - 7200, false}, cfg, kNow), PinState::Expired);
	EXPECT_EQ(initial_pin_state({true, kNow - 60, false}, cfg, kNow), PinState::Ask);
}

// ---- BR-CHAR-020 -----------------------------------------------------------------------------------
TEST(Pincode, BR_CHAR_020_WorkedExamplesIllegalAndAccepted) {
	PincodeConfig cfg; // shipped: repeated and sequential not allowed
	EXPECT_FALSE(pincode_allowed("1234", cfg));
	EXPECT_FALSE(pincode_allowed("8901", cfg)); // ascending, wraps 9 -> 0
	EXPECT_FALSE(pincode_allowed("3210", cfg));
	EXPECT_FALSE(pincode_allowed("1111", cfg));
	EXPECT_TRUE(pincode_allowed("1357", cfg));
}

TEST(Pincode, BR_CHAR_020_ExactlyFourDigits) {
	PincodeConfig cfg;
	EXPECT_FALSE(pincode_allowed("135", cfg));
	EXPECT_FALSE(pincode_allowed("13579", cfg));
	EXPECT_FALSE(pincode_allowed("13a7", cfg));
	EXPECT_FALSE(pincode_allowed("1098", cfg)); // descending wrap 0 -> 9
	cfg.allow_repeated = true;
	cfg.allow_sequential = true;
	EXPECT_TRUE(pincode_allowed("1111", cfg));
	EXPECT_TRUE(pincode_allowed("1234", cfg));
}

// ---- BR-CHAR-021 -----------------------------------------------------------------------------------
TEST(Pincode, BR_CHAR_021_ThreeWrongPinsNotifyLoginAndDisconnect) {
	PinAttemptCounter counter(PincodeConfig{}.max_tries);
	EXPECT_FALSE(counter.record(false).notify_login_and_disconnect);
	EXPECT_FALSE(counter.record(false).notify_login_and_disconnect);
	const auto third = counter.record(false);
	EXPECT_TRUE(third.notify_login_and_disconnect);
	EXPECT_EQ(third.failures, 3);
}

TEST(Pincode, BR_CHAR_021_SuccessResetsCounter) {
	PinAttemptCounter counter(3);
	counter.record(false);
	counter.record(false);
	EXPECT_TRUE(counter.record(true).accepted);
	EXPECT_EQ(counter.failures(), 0);
	EXPECT_FALSE(counter.record(false).notify_login_and_disconnect);
}

// ---- BR-CHAR-022 -----------------------------------------------------------------------------------
TEST(Pincode, BR_CHAR_022_OnlyConnectPingPinAndCharlistBeforeVerification) {
	PincodeConfig cfg;
	const CharPacketIds ids;
	const GateSession waiting{true, false, PinState::Ask};
	for (const auto cmd : {ids.connect, ids.ping, ids.pin_ack, ids.pin_edit, ids.pin_available, ids.pin_make,
	                       ids.charlist_req}) {
		EXPECT_EQ(pin_packet_gate(cmd, waiting, cfg), GateDecision::Allow) << std::hex << cmd;
	}
	constexpr std::uint16_t kSelectChar = 0x0066;
	constexpr std::uint16_t kMakeChar = 0x0a39;
	EXPECT_EQ(pin_packet_gate(kSelectChar, waiting, cfg), GateDecision::Disconnect);
	EXPECT_EQ(pin_packet_gate(kMakeChar, waiting, cfg), GateDecision::Disconnect);

	const GateSession verified{true, true, PinState::Ask};
	EXPECT_EQ(pin_packet_gate(kSelectChar, verified, cfg), GateDecision::Allow);
}

TEST(Pincode, BR_CHAR_022_ForcedNewPinStaysGatedUntilCreated) {
	PincodeConfig cfg;
	EXPECT_EQ(pin_packet_gate(0x0066, {true, false, PinState::New}, cfg), GateDecision::Disconnect);
	EXPECT_EQ(pin_packet_gate(0x0066, {true, false, PinState::Passed}, cfg), GateDecision::Allow);
	cfg.enabled = false;
	EXPECT_EQ(pin_packet_gate(0x0066, {true, false, PinState::Ok}, cfg), GateDecision::Allow);
}

TEST(Pincode, BR_CHAR_022_NoSessionNeverReachesHandlersS5) {
	PincodeConfig cfg;
	cfg.enabled = false;
	EXPECT_EQ(pin_packet_gate(0x0066, {false, false, PinState::Ok}, cfg), GateDecision::Disconnect);
	EXPECT_EQ(pin_packet_gate(0x0065, {false, false, PinState::Ok}, cfg), GateDecision::Allow);
}

TEST(Pincode, BR_CHAR_022_GateOpcodesMatchShuffledHeadersPerPacketver) {
	GTEST_SKIP() << "PENDING BR-CHAR-022: per-PACKETVER header ids come from the verbatim packet headers via "
	                "netproto (architecture K1); golden check against common/packets.hpp for each supported date";
}
