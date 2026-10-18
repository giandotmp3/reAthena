#include <cstdio>
#include <cstring>

#include "mini_test.hpp"

int main(int argc, char** argv) {
  bool list = argc > 1 && std::strcmp(argv[1], "--list-pending") == 0;
  int pass = 0, xfail = 0, fail = 0;
  for (auto& c : mt::registry()) {
    if (list) {
      if (c.expected_failure) std::printf("%s %s (%s)\n", c.tag.c_str(), c.name.c_str(), c.reason.c_str());
      continue;
    }
    bool threw = false;
    std::string what;
    try { c.fn(); } catch (const std::exception& e) { threw = true; what = e.what(); }
    if (c.expected_failure) {
      if (threw) { ++xfail; std::printf("XFAIL [%s] %s\n", c.tag.c_str(), c.name.c_str()); }
      else { ++fail; std::printf("XPASS [%s] %s: now passes, remove PENDING\n", c.tag.c_str(), c.name.c_str()); }
    } else if (threw) {
      ++fail; std::printf("FAIL  [%s] %s: %s\n", c.tag.c_str(), c.name.c_str(), what.c_str());
    } else { ++pass; std::printf("PASS  [%s] %s\n", c.tag.c_str(), c.name.c_str()); }
  }
  if (!list) std::printf("passed=%d expected-failures=%d failed=%d\n", pass, xfail, fail);
  return fail == 0 ? 0 : 1;
}
