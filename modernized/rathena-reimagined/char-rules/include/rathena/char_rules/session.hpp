// rules/charlife (session) + rules/session (char side): CAP-04 session handoff, CAP-07 save
// acceptance. BR-CHAR-023..032; BR-INTER-008, 009, 019.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rathena/char_rules/charlife.hpp"
#include "rathena/char_rules/config.hpp"
#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

// ---- BR-CHAR-023 / 024 ----------------------------------------------------------------------------
/// Refuse (chclif_reject 0) when full, when max_connect_user == 0, or in maintenance, unless the
/// account's group is >= gm_allow_group.
bool admit_to_char_server(int group_id, int online_users, const SessionConfig& cfg);
/// Slots > MAX_CHARS -> MAX_CHARS; 0 -> MIN_CHARS.
int clamp_char_slots(int slots);

// ---- BR-CHAR-025 duplicate session ---------------------------------------------------------------
struct DuplicateSessionAction {
	bool kick_existing = true;
	std::uint8_t client_refuse_code = 8; ///< "already logged in"
	UnixTime force_offline_at = 0;
};
DuplicateSessionAction on_duplicate_session(UnixTime now, const SessionConfig& cfg);

// ---- BR-INTER-008 / BR-CHAR-026 / BR-CHAR-031 auth tickets -----------------------------------------
/// One-time tickets keyed by account. TTL from config (30 s); single use.
class AuthTicketStore {
public:
	explicit AuthTicketStore(UnixTime ttl_seconds) : ttl_(ttl_seconds) {}

	void issue(const AuthTicket& t) { tickets_[t.account_id] = t; }
	bool contains(AccountId aid) const { return tickets_.count(aid) != 0; }
	std::size_t size() const { return tickets_.size(); }

	/// BR-CHAR-026 client reconnect from map: match aid + id1 + id2; consumes on success.
	std::optional<AuthTicket> consume_for_client(AccountId aid, std::uint32_t id1, std::uint32_t id2, UnixTime now);

	/// BR-CHAR-031 map -> char player auth: match aid, char_id, id1 (+sex on old clients); consumes.
	std::optional<AuthTicket> consume_for_map(AccountId aid, CharId cid, std::uint32_t id1, Sex sex,
	                                          bool check_sex, UnixTime now);

	/// BR-INTER-008: drop tickets older than the TTL (legacy 30 s auth_db cleanup timer).
	std::size_t expire(UnixTime now);

private:
	bool expired(const AuthTicket& t, UnixTime now) const { return now - t.issued_at > ttl_; }
	UnixTime ttl_;
	std::unordered_map<AccountId, AuthTicket> tickets_;
};

enum class CharAuthDecision : std::uint8_t {
	AcceptLocalTicket, ///< returning from map: PIN counts as entered
	AskLogin,          ///< forward 0x2712 to login
	Reject,            ///< no login server connected / already authed
};
struct CharAuthOutcome {
	CharAuthDecision decision = CharAuthDecision::Reject;
	bool pin_already_entered = false;
};
/// BR-CHAR-026.
CharAuthOutcome decide_char_auth(AuthTicketStore& store, AccountId aid, std::uint32_t id1, std::uint32_t id2,
                                 bool session_already_authed, bool login_connected, UnixTime now);

// ---- BR-CHAR-027 / 028 / 029 online state ----------------------------------------------------------
class OnlineRegistry {
public:
	/// BR-CHAR-028: startup state is "everyone offline" (persistence clears the `online` column).
	OnlineRegistry() = default;

	void set_online(AccountId aid, CharId cid, ServerId server, int fd = -1);
	void set_offline(AccountId aid);
	/// A map server disconnected: its players move to "unknown server" (-2) (BR-LOGIN-033 parity).
	void mark_server_lost(ServerId server);
	const OnlineEntry* find(AccountId aid) const;
	std::size_t entries() const { return online_.size(); }

	/// Users on a map server; never negative (BR-CHAR-027).
	int user_count(ServerId server) const;
	int total_users() const;

	/// BR-CHAR-029 stale cleanup. [DEFECT fix] legacy returned at the first still-connected entry;
	/// here a connected entry is skipped and the scan continues. Returns the accounts set offline.
	std::vector<AccountId> cleanup_stale();

private:
	std::map<AccountId, OnlineEntry> online_;
	std::map<ServerId, int> counts_;
	void dec(ServerId s);
};

// ---- BR-CHAR-030 map-server registration -----------------------------------------------------------
/// Expected credentials come from ${INTER_SERVER_USER} / ${INTER_SERVER_PASS} at runtime; this rule
/// never sees a literal. Empty expected credentials always refuse (secure default).
struct InterServerCredentials {
	std::string user;
	std::string pass;
};
enum class MapRegistrationResult : std::uint8_t { Ok = 0, Refused = 3 };
MapRegistrationResult check_map_registration(std::string_view user, std::string_view pass,
                                             const InterServerCredentials& expected, bool free_slot);

// ---- BR-CHAR-032 save acceptance -------------------------------------------------------------------
struct SaveRequest {
	AccountId account_id = 0;
	CharId char_id = 0;
	ServerId from_server = 0;
	bool final_save = false;
	bool restore = false;
	std::size_t payload_size = 0;
};
enum class SaveDecision : std::uint8_t { Accept, RejectSize, RejectNotOnline };
SaveDecision accept_save(const SaveRequest& req, const OnlineRegistry& online, std::size_t expected_payload_size);

// ---- BR-INTER-009 id ranges / name length ----------------------------------------------------------
bool is_valid_account_id(std::uint32_t aid);
bool is_valid_char_id(std::uint32_t cid);
bool fits_name_field(std::string_view name); ///< <= NAME_LENGTH-1 bytes

// ---- BR-INTER-019 char -> map ban notification -----------------------------------------------------
struct CharBanNotice {
	std::uint16_t opcode = 0x2b14;
	CharId char_id = 0;
	std::uint8_t type = 2; ///< 2 = char ban
	std::uint32_t unban_time = 0;
};
/// Only an active ban (kick) is broadcast to all map servers.
std::optional<CharBanNotice> char_ban_notice(CharId cid, const CharBanOutcome& ban);

} // namespace rathena::char_rules
