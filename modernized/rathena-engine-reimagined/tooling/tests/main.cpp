#include <cstring>
#include <iostream>

#include "harness.hpp"

std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

int main(int argc, char** argv) {
  bool list = argc > 1 && std::strcmp(argv[1], "--list") == 0;
  int pass = 0, fail = 0, xfail = 0, xpass = 0;
  for (const auto& t : registry()) {
    if (list) {
      std::cout << t.rule << (t.pending ? " PENDING " : " ACTIVE  ") << t.name << "\n";
      continue;
    }
    bool ok = true;
    std::string why;
    try {
      t.fn();
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
            << " expected-failures, " << xpass << " unexpected passes\n";
  return (fail || xpass) ? 1 : 0;
}
