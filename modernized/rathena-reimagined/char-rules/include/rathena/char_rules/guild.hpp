// rules/guild: CAP-09 guilds (char side). BR-CHAR-037..045.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/char_rules/config.hpp"
#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

inline constexpr int kGuildSkillExtension = 10004;    ///< GD_EXTENSION
inline constexpr int kGuildSkillStorage = 10016;      ///< GD_GUILD_STORAGE
inline constexpr std::size_t kMaxGuildPosition = 20;

struct GuildMember {
	AccountId account_id = 0;
	CharId char_id = 0;
	std::string name;
	int position = 0;
};

struct GuildPosition {
	std::string name;
	std::uint32_t mode = 0; ///< bit 0x01 invite, 0x10 expel, 0x40 storage
	int exp_mode = 0;       ///< tax %
};

struct GuildSkill {
	int id = 0;
	int lv = 0;
	int max_lv = 0;
};

struct GuildExpulsion {
	AccountId account_id = 0;
	CharId char_id = 0;
	std::string name;
	std::string message;
};

enum class AllianceKind : std::uint8_t { Ally = 0, Opposition = 1 };
struct GuildAlliance {
	GuildId guild_id = 0;
	AllianceKind kind = AllianceKind::Ally;
	std::string name;
};

struct Guild {
	GuildId guild_id = 0;
	std::string name;
	int level = 1;
	std::uint64_t exp = 0;
	std::uint64_t next_exp = 0;
	int skill_points = 0;
	int max_member = kGuildBaseCapacity;
	std::vector<GuildMember> members;        ///< members[0] is the master
	std::vector<GuildPosition> positions;
	std::vector<GuildSkill> skills;
	std::deque<GuildExpulsion> expulsions;   ///< FIFO, max 32
	std::vector<GuildAlliance> alliances;    ///< max 16
	UnixTime last_master_change = 0;
};

// BR-CHAR-037
enum class GuildCreateResult : std::uint8_t { Ok = 0, NameTaken = 2, InvalidName = 3 };
GuildCreateResult check_guild_create(std::string_view name, const NameConfig& ncfg,
                                     const std::function<bool(std::string_view)>& name_exists);
/// Default positions: 20 entries, "Position 1".."Position 20"; position 0 (master) has full rights.
Guild new_guild(GuildId id, std::string name, const GuildMember& master);

int guild_skill_level(const Guild& g, int skill_id);
/// BR-CHAR-038: 16 + 6 x Extension level, capped at MAX_GUILD (76).
int guild_capacity(int extension_level);
bool guild_can_add_member(const Guild& g);

/// BR-CHAR-039: consume exp while exp >= next and level < 50; +1 skill point per level.
/// `next_exp_table[n-1]` is the exp needed to go from level n to n+1 (db/guild_exp.yml).
void guild_recalculate(Guild& g, std::span<const std::uint64_t> next_exp_table);

/// BR-CHAR-040
bool guild_can_skill_up(const Guild& g, int skill_id);
bool guild_skill_up(Guild& g, int skill_id);

/// BR-CHAR-041: FIFO expulsion list (max 32). Returns true when the guild is now empty and must break.
bool guild_expel(Guild& g, CharId cid, std::string message);

/// BR-CHAR-042: everything a guild break deletes / clears (persistence executes it).
struct GuildBreakPlan {
	GuildId guild_id = 0;
	std::vector<CharId> clear_guild_id_for; ///< char.guild_id -> 0
	std::vector<GuildId> remove_alliance_from; ///< counterpart guilds' alliance rows
	bool delete_guild_row = true, delete_members = true, delete_positions = true, delete_skills = true,
	     delete_expulsions = true, delete_storage = true, delete_storage_log = true, delete_emblem = true,
	     release_castles = true;
};
GuildBreakPlan guild_break_plan(const Guild& g);

/// BR-CHAR-043: alliance is bidirectional (both rows), opposition unidirectional, max 16 each side.
enum class AllianceResult : std::uint8_t { Ok, AlreadyLinked, LimitReached, SameGuild };
AllianceResult guild_add_alliance(Guild& a, Guild& b, AllianceKind kind);

/// BR-CHAR-044: swap the new master into slot 0 (positions swap accordingly) and record the time.
bool guild_change_master(Guild& g, CharId new_master, UnixTime now);

/// BR-CHAR-045: 100 + 100 x GD_GUILD_STORAGE level (0 if unlearned) in official mode, else 600.
int guild_storage_capacity(int storage_skill_level, bool official_mode);

} // namespace rathena::char_rules
