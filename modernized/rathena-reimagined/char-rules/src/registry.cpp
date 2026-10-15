#include "rathena/char_rules/registry.hpp"

#include <algorithm>

namespace rathena::char_rules {

RegistryScope registry_scope(std::string_view key) {
	if (key.empty()) return RegistryScope::Invalid;
	if (key.starts_with("##")) return key.size() > 2 ? RegistryScope::GlobalAccount : RegistryScope::Invalid;
	switch (key.front()) {
	case '#': return key.size() > 1 ? RegistryScope::Account : RegistryScope::Invalid;
	case '$': return key.size() > 1 ? RegistryScope::MapServer : RegistryScope::Invalid;
	case '@':
	case '.':
	case '\'': return RegistryScope::Temporary;
	default: return RegistryScope::Character;
	}
}

RegistryOwner registry_owner(RegistryScope s) {
	switch (s) {
	case RegistryScope::GlobalAccount: return RegistryOwner::Login;
	case RegistryScope::Account:
	case RegistryScope::Character: return RegistryOwner::Char;
	case RegistryScope::MapServer: return RegistryOwner::Map;
	case RegistryScope::Temporary:
	case RegistryScope::Invalid: break;
	}
	return RegistryOwner::None;
}

bool is_string_variable(std::string_view key) { return !key.empty() && key.back() == '$'; }

RegistryOp registry_op(const RegistryWrite& w) {
	if (const auto* n = std::get_if<std::int64_t>(&w.value)) return *n == 0 ? RegistryOp::Delete : RegistryOp::Upsert;
	return std::get<std::string>(w.value).empty() ? RegistryOp::Delete : RegistryOp::Upsert;
}

std::vector<SkillCooldown> cooldowns_to_persist(const std::vector<SkillCooldown>& active) {
	std::vector<SkillCooldown> out;
	for (const auto& c : active) {
		if (out.size() >= kMaxSkillCooldown) break;
		if (c.remaining_ms > 0) out.push_back(c);
	}
	return out;
}

void CooldownStore::save(CharId cid, const std::vector<SkillCooldown>& active) {
	std::erase_if(rows_, [&](const auto& r) { return r.first == cid; });
	rows_.emplace_back(cid, cooldowns_to_persist(active));
}

std::vector<SkillCooldown> CooldownStore::load_once(CharId cid) {
	const auto it = std::find_if(rows_.begin(), rows_.end(), [&](const auto& r) { return r.first == cid; });
	if (it == rows_.end()) return {};
	auto out = std::move(it->second);
	rows_.erase(it);
	return out;
}

} // namespace rathena::char_rules
