#pragma once
// Minimal acceptance-test harness (no external dependencies).
//   TEST("MOD-XXX-001", name) { ... }          must pass
//   PENDING_TEST("MOD-XXX-002", name) { ... }  expected-failure: body encodes the
//     real expectation; it must currently fail (typically NotImplemented). If
//     it unexpectedly passes the run fails, so the marker gets removed.
// `--list` prints rule tags and status for coverage reporting.
#include <sstream>
#include "mods/common.hpp"
#include <stdexcept>
#include <string>
#include <vector>

struct TestCase {
  const char* rule;
  const char* name;
  bool pending;
  void (*fn)();
};
std::vector<TestCase>& registry();
struct Registrar {
  Registrar(const char* r, const char* n, bool p, void (*f)()) {
    registry().push_back({r, n, p, f});
  }
};

struct CheckFailure : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// Thrown by tests that need something absent from this build (e.g. the Luau
// VM). Reported as SKIP; a skipped PENDING test is not an expected-failure.
struct SkipTest : std::runtime_error {
  using std::runtime_error::runtime_error;
};
#define SKIP_TEST(why) throw SkipTest(why)

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::ostringstream o_;                                                 \
      o_ << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond;          \
      throw CheckFailure(o_.str());                                          \
    }                                                                        \
  } while (0)

#define DEFINE_TEST_(rule, name, pending)                                    \
  static void name();                                                        \
  static Registrar reg_##name(rule, #name, pending, &name);                  \
  static void name()
#define TEST(rule, name) DEFINE_TEST_(rule, name, false)
#define PENDING_TEST(rule, name) DEFINE_TEST_(rule, name, true)
