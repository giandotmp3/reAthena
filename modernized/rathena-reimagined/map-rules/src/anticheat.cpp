#include "rathena/map_rules/anticheat.hpp"

#include <algorithm>

namespace rathena::map_rules::anticheat {

bool can_logout(Tick now, Tick last_combat_tick, const BattleConfig& cfg) {
	return logout_lock_remaining(now, last_combat_tick, cfg) == 0;
}

Tick logout_lock_remaining(Tick now, Tick last_combat_tick, const BattleConfig& cfg) {
	if (cfg.prevent_logout <= 0 || last_combat_tick == 0) {
		return 0;
	}
	const Tick elapsed = now - last_combat_tick;
	// Legacy allows quitting once DIFF_TICK(now, canlog_tick) > prevent_logout.
	if (elapsed > cfg.prevent_logout) {
		return 0;
	}
	return cfg.prevent_logout - elapsed + 1;
}

ChatSpeakerCheck check_chat_speaker(std::string_view own_name, std::string_view raw) {
	constexpr std::string_view kSep = " : ";
	ChatSpeakerCheck c;
	const std::string_view name = own_name.substr(0, std::min(own_name.size(), kNameLength - 1));
	if (raw.size() < name.size() + kSep.size() + 1) {
		return c; // Malformed
	}
	if (raw.substr(0, name.size()) != name || raw.substr(name.size(), kSep.size()) != kSep) {
		c.verdict = ChatSpeaker::Spoofed;
		return c;
	}
	c.verdict = ChatSpeaker::Ok;
	c.message_offset = name.size() + kSep.size();
	return c;
}

ChatCheck validate_chat_line(std::string_view own_name, std::string_view raw) {
	ChatCheck c;
	const ChatSpeakerCheck speaker = check_chat_speaker(own_name, raw);
	if (speaker.verdict != ChatSpeaker::Ok) {
		return c;
	}
	std::string_view text = raw.substr(speaker.message_offset);
	if (const auto nul = text.find('\0'); nul != std::string_view::npos) {
		text = text.substr(0, nul);
	}
	c.ok = true;
	c.message = std::string(text);
	return c;
}

bool is_suspicious_name_request(const NameRequestTarget& t, std::int32_t requester_group_level,
                                std::int32_t hack_info_gm_level) {
	return t.invisible && !t.disguised && !t.is_npc && requester_group_level < hack_info_gm_level;
}

FrameCheck check_frame(std::int32_t db_len, std::size_t available, std::uint16_t declared_len) {
	FrameCheck f;
	if (db_len == 0) {
		f.verdict = FrameVerdict::Disconnect; // unknown opcode
		f.error = FrameError::UnknownOpcode;
		return f;
	}
	std::int32_t len = db_len;
	if (db_len == -1) {
		if (available < 4) {
			return f; // NeedMore
		}
		len = declared_len;
		f.length = len; // reported in the disconnect warnings too
		if (len < 4 || len > 32768) {
			f.verdict = FrameVerdict::Disconnect;
			f.error = FrameError::BadDeclaredLength;
			return f;
		}
	}
	f.length = len;
	if (available < static_cast<std::size_t>(len)) {
		// Legacy disconnects when the remaining bytes are short of a complete packet.
		f.verdict = FrameVerdict::Disconnect;
		f.error = FrameError::Truncated;
		return f;
	}
	f.verdict = FrameVerdict::Ok;
	return f;
}

bool packet_allowed(SessionPhase phase, PacketKind kind) {
	switch (phase) {
	case SessionPhase::NoSession:
		return kind == PacketKind::WantToConnection;
	case SessionPhase::Authed:
		return kind == PacketKind::LoadEndAck;
	case SessionPhase::OnMap:
		return true;
	}
	return false;
}

ConnectError validate_connect(std::size_t packet_len, std::size_t expected_len, const ConnectPacket& p,
                              bool session_already_authed) {
	if (session_already_authed) {
		return ConnectError::AlreadyLoggedIn;
	}
	if (packet_len != expected_len) {
		return ConnectError::WrongLength;
	}
	if (p.account_id < kStartAccountNum || p.account_id > kEndAccountNum) {
		return ConnectError::InvalidAccount;
	}
	if (p.char_id <= 0) {
		return ConnectError::InvalidChar;
	}
	if (p.sex != 0 && p.sex != 1) {
		return ConnectError::InvalidSex;
	}
	return ConnectError::None;
}

bool can_begin(const CommerceState& s, Activity a) {
	(void)a;
	return !s.vending && !s.buying_store && !s.trading;
}

} // namespace rathena::map_rules::anticheat
