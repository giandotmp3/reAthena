#include "rathena/char_rules/party.hpp"

#include <algorithm>
#include <climits>


namespace rathena::char_rules {

namespace {
bool violates_policy(std::string_view name, const NameConfig& ncfg, char* offending) {
	for (const char c : name) {
		const bool listed = ncfg.letters.find(c) != std::string::npos;
		if ((ncfg.policy == NameLetterPolicy::AllowOnly && !listed) || (ncfg.policy == NameLetterPolicy::Forbid && listed)) {
			*offending = c;
			return true;
		}
	}
	return false;
}

std::string collapse_quotes(std::string_view name) {
	std::string out;
	bool pending_space = false;
	for (const char c : name) {
		if (c == '"') {
			pending_space = !out.empty();
			continue;
		}
		if (pending_space) out.push_back(' ');
		pending_space = false;
		out.push_back(c);
	}
	return out;
}
} // namespace

PartyCreateOutcome check_party_create(std::string_view raw, const NameConfig& ncfg,
                                      const std::function<bool(std::string_view)>& name_exists) {
	std::string name{raw.substr(0, std::min(raw.size(), kNameLength - 1))};
	if (name.empty()) return {PartyCreateResult::InvalidName, {}};
	if (name_exists && name_exists(name)) return {PartyCreateResult::NameTaken, name};
	char bad = 0;
	if (violates_policy(name, ncfg, &bad)) {
		if (ncfg.policy == NameLetterPolicy::AllowOnly && bad == '"') {
			return check_party_create(collapse_quotes(name), ncfg, name_exists); // client-special char
		}
		return {PartyCreateResult::InvalidName, {}};
	}
	return {PartyCreateResult::Ok, name};
}

bool party_can_add_member(const Party& p) {
	const auto used = std::count_if(p.members.begin(), p.members.end(), [](const auto& m) { return m.level > 0; });
	return static_cast<std::size_t>(used) < kMaxParty;
}

bool party_even_share_allowed(const Party& p, int share_level, const FamilyInfo& family) {
	int online = 0;
	int min_lv = INT_MAX;
	int max_lv = 0;
	for (const auto& m : p.members) {
		if (m.level <= 0 || !m.online) continue;
		++online;
		min_lv = std::min(min_lv, m.level);
		max_lv = std::max(max_lv, m.level);
	}
	return online < 2 || (max_lv - min_lv) <= share_level || family.is_family;
}

LeaveOutcome party_leave(Party& p, CharId leaving) {
	const auto it = std::find_if(p.members.begin(), p.members.end(), [&](const auto& m) { return m.char_id == leaving; });
	if (it == p.members.end()) return LeaveOutcome::NotMember;
	if (it->leader) {
		p.members.clear();
		return LeaveOutcome::PartyDissolved;
	}
	p.members.erase(it);
	return p.members.empty() ? LeaveOutcome::PartyDissolved : LeaveOutcome::MemberRemoved;
}

bool party_is_orphaned(const Party& p) {
	return std::none_of(p.members.begin(), p.members.end(), [](const auto& m) { return m.account_id != 0; });
}

} // namespace rathena::char_rules
