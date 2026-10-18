#pragma once
// Mod loader facade: resolve -> authorize -> register ids -> activate, plus
// kill switch and observability hooks (architecture 5 Observability, 3.1).
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "mods/id_map.hpp"
#include "mods/lock.hpp"
#include "mods/resolver.hpp"

namespace mods {

enum class ModState { Active, Disabled, Invalid };

struct ModEvent {
  enum class Kind { Loaded, Refused, Disabled, Enabled, GenerationSwapped } kind;
  std::string mod_id;
  std::string detail;  // precise error for Refused
  std::chrono::microseconds elapsed{0};
};

// Observability seam. Implementations must not throw.
class ModObserver {
public:
  virtual ~ModObserver() = default;
  virtual void on_event(const ModEvent&) = 0;
};

struct LoadRequest {
  std::vector<Manifest> manifests;
  ModsLock lock;
  SignatureVerifier verifier;     // required when lock.require_signatures
  ResolveOptions resolve;
};

class ModLoader {
public:
  ModLoader(IdMap& ids, ModObserver* observer = nullptr);

  // Builds generation N+1 from `req`. All-or-nothing: on any error the
  // previous generation stays active and the error is rethrown
  // (architecture 3.1 "Reload fails to load").
  void load(const LoadRequest& req);

  // Operator kill switch (`mod disable <id>`). Marks the mod's definitions
  // Invalid; dependents are disabled too. Idempotent.
  void disable(const std::string& mod_id);
  void enable(const std::string& mod_id);

  ModState state(const std::string& mod_id) const;
  std::vector<std::string> load_order() const;  // active generation
  unsigned generation() const noexcept { return generation_; }

private:
  IdMap& ids_;
  ModObserver* observer_;
  unsigned generation_ = 0;
  std::vector<Manifest> active_;
  std::set<std::string> disabled_;
};

}  // namespace mods
