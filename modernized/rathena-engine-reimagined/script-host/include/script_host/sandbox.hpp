#pragma once
#include <cstdint>
#include <set>
#include <string>

namespace script_host {

enum class Permission { StorageAccount, StorageReadOther, HttpRoute, UnboundedLoop, Atcommand, NetRaw };

struct ModEnvironmentSpec {
  std::string mod_id;
  std::set<Permission> granted;
  std::uint64_t memory_cap_bytes = 32ull << 20;
};

enum class RunStatus { Ok, Error, BudgetExceeded, MemoryExceeded, PermissionDenied };

struct RunResult {
  RunStatus status = RunStatus::Ok;
  std::string error;  // includes mod id, hook id, trace
};

// Per-mod allocator accounting. Exceeding the cap disables the mod.
class MemoryAccountant {
 public:
  void set_cap(const std::string& mod, std::uint64_t bytes);
  // Returns false (and marks the mod disabled) if the charge would exceed the cap.
  bool charge(const std::string& mod, std::uint64_t bytes);
  void release(const std::string& mod, std::uint64_t bytes);
  std::uint64_t used(const std::string& mod) const;
  bool disabled(const std::string& mod) const;
};

// Sandboxed Luau environment for one mod.
class ModSandbox {
 public:
  explicit ModSandbox(ModEnvironmentSpec spec) : spec_(std::move(spec)) {}
  // Runs source in the mod's environment: no os/io/debug, arbitrary require,
  // frozen shared globals, bounded string/table natives.
  RunResult run(const std::string& chunk_name, const std::string& source);
  const ModEnvironmentSpec& spec() const { return spec_; }

 private:
  ModEnvironmentSpec spec_;
};

}  // namespace script_host
