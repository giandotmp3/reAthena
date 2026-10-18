#include "native_defaults/not_implemented.hpp"
#include "native_defaults/stage.hpp"

namespace nd {
namespace {

class StubStage final : public IStage {
 public:
  StubStage(const char* name, const char* label) : id_{name, 1}, label_(label) {}
  const StageId& id() const override { return id_; }
  void apply(DamageCtx&, const RulesetTables&, IRng&) const override { throw NotImplementedError(label_); }

 private:
  StageId id_;
  const char* label_;
};

}  // namespace

// TODO(BR-RULESET-111, BR-RULESET-018/019): base attack, size fix, variance roll.
std::unique_ptr<IStage> make_weapon_basic_v1() {
  return std::make_unique<StubStage>("native.damage.weapon_basic", "native.damage.weapon_basic@1");
}
// TODO(BR-RULESET-031, BR-MAP-017): element table, status modifiers PRE/RE.
std::unique_ptr<IStage> make_element_table_v1() {
  return std::make_unique<StubStage>("native.damage.element_table", "native.damage.element_table@1");
}
// TODO(BR-MAP-014/015, BR-RULESET-025/045): soft/hard defense, RES, MDEF.
std::unique_ptr<IStage> make_defense_soft_v1() {
  return std::make_unique<StubStage>("native.damage.defense_soft", "native.damage.defense_soft@1");
}

}  // namespace nd
