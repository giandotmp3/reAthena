#include <cstdio>
#include <cstring>

#include "legacy_npc/commands.hpp"
#include "mini_test.hpp"

void register_luau_suites();  // test_luau_suites.cpp: one case per Luau test of luau/tests

int main(int argc, char** argv) {
  bool list = false, print_commands = false;
  std::string tag;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--list-pending")) list = true;
    if (!std::strcmp(argv[i], "--print-commands")) print_commands = true;
    if (!std::strcmp(argv[i], "--tag") && i + 1 < argc) tag = argv[i + 1];
  }
  if (print_commands) {  // regenerates src/supported_commands.inc from the Luau tables
    extern void print_supported_commands();
    print_supported_commands();
    return 0;
  }
  if (!list) register_luau_suites();
  int pass = 0, xfail = 0, fail = 0, xpass = 0, skipped = 0;
  for (auto& c : mt::registry()) {
    if (!tag.empty() && c.tag.rfind(tag, 0) != 0) continue;
    if (list) {
      if (c.expected_failure) std::printf("%s %s (%s)\n", c.tag.c_str(), c.name.c_str(), c.reason.c_str());
      continue;
    }
    bool threw = false;
    std::string what;
    try { c.fn(); }
    catch (const mt::Skip& e) { ++skipped; std::printf("SKIP  [%s] %s: %s\n", c.tag.c_str(), c.name.c_str(), e.what()); continue; }
    catch (const std::exception& e) { threw = true; what = e.what(); }
    if (c.expected_failure) {
      if (threw) { ++xfail; std::printf("XFAIL [%s] %s: %s\n", c.tag.c_str(), c.name.c_str(), c.reason.c_str()); }
      else { ++xpass; ++fail; std::printf("XPASS [%s] %s: now passes, remove PENDING\n", c.tag.c_str(), c.name.c_str()); }
    } else if (threw) {
      ++fail; std::printf("FAIL  [%s] %s: %s\n", c.tag.c_str(), c.name.c_str(), what.c_str());
    } else { ++pass; std::printf("PASS  [%s] %s\n", c.tag.c_str(), c.name.c_str()); }
  }
  if (!list) std::printf("passed=%d expected-failures=%d failed=%d (unexpected-passes=%d) skipped=%d\n", pass, xfail, fail, xpass, skipped);
  return fail == 0 ? 0 : 1;
}
