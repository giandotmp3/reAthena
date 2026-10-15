// Typed, immutable char-rule policy values (architecture §2 "BattleConfig ... passed explicitly",
// spec §5.6 "shipped values become the code defaults").
//
// Defaults below are the values shipped in conf/char_athena.conf and conf/inter_athena.conf.
// No credential lives here: inter-server credentials are injected at runtime from
// ${INTER_SERVER_USER} / ${INTER_SERVER_PASS} (BR-INTER-016, owned by core/secrets).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

enum class NameLetterPolicy : std::uint8_t {
	Any = 0,       ///< char_name_option 0
	AllowOnly = 1, ///< char_name_option 1: only char_name_letters are allowed
	Forbid = 2,    ///< char_name_option 2: char_name_letters are forbidden
};

/// char_del_restriction bitmask (char.hpp e_char_delete_restriction).
enum DeleteRestriction : std::uint8_t {
	kDelRestrictNone = 0,
	kDelRestrictParty = 1,
	kDelRestrictGuild = 2,
	kDelRestrictAll = 3,
};

/// char_del_option bitmask (char.hpp e_char_delete).
enum DeleteOption : std::uint8_t {
	kDelOptionEmail = 1,
	kDelOptionBirthdate = 2,
};

struct NameConfig {
	std::size_t min_length = 4;                     ///< char_name_min_length
	NameLetterPolicy policy = NameLetterPolicy::AllowOnly; ///< char_name_option: 1
	std::string letters = "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
	std::string wisp_server_name = "Server";        ///< reserved name (BR-CHAR-001)
};

struct CreationConfig {
	bool char_new = true;                    ///< BR-CHAR-008
	int start_status_points = 48;            ///< inter_athena.conf (BR-CHAR-005)
	std::uint8_t allowed_job_flag = 3;       ///< &1 Novice, &2 Summoner (BR-CHAR-006)
	std::int32_t start_zeny = 0;             ///< BR-CHAR-007
	std::vector<MapPoint> start_points = {{"iz_int", 18, 26}, {"iz_int01", 18, 26}, {"iz_int02", 18, 26},
	                                      {"iz_int03", 18, 26}, {"iz_int04", 18, 26}};
	std::vector<MapPoint> start_points_doram = {{"lasa_fild01", 48, 297}};
	std::vector<StartItem> start_items = {{1201, 1, 2}, {2301, 1, 16}, {23484, 1, 0}};
	std::vector<StartItem> start_items_doram = {{1681, 1, 2}, {2301, 1, 16}};
	bool vip_build = true;                   ///< BR-CHAR-003: VIP_ENABLE compile switch
	int client_packetver = 20211103;         ///< PACKETVER (BR-CHAR-004/005)
};

struct DeletionConfig {
	UnixTime delay_seconds = 86'400;         ///< char_del_delay (BR-CHAR-009)
	int level_limit = 0;                     ///< char_del_level; >0: >= blocks, <0: <= -x blocks
	std::uint8_t restriction = kDelRestrictAll; ///< char_del_restriction: 3
	std::uint8_t option = kDelOptionBirthdate;  ///< char_del_option: 2
};

struct RenameConfig {
	bool allow_in_party = false; ///< char_rename_party
	bool allow_in_guild = false; ///< char_rename_guild
};

struct SlotMoveConfig {
	bool enabled = true;     ///< char_move_enabled
	bool move_to_used = true;///< char_movetoused
	bool unlimited = false;  ///< char_moves_unlimited
};

struct PincodeConfig {
	bool enabled = true;          ///< pincode_enabled
	UnixTime change_time = 0;     ///< pincode_changetime (0 = never expires)
	int max_tries = 3;            ///< pincode_maxtry
	bool force = true;            ///< pincode_force
	bool allow_repeated = false;  ///< pincode_allow_repeated
	bool allow_sequential = false;///< pincode_allow_sequential
};

struct SessionConfig {
	int max_connect_user = -1;    ///< -1 unlimited; 0 = only gm_allow_group (BR-CHAR-023)
	int gm_allow_group = 99;
	bool maintenance = false;     ///< char_maintenance
	UnixTime auth_ticket_ttl = 30;///< BR-INTER-008 (login.hpp:150, char.hpp:222)
	UnixTime duplicate_offline_after = 30; ///< BR-CHAR-025
};

struct PartyGuildConfig {
	int party_share_level = 15;   ///< spec §5.6 shipped value
	bool official_guild_storage = true; ///< OFFICIAL_GUILD_STORAGE (BR-CHAR-045)
	/// Guild exp needed for level N -> N+1, indexed by N-1 (db/guild_exp.yml). Injected.
	std::vector<std::uint64_t> guild_next_exp;
};

struct MailConfig {
	int return_days = 15;  ///< mail_return_days (spec §5.6 shipped)
	int delete_days = 15;  ///< mail_delete_days
	bool retrieve_unread = true; ///< mail_retrieve
};

struct ClanConfig {
	int remove_inactive_days = 14; ///< clan_remove_inactive_days (BR-CHAR-051)
};

struct FameConfig {
	std::size_t list_size = kMaxFameList; ///< fame_list_* (<= MAX_FAME_LIST)
};

struct CharRulesConfig {
	NameConfig name;
	CreationConfig creation;
	DeletionConfig deletion;
	RenameConfig rename;
	SlotMoveConfig slot_move;
	PincodeConfig pincode;
	SessionConfig session;
	PartyGuildConfig party_guild;
	MailConfig mail;
	ClanConfig clan;
	FameConfig fame;
};

} // namespace rathena::char_rules
