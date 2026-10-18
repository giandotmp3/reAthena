#pragma once
#include <memory>
#include <string>

#include "native_defaults/damage_ctx.hpp"
#include "native_defaults/stage_id.hpp"
#include "native_defaults/tables.hpp"

namespace nd {

// A pure function of (ctx, tables, rng). Native and Luau-reference variants must agree bit for bit.
class IStage {
 public:
  virtual ~IStage() = default;
  virtual const StageId& id() const = 0;
  virtual void apply(DamageCtx& ctx, const RulesetTables& tables, IRng& rng) const = 0;
};

// Factory functions for the shipped damage stages. Bodies are stubs (throw NotImplementedError).
std::unique_ptr<IStage> make_weapon_basic_v1();   // native.damage.weapon_basic@1
std::unique_ptr<IStage> make_element_table_v1();  // native.damage.element_table@1
std::unique_ptr<IStage> make_defense_soft_v1();   // native.damage.defense_soft@1

}  // namespace nd
