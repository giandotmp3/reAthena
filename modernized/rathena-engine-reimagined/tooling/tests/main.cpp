#include <cstring>
#include <iostream>

#include "harness.hpp"

std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

int main(int argc, char** argv) {
  bool list = false;
  std::string only;  // --only <substring of "RULE name">
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--list") == 0) list = true;
    else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = argv[++i];
  }
  int pass = 0, fail = 0, xfail = 0, xpass = 0, skip = 0;
  for (const auto& t : registry()) {
    if (!only.empty() && (std::string(t.rule) + " " + t.name).find(only) == std::string::npos) continue;
    if (list) {
      std::cout << t.rule << (t.pending ? " PENDING " : " ACTIVE  ") << t.name << "\n";
      continue;
    }
    bool ok = true;
    std::string why;
    try {
      t.fn();
    } catch (const SkipTest& e) {
      ++skip;
      std::cout << "SKIP   " << t.rule << " " << t.name << " (" << e.what() << ")\n";
      continue;
    } catch (const std::exception& e) {
      ok = false;
      why = e.what();
    }
    if (!t.pending) {
      if (ok) { ++pass; std::cout << "PASS   " << t.rule << " " << t.name << "\n"; }
      else { ++fail; std::cout << "FAIL   " << t.rule << " " << t.name << ": " << why << "\n"; }
    } else {
      if (!ok) { ++xfail; std::cout << "XFAIL  " << t.rule << " " << t.name << " (" << why << ")\n"; }
      else { ++xpass; std::cout << "XPASS  " << t.rule << " " << t.name
                                << ": implemented, remove PENDING marker\n"; }
    }
  }
  if (list) return 0;
  std::cout << "\n" << pass << " passed, " << fail << " failed, " << xfail
            << " expected-failures, " << xpass << " unexpected passes, " << skip << " skipped\n";
  return (fail || xpass) ? 1 : 0;
}
