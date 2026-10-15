// rules/party: CAP-08 parties (char side). BR-CHAR-033..036.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/char_rules/config.hpp"
#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

struct PartyMember {
	AccountId account_id = 0;
	CharId char_id = 0;
	std::string name;
	int level = 0;      ///< 0 = empty slot
	bool online = false;
	bool leader = false;
};

struct Party {
	PartyId party_id = 0;
	std::string name;
	bool exp_even_share = false;
	std::vector<PartyMember> members; ///< at most kMaxParty
};

/// Family relation for the even-share exception (party_check_family_share): a 2-member party of
/// parent + child, or a 3-member party forming a family. Supplied by the caller (needs char rows).
struct FamilyInfo {
	bool is_family = false;
};

enum class PartyCreateResult : std::uint8_t { Ok = 0, NameTaken = 1, InvalidName = 2 };
struct PartyCreateOutcome {
	PartyCreateResult result = PartyCreateResult::InvalidName;
	std::string name; ///< possibly normalized (client-special '"' runs collapsed, AllowOnly policy)
};
/// BR-CHAR-033: unique name + char_name_option letter policy (int_party.cpp:505-540).
PartyCreateOutcome check_party_create(std::string_view name, const NameConfig& ncfg,
                                      const std::function<bool(std::string_view)>& name_exists);
/// BR-CHAR-033: a party never exceeds MAX_PARTY (12).
bool party_can_add_member(const Party& p);

/// BR-CHAR-034: even share is allowed if fewer than 2 members are online, the online level gap
/// is <= party_share_level, or the family exception applies.
bool party_even_share_allowed(const Party& p, int party_share_level, const FamilyInfo& family);

enum class LeaveOutcome : std::uint8_t { MemberRemoved, PartyDissolved, NotMember };
/// BR-CHAR-035: if the leader leaves, the party is dissolved.
LeaveOutcome party_leave(Party& p, CharId leaving);

/// BR-CHAR-036: a party with no members (orphan) must be broken.
bool party_is_orphaned(const Party& p);

} // namespace rathena::char_rules
