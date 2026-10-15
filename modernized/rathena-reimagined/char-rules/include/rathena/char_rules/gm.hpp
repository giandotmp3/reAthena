// rules/gm: CAP-21 GM account operations relayed by the char server. BR-CHAR-055.
//
// [DEFECT fix] Legacy chmapif_parse_fwlog_changestatus (char_mapif.cpp:748-852) trusted the map
// server's request and never re-checked the requester ("FIXME: need to move this check").
// The rebuild re-checks authority on the char server before anything is forwarded to login.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

/// common/socket.hpp enum chrif_req_op.
enum class GmOp : std::uint16_t {
	LoginBlock = 1,
	LoginBan = 2,
	LoginUnblock = 3,
	LoginUnban = 4,
	LoginChangeSex = 5,
	LoginVip = 6,
	Ban = 7,
	Unban = 8,
	ChangeCharSex = 9,
};

/// Answer codes sent back to the requesting map server.
enum class GmResult : std::uint8_t {
	Done = 0,
	NotFound = 1,
	NotAuthorized = 2, ///< "gm level too low"
	LoginOffline = 3,
};

inline constexpr std::int32_t kServerRequester = -1; ///< request made by the server itself

struct GmRequest {
	std::int32_t requester_aid = kServerRequester;
	GmOp op = GmOp::LoginBlock;
	std::string target_name;
	std::int32_t timediff = 0;
	std::int32_t val1 = 0;
	Sex sex = Sex::Male;
};

struct GmTarget {
	AccountId account_id = 0;
	CharId char_id = 0;
	int group_level = 0;
};

struct GmContext {
	/// Requester's group level as known to the char server (from login account data), not as
	/// claimed by the map server. nullopt = unknown requester.
	std::optional<int> requester_group_level;
	/// Permission check for the operation (pc_groups permission table, injected).
	std::function<bool(GmOp)> requester_may = [](GmOp) { return false; };
	std::optional<GmTarget> target;
	bool login_connected = true;
};

/// What the char server forwards to login (or applies locally).
struct GmForward {
	std::uint16_t opcode = 0;   ///< 0x2724 state, 0x2725 ban, 0x272a unban, 0x2727 sex, 0x2742 vip; 0 = local
	AccountId account_id = 0;
	CharId char_id = 0;
	std::int32_t value = 0;     ///< new state (5 block / 0 unblock) or timediff
};

struct GmOutcome {
	GmResult result = GmResult::NotAuthorized;
	std::optional<GmForward> forward;
	bool answer_requester = true; ///< legacy: no answer for sex changes / server requests
};

GmOutcome process_gm_request(const GmRequest& req, const GmContext& ctx);

} // namespace rathena::char_rules
