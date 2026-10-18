#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "native_defaults/stage.hpp"

namespace nd {

// The Luau reference implementation, supplied by the ruleset/script host.
using ReferenceFn = std::function<void(DamageCtx&, const RulesetTables&, IRng&)>;

enum class ResolvedPath { Native, LuauReference };

struct Resolution {
  ResolvedPath path;
  const IStage* native = nullptr;     // set when path == Native
  const ReferenceFn* reference = nullptr;  // set when path == LuauReference
};

struct RegistryError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Opt-in registry. A stage is runnable only if registered by ID with a Luau reference
// (config require_reference). Resolution: hooked stage -> reference, else native.
class Registry {
 public:
  explicit Registry(bool require_reference = true) : require_reference_(require_reference) {}

  void register_native(std::unique_ptr<IStage> stage);
  void register_reference(const StageId& id, ReferenceFn fn);

  // Throws RegistryError for unknown id/version, or native without reference when required.
  Resolution resolve(const StageId& id, bool has_script_hook) const;

  bool has_native(const StageId& id) const { return natives_.count(id) != 0; }

 private:
  bool require_reference_;
  std::map<StageId, std::unique_ptr<IStage>> natives_;
  std::map<StageId, ReferenceFn> references_;
};

}  // namespace nd
