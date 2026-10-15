// rules/clan: CAP-10 clans (P2). BR-CHAR-051.
#pragma once

#include <vector>

#include "rathena/char_rules/config.hpp"
#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

/// BR-CHAR-051: an offline member whose last login is >= remove_inactive_days ago loses its clan.
/// remove_inactive_days <= 0 disables the cleanup. A never-logged-in character (last_login 0 /
/// NULL) is not removed (legacy `last_login IS NOT NULL`).
bool clan_membership_expired(const CharacterSummary& c, const ClanConfig& cfg, UnixTime now);
std::vector<CharId> clan_cleanup(const std::vector<CharacterSummary>& members, const ClanConfig& cfg, UnixTime now);

} // namespace rathena::char_rules
