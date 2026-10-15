#include "rathena/char_rules/guild.hpp"

#include <algorithm>

namespace rathena::char_rules {

GuildCreateResult check_guild_create(std::string_view name, const NameConfig& ncfg,
                                     const std::function<bool(std::string_view)>& name_exists) {
	if (name.empty() || name.size() > kNameLength - 1) return GuildCreateResult::InvalidName;
	for (const char c : name) {
		const bool listed = ncfg.letters.find(c) != std::string::npos;
		if ((ncfg.policy == NameLetterPolicy::AllowOnly && !listed) || (ncfg.policy == NameLetterPolicy::Forbid && listed)) {
			return GuildCreateResult::InvalidName;
		}
	}
	if (name_exists && name_exists(name)) return GuildCreateResult::NameTaken;
	return GuildCreateResult::Ok;
}

Guild new_guild(GuildId id, std::string name, const GuildMember& master) {
	Guild g;
	g.guild_id = id;
	g.name = std::move(name);
	g.level = 1;
	g.max_member = kGuildBaseCapacity;
	GuildMember m = master;
	m.position = 0;
	g.members.push_back(std::move(m));
	for (std::size_t i = 0; i < kMaxGuildPosition; ++i) {
		GuildPosition p;
		p.name = "Position " + std::to_string(i + 1);
		p.mode = i == 0 ? 0x51u : 0u; // master: invite | expel | storage
		g.positions.push_back(std::move(p));
	}
	return g;
}

int guild_skill_level(const Guild& g, int skill_id) {
	for (const auto& s : g.skills) {
		if (s.id == skill_id) return s.lv;
	}
	return 0;
}

int guild_capacity(int extension_level) {
	return std::min(kGuildBaseCapacity + std::max(0, extension_level) * kGuildCapacityPerExtension, kMaxGuild);
}

bool guild_can_add_member(const Guild& g) { return static_cast<int>(g.members.size()) < g.max_member; }

void guild_recalculate(Guild& g, std::span<const std::uint64_t> table) {
	if (g.level <= 0) g.level = 1;
	auto next_for = [&](int lv) -> std::uint64_t {
		const auto idx = static_cast<std::size_t>(lv - 1);
		return idx < table.size() ? table[idx] : 0;
	};
	g.next_exp = next_for(g.level);
	while (g.next_exp > 0 && g.exp >= g.next_exp && g.level < kMaxGuildLevel) {
		g.exp -= g.next_exp;
		++g.level;
		++g.skill_points;
		g.next_exp = next_for(g.level);
	}
	g.max_member = guild_capacity(guild_skill_level(g, kGuildSkillExtension));
}

bool guild_can_skill_up(const Guild& g, int skill_id) {
	if (g.skill_points <= 0) return false;
	const auto it = std::find_if(g.skills.begin(), g.skills.end(), [&](const auto& s) { return s.id == skill_id; });
	return it != g.skills.end() && it->lv < it->max_lv;
}

bool guild_skill_up(Guild& g, int skill_id) {
	if (!guild_can_skill_up(g, skill_id)) return false;
	auto it = std::find_if(g.skills.begin(), g.skills.end(), [&](const auto& s) { return s.id == skill_id; });
	++it->lv;
	--g.skill_points;
	if (skill_id == kGuildSkillExtension) g.max_member = guild_capacity(it->lv);
	return true;
}

bool guild_expel(Guild& g, CharId cid, std::string message) {
	const auto it = std::find_if(g.members.begin(), g.members.end(), [&](const auto& m) { return m.char_id == cid; });
	if (it == g.members.end()) return g.members.empty();
	if (g.expulsions.size() >= kMaxGuildExpulsion) g.expulsions.pop_front();
	g.expulsions.push_back({it->account_id, it->char_id, it->name, std::move(message)});
	g.members.erase(it);
	return g.members.empty();
}

GuildBreakPlan guild_break_plan(const Guild& g) {
	GuildBreakPlan plan;
	plan.guild_id = g.guild_id;
	for (const auto& m : g.members) plan.clear_guild_id_for.push_back(m.char_id);
	for (const auto& a : g.alliances) {
		if (a.kind == AllianceKind::Ally) plan.remove_alliance_from.push_back(a.guild_id);
	}
	return plan;
}

AllianceResult guild_add_alliance(Guild& a, Guild& b, AllianceKind kind) {
	if (a.guild_id == b.guild_id) return AllianceResult::SameGuild;
	const auto linked = [](const Guild& g, GuildId other) {
		return std::any_of(g.alliances.begin(), g.alliances.end(), [&](const auto& x) { return x.guild_id == other; });
	};
	const auto count = [](const Guild& g, AllianceKind k) {
		return static_cast<std::size_t>(
		    std::count_if(g.alliances.begin(), g.alliances.end(), [&](const auto& x) { return x.kind == k; }));
	};
	if (linked(a, b.guild_id)) return AllianceResult::AlreadyLinked;
	if (count(a, kind) >= kMaxGuildAlliance) return AllianceResult::LimitReached;
	if (kind == AllianceKind::Ally) {
		if (linked(b, a.guild_id)) return AllianceResult::AlreadyLinked;
		if (count(b, kind) >= kMaxGuildAlliance) return AllianceResult::LimitReached;
		b.alliances.push_back({a.guild_id, kind, a.name});
	}
	a.alliances.push_back({b.guild_id, kind, b.name});
	return AllianceResult::Ok;
}

bool guild_change_master(Guild& g, CharId new_master, UnixTime now) {
	if (g.members.empty()) return false;
	const auto it = std::find_if(g.members.begin(), g.members.end(), [&](const auto& m) { return m.char_id == new_master; });
	if (it == g.members.end() || it == g.members.begin()) return false;
	std::swap(g.members.front().position, it->position);
	std::iter_swap(g.members.begin(), it);
	g.last_master_change = now;
	return true;
}

int guild_storage_capacity(int lv, bool official) {
	if (!official) return kMaxGuildStorageNonOfficial;
	return lv > 0 ? 100 + lv * 100 : 0;
}

} // namespace rathena::char_rules
