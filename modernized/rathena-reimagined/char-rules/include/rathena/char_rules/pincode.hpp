// rules/charlife (PIN): CAP-06 second password. BR-CHAR-019..022.
#pragma once

#include <cstdint>
#include <string_view>

#include "rathena/char_rules/config.hpp"
#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

/// Client-facing PIN states (char.hpp:113-126, PACKETVER >= 20180124 numbering for PASSED).
enum class PinState : std::uint8_t {
	Ok = 0,      ///< system disabled
	Ask = 1,
	NotSet = 2,
	Expired = 3,
	New = 4,     ///< forced creation of a PIN
	Passed = 7,
};

struct PinAccountState {
	bool has_pin = false;
	UnixTime last_change = 0;
	bool already_passed_this_session = false; ///< online entry pincode_success
};

/// BR-CHAR-019 state machine.
PinState initial_pin_state(const PinAccountState& acct, const PincodeConfig& cfg, UnixTime now);

/// BR-CHAR-020: exactly 4 digits; repeated / ascending / descending (wrapping 9->0) refused
/// unless allowed by config.
bool pincode_allowed(std::string_view pin, const PincodeConfig& cfg);

/// BR-CHAR-021: wrong-PIN counter. Success resets; reaching max_tries notifies login and the
/// account goes offline.
class PinAttemptCounter {
public:
	struct Verdict {
		bool accepted = false;
		bool notify_login_and_disconnect = false;
		int failures = 0;
	};
	explicit PinAttemptCounter(int max_tries) : max_tries_(max_tries) {}
	Verdict record(bool pin_correct);
	int failures() const noexcept { return failures_; }

private:
	int max_tries_;
	int failures_ = 0;
};

/// BR-CHAR-022 packet gate. Command ids are the char-server headers for the default PACKETVER.
struct CharPacketIds {
	std::uint16_t connect = 0x0065;              ///< CH_ENTER
	std::uint16_t ping = 0x0187;                 ///< HEADER_PING
	std::uint16_t pin_ack = 0x08b8;              ///< CH_SECOND_PASSWD_ACK
	std::uint16_t pin_edit = 0x08be;             ///< CH_EDIT_SECOND_PASSWD
	std::uint16_t pin_available = 0x08c5;        ///< CH_AVAILABLE_SECOND_PASSWD
	std::uint16_t charlist_req = 0x09a1;         ///< CH_CHARLIST_REQ
	std::uint16_t map_login = 0x2af8;            ///< map-server registration
	std::uint16_t pin_make = 0x08ba;             ///< CH_MAKE_SECOND_PASSWD
};

enum class GateDecision : std::uint8_t { Allow, Disconnect };

struct GateSession {
	bool has_session = false; ///< S5: no auth data -> never reaches handlers
	bool pin_verified = false;
	PinState pin_state = PinState::Ask;
};

GateDecision pin_packet_gate(std::uint16_t cmd, const GateSession& s, const PincodeConfig& cfg,
                             const CharPacketIds& ids = {});

} // namespace rathena::char_rules
