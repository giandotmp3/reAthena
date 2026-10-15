// rules/charlife: character lifecycle (CAP-05, CAP-21 partial). BR-CHAR-001..018, 052, 053.
//
// Every function is pure: existing-name lookups, slot occupancy, clock values and randomness are
// passed in. The legacy char.cpp / char_clif.cpp call sites keep their SQL and delegate the
// decision to these functions (architecture §3.2 "Delegate").
#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/char_rules/config.hpp"
#include "rathena/char_rules/domain.hpp"
#include "rathena/char_rules/rng.hpp"

namespace rathena::char_rules {

// ---- BR-CHAR-001 name validation ----------------------------------------------------------------
/// Legacy TRIM_CHARS (char.hpp:161): 0xAD, 0xA0, 0x1A, '\t', '\n', '\r', ' '.
inline constexpr std::string_view kTrimChars{"\xAD\xA0\x1A\t\n\r ", 7};

/// normalize_name(): truncates to NAME_LENGTH-1, trims leading/trailing TRIM_CHARS and collapses
/// interior runs of them into one space.
std::string normalize_char_name(std::string_view raw);

enum class NameResult : int {
	Ok = 0,
	Taken = -1,    ///< already exists, or the reserved wisp server name
	Invalid = -2,  ///< empty / too short / control chars / '#' prefix / letter policy
};

/// `name_exists` is the uniqueness probe (case handling per name_ignoring_case lives in the repo).
NameResult check_char_name(std::string_view normalized_name, const NameConfig& cfg,
                           const std::function<bool(std::string_view)>& name_exists);

// ---- BR-CHAR-002..008 creation ------------------------------------------------------------------
enum class CreateResult : int {
	Ok = 0,
	NameTaken = -1,
	Invalid = -2,       ///< invalid input, occupied slot, count limit, job, sex, stats, char_new = no
	Underaged = -3,     ///< client code 0x01; not produced by current rules
	InvalidSlot = -4,
};

struct CreateRequest {
	std::string name;
	Stats stats;
	int slot = 0;
	int hair_style = 0;
	int hair_color = 0;
	Job start_job = Job::Novice;
	std::optional<Sex> client_sex; ///< sent by PACKETVER >= 20151001 clients
};

struct AccountContext {
	AccountId account_id = 0;
	Sex account_sex = Sex::Male;
	int char_slots = kMinChars;
	int existing_char_count = 0;
	std::function<bool(int)> slot_occupied = [](int) { return false; };
	std::function<bool(std::string_view)> name_exists = [](std::string_view) { return false; };
};

/// The character row + inventory rows a successful creation inserts.
struct NewCharacter {
	std::string name;
	AccountId account_id = 0;
	int slot = 0;
	Job job = Job::Novice;
	Sex sex = Sex::Male;
	Stats stats;
	int status_points = 0;
	std::int32_t zeny = 0;
	std::uint32_t max_hp = 0, hp = 0, max_sp = 0, sp = 0;
	int hair_style = 0, hair_color = 0;
	MapPoint last_point;
	MapPoint save_point;
	std::vector<StartItem> items;
};

struct CreateOutcome {
	CreateResult result = CreateResult::Invalid;
	std::optional<NewCharacter> character;
};

/// BR-CHAR-002: slot >= char_slots -> -4; occupied -> -2.
CreateResult check_slot(int slot, int char_slots, bool occupied);
/// BR-CHAR-003: count < char_slots (non-VIP build) or < MAX_CHARS (VIP build).
bool may_create_another(int existing_count, int char_slots, bool vip_build);
/// BR-CHAR-004: PACKETVER >= 20151001 uses the client's M/F, older clients the account sex.
std::optional<Sex> resolve_new_char_sex(int packetver, std::optional<Sex> client_sex, Sex account_sex);
/// BR-CHAR-005: starting stats. Returns the status points granted, or nullopt when invalid.
std::optional<int> validate_start_stats(const Stats& s, int packetver, int start_status_points);
/// BR-CHAR-006
bool is_allowed_start_job(Job job, std::uint8_t allowed_job_flag, int packetver);
/// BR-CHAR-007: HP = 40*(100+VIT)/100, SP = 11*(100+INT)/100.
std::uint32_t initial_max_hp(int vit);
std::uint32_t initial_max_sp(int int_);
/// BR-CHAR-007 start items, bounded by MAX_STARTITEM and stopping at the first nameid 0.
/// [DEFECT fix] legacy loop used `k <= MAX_STARTITEM` (one past the array).
std::vector<StartItem> start_items_for(std::span<const StartItem> configured);
/// BR-CHAR-008: legacy make-char error code -> client refusal byte.
std::uint8_t create_error_to_client_code(CreateResult r);

/// Full creation decision (BR-CHAR-001..008), in legacy check order.
CreateOutcome make_new_char(const CreateRequest& req, const AccountContext& acc,
                            const CreationConfig& ccfg, const NameConfig& ncfg, Rng& rng);

// ---- BR-CHAR-009..015 deletion ------------------------------------------------------------------
/// CH_DELETE_CHAR3_RESERVED ack codes (char_clif.cpp:588-663).
enum class DeleteRequestResult : std::uint8_t {
	AlreadyQueued = 0,
	Queued = 1,
	DatabaseOrNotFound = 3,
	InGuild = 4,
	InParty = 5,
};
struct DeleteRequestOutcome {
	DeleteRequestResult result = DeleteRequestResult::DatabaseOrNotFound;
	UnixTime delete_date = 0;
};
DeleteRequestOutcome request_deletion(const CharacterSummary& c, bool owned_by_session, const DeletionConfig& cfg,
                                      UnixTime now);

/// BR-CHAR-010: birthdate stored "YYYY-MM-DD"; client sends YYMMDD.
bool birthdate_code_matches(std::string_view stored_birthdate, std::string_view yymmdd);
/// BR-CHAR-011: legacy delete-by-code per char_del_option.
bool delete_code_matches(std::string_view code, std::string_view account_email, std::string_view stored_birthdate,
                         std::uint8_t option, bool code_is_birthdate_form);

/// BR-CHAR-012 (char.cpp e_char_del_response).
enum class DeleteGate : std::uint8_t {
	Ok = 0,
	Database = 1,
	NotFound = 2,
	BaseLevel = 3,
	Guild = 4,
	Party = 5,
	Time = 6,
};
DeleteGate check_delete_gate(const CharacterSummary& c, bool owned_by_session, const DeletionConfig& cfg,
                             UnixTime now);

/// BR-CHAR-013: ordered cleanup plan executed by the persistence layer in one transaction.
/// Order follows legacy char_delete (char.cpp:1615-1751); the M3b FK cascade is verified against it.
enum class CleanupStep : std::uint8_t {
	Divorce,
	DeAdoptFromParents,
	LeaveParty,
	DeletePets,
	DeleteHomunculus,
	DeleteElemental,
	DeleteMercenaryOwner,
	DeleteFriendsBothWays,
	DeleteHotkeys,
	DeleteInventory,
	DeleteCart,
	DeleteMemos,
	DeleteRegistries,
	DeleteSkills,
	DeleteReceivedMail,
	AnonymizeSentMail,
	DeleteStatusChanges,
	DeleteBonusScripts,
	DeleteQuests,
	DeleteAchievements,
	DeleteCharacterRow,
	BreakGuild,
	LeaveGuild,
};
std::vector<CleanupStep> deletion_cleanup_plan(const CharacterSummary& c, bool is_guild_master);

/// BR-CHAR-014: only the owner may cancel; sets delete_date to 0.
std::optional<UnixTime> cancel_deletion(const CharacterSummary& c, bool owned_by_session);

// ---- BR-CHAR-015 / 016 selection & character ban -------------------------------------------------
enum class SelectResult : std::uint8_t { Ok, QueuedForDeletion, Banned };
SelectResult check_selectable(const CharacterSummary& c, UnixTime now);

struct CharBanOutcome {
	bool applied = false;     ///< false: ignored (reduce a non-banned char)
	UnixTime unban_time = 0;
	bool kick = false;        ///< active ban -> disconnect + notify map (BR-INTER-019)
};
/// Ban adjusts from max(now, current); result below now -> 0; active -> kick.
CharBanOutcome adjust_char_ban(UnixTime current_unban, std::int64_t delta_seconds, UnixTime now);

// ---- BR-CHAR-017 rename / BR-CHAR-018 slot move -------------------------------------------------
/// Legacy rename result codes (char.cpp:1267-1332).
enum class RenameResult : std::uint8_t {
	Ok = 0,
	NoRenamesLeft = 1,
	NotReady = 2,
	Database = 3,
	NameTaken = 4,
	InGuild = 5,
	InParty = 6,
	InvalidName = 8,
};
struct RenameOutcome {
	RenameResult result = RenameResult::NotReady;
	int remaining = 0;
	std::string new_name;
};
RenameOutcome rename_char(const CharacterSummary& c, std::string_view requested, const RenameConfig& rcfg,
                          const NameConfig& ncfg, const std::function<bool(std::string_view)>& name_exists);

struct SlotMoveOutcome {
	bool ok = false;
	bool swapped = false;
	int remaining_moves = 0;
};
/// `occupant_of(slot)` returns the char id in a slot or 0.
SlotMoveOutcome move_char_slot(int from, int to, int char_slots, int moves_left, const SlotMoveConfig& cfg,
                               const std::function<CharId(int)>& occupant_of);

// ---- BR-CHAR-052 sex change / BR-CHAR-053 fame lists ---------------------------------------------
Job gendered_counterpart(Job job);
struct SexChangeOutcome {
	Job job = Job::Novice;
	Sex sex = Sex::Male;
	CharacterLook look;        ///< all cleared; body = new class
	bool unequip_all = true;
	bool update_guild_member = false;
};
SexChangeOutcome apply_sex_change(Job job, Sex new_sex, GuildId guild_id);

struct FameEntry {
	CharId char_id = 0;
	int fame = 0;
	std::string name;
	friend bool operator==(const FameEntry&, const FameEntry&) = default;
};
/// Top-N by fame descending (N clamped to MAX_FAME_LIST); zero-fame entries are excluded.
std::vector<FameEntry> build_fame_list(std::vector<FameEntry> candidates, std::size_t list_size);

} // namespace rathena::char_rules
