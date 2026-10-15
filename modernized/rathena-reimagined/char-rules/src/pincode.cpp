#include "rathena/char_rules/pincode.hpp"

#include <string>

namespace rathena::char_rules {

PinState initial_pin_state(const PinAccountState& acct, const PincodeConfig& cfg, UnixTime now) {
	if (!cfg.enabled) {
		return PinState::Ok;
	}
	if (!acct.has_pin) {
		return cfg.force ? PinState::New : PinState::Passed;
	}
	if (cfg.change_time == 0 || acct.last_change + cfg.change_time > now) {
		return acct.already_passed_this_session ? PinState::Passed : PinState::Ask;
	}
	return PinState::Expired;
}

bool pincode_allowed(std::string_view pin, const PincodeConfig& cfg) {
	if (pin.size() != kPincodeLength) {
		return false;
	}
	for (const char c : pin) {
		if (c < '0' || c > '9') {
			return false;
		}
	}
	if (!cfg.allow_repeated && pin == std::string(kPincodeLength, pin[0])) {
		return false;
	}
	if (!cfg.allow_sequential) {
		std::string asc(kPincodeLength, '0');
		std::string desc(kPincodeLength, '0');
		const int first = pin[0] - '0';
		for (std::size_t i = 0; i < kPincodeLength; ++i) {
			const int k = static_cast<int>(i);
			asc[i] = static_cast<char>('0' + (first + k) % 10);
			desc[i] = static_cast<char>('0' + ((first - k) % 10 + 10) % 10);
		}
		if (pin == asc || pin == desc) {
			return false;
		}
	}
	return true;
}

PinAttemptCounter::Verdict PinAttemptCounter::record(bool pin_correct) {
	if (pin_correct) {
		failures_ = 0;
		return {true, false, 0};
	}
	++failures_;
	return {false, failures_ >= max_tries_, failures_};
}

GateDecision pin_packet_gate(std::uint16_t cmd, const GateSession& s, const PincodeConfig& cfg,
                             const CharPacketIds& ids) {
	// Connect and map-server registration are the only packets accepted before a session exists.
	if (cmd == ids.connect || cmd == ids.map_login) {
		return GateDecision::Allow;
	}
	if (!s.has_session) {
		return GateDecision::Disconnect; // S5
	}
	if (!cfg.enabled) {
		return GateDecision::Allow;
	}
	if (cmd == ids.ping || cmd == ids.pin_ack || cmd == ids.pin_edit || cmd == ids.pin_available ||
	    cmd == ids.pin_make || cmd == ids.charlist_req) {
		return GateDecision::Allow;
	}
	// Legacy (char_clif.cpp:1680-1690) also let every packet through while no PIN was set, even when
	// pincode_force demanded one. The spec reads "until the PIN is verified", so a forced-NEW
	// session stays gated until it creates a PIN; a non-forced account with no PIN is PASSED.
	if (s.pin_verified || s.pin_state == PinState::Ok || s.pin_state == PinState::Passed) {
		return GateDecision::Allow;
	}
	return GateDecision::Disconnect;
}

} // namespace rathena::char_rules
