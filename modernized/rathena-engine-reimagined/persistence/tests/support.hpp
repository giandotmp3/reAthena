#pragma once
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "rathena/persist/service.hpp"

namespace rathena::persist::testing_support {

inline bool run_pending() {
	const char* v = std::getenv("RATHENA_PERSIST_RUN_PENDING");
	return v != nullptr && std::string(v) == "1";
}

/// Executable-but-unimplemented acceptance test gate. The body below the macro is a real
/// assertion set; until the service is built it is skipped and tagged with the rule id.
#define PENDING_RULE(rule_id) \
	do { if (!::rathena::persist::testing_support::run_pending()) GTEST_SKIP() << "PENDING " rule_id; } while (0)

/// Fake same-shape URLs only. No credential from legacy code is ever used here.
inline constexpr const char* kFakePgUrl = "postgresql://rathena:fake-db-pass-0000@db.invalid:5432/rathena_engine";
inline constexpr const char* kMemoryUrl = "sqlite::memory:";

inline std::unique_ptr<SqlConnection> memory_db() { return open_connection(kMemoryUrl); }

inline Blob blob(std::string s) { return Blob{std::move(s)}; }
inline OwnerRef character(std::uint64_t id) { return {OwnerScope::Character, id}; }
inline OwnerRef account(std::uint64_t id) { return {OwnerScope::Account, id}; }
inline OwnerRef global_account() { return {OwnerScope::GlobalAccount, 0}; }

} // namespace rathena::persist::testing_support
