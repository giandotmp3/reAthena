// EC-LINT-001: the generic core must not contain game vocabulary. Smoke check
// only (architecture section 4); a review rule on numeric literals backs it.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "harness.hpp"

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}
}  // namespace

TEST("EC-LINT-001", engine_sources_contain_no_game_vocabulary) {
  const char* ci_words[] = {"skill", "itemdb", "nameid", "jobid", "status_change",
                            "monster", "weapon", "element_fix"};
  const char* cs_words[] = {"SC_", "SP_", "JOB_", "MOB_"};
  int hits = 0;
  std::string report;
  for (const char* sub : {"include", "src"}) {
    fs::path root = fs::path(ENGINE_CORE_SOURCE_DIR) / sub;
    for (auto& ent : fs::recursive_directory_iterator(root)) {
      if (!ent.is_regular_file()) continue;
      std::ifstream f(ent.path());
      std::stringstream ss;
      ss << f.rdbuf();
      std::string text = ss.str(), lo = lower(text);
      for (auto w : ci_words)
        if (lo.find(w) != std::string::npos) { ++hits; report += ent.path().string() + ":" + w + " "; }
      for (auto w : cs_words)
        if (text.find(w) != std::string::npos) { ++hits; report += ent.path().string() + ":" + w + " "; }
    }
  }
  if (hits) throw CheckFailure("game vocabulary found: " + report);
}
