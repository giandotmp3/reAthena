#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "legacy_npc/types.hpp"

namespace legacy_npc {

inline constexpr int kMaxRecompileDepth = 10;  // BR-RULESET-112 recursion limit

struct BonusDelta {
  std::string bonus;        // e.g. "bStr"
  std::vector<Value> args;  // bonus .. bonus5 arity = args.size()
};

struct CompiledDbScript {
  enum class Kind { BonusTable, NeedsInterpreter } kind = Kind::NeedsInterpreter;
  std::vector<BonusDelta> deltas;  // cacheable, no live memory writes
  std::string reason;
};

// Straight-line scripts -> bonus table; control flow falls back. Stub.
CompiledDbScript compile_db_script(std::string_view script);

}  // namespace legacy_npc
