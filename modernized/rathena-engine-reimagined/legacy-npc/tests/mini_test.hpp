#pragma once
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mt {
struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct Case {
  std::string tag, name;
  std::function<void()> fn;
  bool expected_failure;
  std::string reason;
};
inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
struct Reg {
  explicit Reg(Case c) { registry().push_back(std::move(c)); }
};
}  // namespace mt

#define CHECK(cond) \
  do { if (!(cond)) throw mt::Failure(std::string(#cond) + " @" + __FILE__ + ":" + std::to_string(__LINE__)); } while (0)

// ACCEPT(rule_id, name): must pass.
#define ACCEPT(tag, name) \
  static void fn_##name(); static mt::Reg reg_##name{{tag, #name, fn_##name, false, ""}}; \
  static void fn_##name()
// PENDING(rule_id, name, reason): expected failure until implemented (XPASS fails the run).
#define PENDING(tag, name, reason) \
  static void fn_##name(); static mt::Reg reg_##name{{tag, #name, fn_##name, true, reason}}; \
  static void fn_##name()
