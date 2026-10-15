// rules/anticheat: CAP-20 anti-cheat and packet integrity, BR-MAP-056..061.
// Legacy homes: src/map/clif.cpp (gating, chat, connect), vending.cpp / trade.cpp (exclusivity).
// Packet *layouts* stay in the verbatim PACKETVER headers (architecture K1); these rules decide
// on already-decoded values.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "rathena/map_rules/battle_config.hpp"
#include "rathena/map_rules/domain.hpp"

namespace rathena::map_rules::anticheat {

inline constexpr std::size_t kNameLength = 24; // NAME_LENGTH (including NUL)

/// BR-MAP-056: combat logout lock. [DEFECT fixed] Legacy hard-coded 10,000 ms for the delayed
/// quit; the rebuild uses prevent_logout everywhere. 0 disables the lock.
bool can_logout(Tick now, Tick last_combat_tick, const BattleConfig& cfg);
/// BR-MAP-056: ms until the player may log out (0 if allowed now).
Tick logout_lock_remaining(Tick now, Tick last_combat_tick, const BattleConfig& cfg);

struct ChatCheck {
	bool ok = false;
	std::string message; // text after "<name> : " when ok
};
/// BR-MAP-057: a public chat line must be exactly "<own name> : <text>" with non-empty text;
/// any other speaker name is a spoof and is rejected.
ChatCheck validate_chat_line(std::string_view own_name, std::string_view raw);

struct NameRequestTarget {
	bool invisible = false; // OPTION_INVISIBLE (GM hide)
	bool disguised = false;
	bool is_npc = false;
};
/// BR-MAP-058: a name request for a hidden (GM-hide) non-NPC target from a player below
/// hack_info_gm_level is flagged. Legacy disabled the action due to lag false positives, so the
/// caller decides whether to report or only log.
bool is_suspicious_name_request(const NameRequestTarget& t, std::int32_t requester_group_level,
                                std::int32_t hack_info_gm_level);

// --- BR-MAP-059: framing and state gating -----------------------------------------------------

enum class FrameVerdict : std::uint8_t { NeedMore, Ok, Disconnect };
struct FrameCheck {
	FrameVerdict verdict = FrameVerdict::NeedMore;
	std::int32_t length = 0;
};
/// `db_len` is the packet_db length (-1 = variable, 0 = unknown opcode). Variable lengths must be
/// 4..32768 bytes; unknown opcodes disconnect.
FrameCheck check_frame(std::int32_t db_len, std::size_t available, std::uint16_t declared_len);

enum class SessionPhase : std::uint8_t { NoSession, Authed, OnMap };
enum class PacketKind : std::uint8_t { WantToConnection, LoadEndAck, Other };
/// Before a session exists only the connect packet is processed; before load-end only
/// LoadEndAck is processed; everything else is dropped.
bool packet_allowed(SessionPhase phase, PacketKind kind);

// --- BR-MAP-060: connect packet sanity --------------------------------------------------------

struct ConnectPacket {
	std::int32_t account_id = 0;
	std::int32_t char_id = 0;
	std::int32_t login_id1 = 0;
	std::uint32_t client_tick = 0;
	std::uint8_t sex = 0;
};
/// Values are the legacy 0x6a refusal codes where one exists.
enum class ConnectError : std::uint8_t {
	None = 0,
	WrongLength = 1,
	InvalidAccount = 2,
	InvalidChar = 3,
	InvalidSex = 6,
	AlreadyLoggedIn = 0xFF, // legacy: logged and ignored, no 0x6a sent
};
ConnectError validate_connect(std::size_t packet_len, std::size_t expected_len, const ConnectPacket& p,
                              bool session_already_authed);

// --- BR-MAP-061: exclusive commerce states ----------------------------------------------------

struct CommerceState {
	bool vending = false;
	bool buying_store = false;
	bool trading = false;
};
enum class Activity : std::uint8_t { Vending, BuyingStore, Trading };
/// Vending, trading and buying-store are mutually exclusive.
bool can_begin(const CommerceState& s, Activity a);

} // namespace rathena::map_rules::anticheat
