#pragma once
// Tiny dependency-free test harness with rule-ID tags and strict expected-failure.
//   TEST(rule, name)     must pass
//   PENDING(rule, name)  expected failure until the rule is implemented; XPASS fails the run
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mt {

struct Case {
  std::string rule, name;
  bool pending;
  std::function<void()> fn;
};
inline std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}
struct Reg {
  Reg(const char* rule, const char* name, bool pending, std::function<void()> fn) {
    cases().push_back({rule, name, pending, std::move(fn)});
  }
};
struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

template <class A, class B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
  if (!(a == b)) {
    std::ostringstream o;
    o << file << ":" << line << ": CHECK_EQ(" << ea << ", " << eb << ") got " << a << " vs " << b;
    throw Failure(o.str());
  }
}

inline int run(int argc, char** argv) {
  std::string tag;
  for (int i = 1; i + 1 < argc; ++i)
    if (!std::strcmp(argv[i], "--tag")) tag = argv[i + 1];
  int pass = 0, xfail = 0, fail = 0;
  for (auto& c : cases()) {
    if (!tag.empty() && c.rule != tag) continue;
    bool ok = true;
    std::string why;
    try {
      c.fn();
    } catch (const std::exception& e) {
      ok = false;
      why = e.what();
    }
    if (!c.pending && ok) { ++pass; std::printf("[ PASS  ] %s %s\n", c.rule.c_str(), c.name.c_str()); }
    else if (!c.pending) { ++fail; std::printf("[ FAIL  ] %s %s: %s\n", c.rule.c_str(), c.name.c_str(), why.c_str()); }
    else if (!ok) { ++xfail; std::printf("[ XFAIL ] %s %s\n", c.rule.c_str(), c.name.c_str()); }
    else { ++fail; std::printf("[ XPASS ] %s %s: remove PENDING marker\n", c.rule.c_str(), c.name.c_str()); }
  }
  std::printf("%d passed, %d expected-failures, %d unexpected\n", pass, xfail, fail);
  return fail == 0 ? 0 : 1;
}

}  // namespace mt

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define MT_DEF(rule, name, pending)                                                    \
  static void MT_CAT(mt_fn_, __LINE__)();                                              \
  static mt::Reg MT_CAT(mt_reg_, __LINE__)(rule, name, pending, &MT_CAT(mt_fn_, __LINE__)); \
  static void MT_CAT(mt_fn_, __LINE__)()
#define TEST(rule, name) MT_DEF(rule, name, false)
#define PENDING(rule, name) MT_DEF(rule, name, true)
#define CHECK_EQ(a, b) mt::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK(c) do { if (!(c)) throw mt::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": CHECK(" #c ")"); } while (0)
#define CHECK_THROWS(expr) do { bool t_ = false; try { (void)(expr); } catch (...) { t_ = true; } if (!t_) throw mt::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": expected throw: " #expr); } while (0)
