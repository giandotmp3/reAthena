#include "native_defaults/mob_ai.hpp"

#include "native_defaults/not_implemented.hpp"

namespace nd::mobai {

// TODO(BR-RULESET-113): aggro scan, chase, target change, loot, slave logic, angry/berserk/follow/rush.
Decision tick(const MobView&, const ModeMap&, const std::vector<Nearby>&) {
  throw NotImplementedError("native.mob_ai.default@1");
}

}  // namespace nd::mobai
