#pragma once
// Mod loader facade: resolve -> authorize -> register ids -> activate, plus
// kill switch and observability (architecture 5 Observability, 3.1).
//
// The loader is independent of the script VM. Activation goes through the
// ModActivator seam; mods/host_runtime.hpp provides the implementation on top
// of script-host's ReloadManager and ModSandbox.
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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

// A definition a mod declares (from its data or `definitions.declare`).
struct DefinitionDecl {
  NamespacedId id;
  std::optional<std::uint32_t> client_id;  // explicit number, else allocated
};

struct LoadRequest {
  std::vector<Manifest> manifests;
  ModsLock lock;
  SignatureVerifier verifier;     // required when the lock needs signatures
  ResolveOptions resolve;
  // Optional inputs. A mod without an entry has no scripts / a derived digest.
  std::map<std::string, std::filesystem::path> packages;  // mod id -> package directory
  std::map<std::string, std::string> digests;             // mod id -> "sha256:<hex>"
  std::vector<DefinitionDecl> definitions;
};

// What the activator gets for each mod, in load order.
struct ActivationInfo {
  const Manifest* manifest = nullptr;
  std::set<Permission> granted;          // effective grants (requested AND granted)
  std::filesystem::path package_dir;     // empty = nothing to run
  std::string digest;
};

// A generation under construction. Destroying it without commit() abandons it.
class ActivationSession {
public:
  virtual ~ActivationSession() = default;
  virtual void activate(const ActivationInfo& info) = 0;  // throws LoadError
  // A mod that is switched off in this generation: record what is needed to
  // enable it later, run none of its code.
  virtual void skip(const ActivationInfo&) {}
  virtual void commit() = 0;                              // makes the generation live
};

struct ActivatorStats {
  std::uint64_t memory_used = 0, memory_peak = 0;
  std::uint64_t hook_calls = 0, hook_errors = 0;
  std::chrono::microseconds hook_time{0};
};

class ModActivator {
public:
  virtual ~ModActivator() = default;
  virtual std::unique_ptr<ActivationSession> begin() = 0;
  // Kill switch: stop the mod's hooks and timers now. Returns a note for the
  // operator (empty if everything was stopped).
  virtual std::string deactivate(const std::string& mod_id) = 0;
  virtual void reactivate(const std::string& mod_id) = 0;
  virtual ActivatorStats stats(const std::string& mod_id) const = 0;
};

// Per-mod counters (observability). Hook figures come from the activator.
struct ModStats {
  std::chrono::microseconds load_time{0};  // activation of this mod in the last generation
  std::uint64_t memory_used = 0, memory_peak = 0;
  std::uint64_t hook_calls = 0, hook_errors = 0;
  std::chrono::microseconds hook_time{0};
  unsigned loaded_in_generation = 0;
};

// Stable digest used when the caller supplies none: SHA-256 over the
// manifest's identifying fields ("sha256:<hex>"). Not a content hash.
std::string manifest_fingerprint(const Manifest& m);

class ModLoader {
public:
  ModLoader(IdMap& ids, ModObserver* observer = nullptr);

  void set_activator(ModActivator* a) { activator_ = a; }

  // Builds generation N+1 from `req`. All-or-nothing: on any error the
  // previous generation stays active (ids, activation, state) and the error
  // is rethrown (architecture 3.1 "Reload fails to load"). Mods disabled with
  // `disable` stay disabled across generations and their code is not run.
  void load(const LoadRequest& req);

  // Operator kill switch (`mod disable <id>`). Dependents (hard `depends`) are
  // disabled too. Idempotent. Throws LoadError{"ops.unknown_mod"}.
  void disable(const std::string& mod_id);
  void enable(const std::string& mod_id);

  // Active, Disabled, or Invalid for an id that is not in the generation.
  ModState state(const std::string& mod_id) const;
  // Why a mod is disabled ("operator" or "dependency <id> is disabled"); empty otherwise.
  std::string disabled_reason(const std::string& mod_id) const;
  std::vector<std::string> load_order() const;  // active generation
  unsigned generation() const noexcept { return generation_; }
  ModStats stats(const std::string& mod_id) const;

private:
  void recompute_cascade();
  void emit(ModEvent::Kind k, const std::string& mod, const std::string& detail = {},
            std::chrono::microseconds el = {}) const;

  IdMap& ids_;
  ModObserver* observer_;
  ModActivator* activator_ = nullptr;
  unsigned generation_ = 0;
  std::vector<Manifest> active_;
  std::set<std::string> disabled_;               // operator kill switches (survive reloads)
  std::map<std::string, std::string> cascaded_;  // mod -> blocking dependency
  std::map<std::string, ModStats> stats_;
};

}  // namespace mods
