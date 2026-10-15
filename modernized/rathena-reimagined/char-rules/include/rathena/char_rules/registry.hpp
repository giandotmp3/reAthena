// rules/registry: CAP-07 persistence of registries and cooldowns. BR-CHAR-050, 054; BR-INTER-015.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

/// Where a script variable is persisted (spec §2.1 variable scopes).
enum class RegistryScope : std::uint8_t {
	GlobalAccount, ///< "##name" -> login (global_acc_reg_*), relayed through char (BR-LOGIN-036)
	Account,       ///< "#name"  -> char  (acc_reg_*)
	Character,     ///< "name"   -> char  (char_reg_*)
	MapServer,     ///< "$name"  -> map   (mapreg); never stored by char
	Temporary,     ///< "@name", ".name", "'name" -> not persisted
	Invalid,
};

/// BR-CHAR-050 / BR-INTER-015: classify a variable key by prefix.
RegistryScope registry_scope(std::string_view key);
/// Owner process for a persisted scope (BR-INTER-015 routing).
enum class RegistryOwner : std::uint8_t { Login, Char, Map, None };
RegistryOwner registry_owner(RegistryScope s);
bool is_string_variable(std::string_view key); ///< trailing '$'

struct RegistryWrite {
	std::string key;
	std::uint32_t index = 0;
	std::variant<std::int64_t, std::string> value;
};
enum class RegistryOp : std::uint8_t { Upsert, Delete };
/// BR-CHAR-050: a 0 numeric value or an empty string deletes the entry.
RegistryOp registry_op(const RegistryWrite& w);

/// BR-CHAR-054: at most 20 cooldowns are persisted; only positive remaining ticks are kept.
struct SkillCooldown {
	std::uint16_t skill_id = 0;
	std::int64_t remaining_ms = 0;
	friend bool operator==(const SkillCooldown&, const SkillCooldown&) = default;
};
std::vector<SkillCooldown> cooldowns_to_persist(const std::vector<SkillCooldown>& active);

/// Restore-once: rows are handed to the map server once on load and then deleted.
class CooldownStore {
public:
	void save(CharId cid, const std::vector<SkillCooldown>& active);
	std::vector<SkillCooldown> load_once(CharId cid);

private:
	std::vector<std::pair<CharId, std::vector<SkillCooldown>>> rows_;
};

} // namespace rathena::char_rules
