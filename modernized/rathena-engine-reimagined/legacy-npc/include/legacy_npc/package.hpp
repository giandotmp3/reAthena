#pragma once
// The `legacy.npc` Luau package (luau/ + mod.toml) as a mod inside script-host: bundling into one chunk and a host that owns the
// sandbox, the storage and the dialogue manager for it. The generic engine loads the same chunk through its mod loader
// (ReloadManager::reload); this class is the unit's own, test-sized version of that.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "script_host/dialogue.hpp"
#include "script_host/reload.hpp"
#include "script_host/sandbox.hpp"
#include "script_host/storage.hpp"

namespace legacy_npc {

inline constexpr const char* kPackageId = "legacy.npc";

// Directory with the package scripts (compile-time path of the source tree).
std::filesystem::path package_dir();

// Concatenates the package modules (luau/*.luau, plus luau/tests/*.luau when with_tests) into ONE chunk. The sandbox has no `require`,
// so each module becomes a function behind a require shim that resolves "./x", "../x" and "x" by file name. The chunk ends with
//   LEGACY = <module legacy>            and, with tests,      LNPC_TESTS = <module all>
std::string bundle_package(const std::filesystem::path& dir, bool with_tests = false);

// Embeds arbitrary bytes as a Luau long string literal.
std::string long_string(const std::string& s);

struct PackageOptions {
  std::set<script_host::Permission> granted;       // host permissions of the sandbox (unbounded_loop lifts its step limit)
  std::uint64_t memory_cap_bytes = 256ull << 20;
  std::uint64_t steps_per_dispatch = 500000000;    // corpus indexing and parsing are CPU work, not hooks: a large step budget
  std::uint64_t steps_per_slice = 500000000;
  bool with_tests = false;
};

class PackageHost {
 public:
  explicit PackageHost(PackageOptions opt = {});
  PackageHost(const PackageHost&) = delete;
  PackageHost& operator=(const PackageHost&) = delete;

  script_host::ModSandbox& sandbox() { return *sandbox_; }
  script_host::StorageApi& storage() { return storage_; }
  script_host::DialogueManager& dialogue() { return *dialogue_; }
  script_host::MemoryStorageBackend& memory_backend() { return *backend_; }
  std::shared_ptr<script_host::ModSandbox> sandbox_ptr() { return sandbox_; }

  // Runs a chunk in the package sandbox; print() output is collected in log().
  script_host::RunResult run(const std::string& name, const std::string& source);
  // run() that throws std::runtime_error with the Luau error text on failure
  void run_ok(const std::string& name, const std::string& source);
  const std::vector<std::string>& log() const { return log_; }
  void clear_log() { log_.clear(); }
  // log lines that start with `prefix`, split at tabs (escapes of dump.luau decoded)
  std::vector<std::vector<std::string>> records(const std::string& prefix) const;

 private:
  PackageOptions opt_;
  std::shared_ptr<script_host::MemoryStorageBackend> backend_;
  script_host::StorageApi storage_;
  std::shared_ptr<script_host::ModSandbox> sandbox_;
  std::unique_ptr<script_host::DialogueManager> dialogue_;
  std::vector<std::string> log_;
};

// Decodes the \\ \t \n \r escapes written by dump.luau.
std::string unescape(const std::string& s);

// Process-wide host used by the facade functions (parse_file, analyze_file, Interpreter). Created on first use.
PackageHost& shared_host();

}  // namespace legacy_npc
