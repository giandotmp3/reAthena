#include "rathena/char_rules/clan.hpp"

namespace rathena::char_rules {

bool clan_membership_expired(const CharacterSummary& c, const ClanConfig& cfg, UnixTime now) {
	if (cfg.remove_inactive_days <= 0) return false;
	if (c.online || c.clan_id == 0 || c.last_login == 0) return false;
	return c.last_login <= now - static_cast<UnixTime>(cfg.remove_inactive_days) * 24 * 60 * 60;
}

std::vector<CharId> clan_cleanup(const std::vector<CharacterSummary>& members, const ClanConfig& cfg, UnixTime now) {
	std::vector<CharId> out;
	for (const auto& c : members) {
		if (clan_membership_expired(c, cfg, now)) out.push_back(c.char_id);
	}
	return out;
}

} // namespace rathena::char_rules
