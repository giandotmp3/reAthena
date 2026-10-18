#include "tooling/sim_coverage.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "tooling/sim_conformance.hpp"
#include "tooling/sim_scenario.hpp"

namespace tooling::sim {

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

bool is_alpha_up(char c) { return c >= 'A' && c <= 'Z'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// BR-<FAMILY>-<digits> tokens of a line, in order.
std::vector<std::string> br_ids(const std::string& line) {
  std::vector<std::string> out;
  size_t i = 0;
  while ((i = line.find("BR-", i)) != std::string::npos) {
    if (i > 0 && (std::isalnum(static_cast<unsigned char>(line[i - 1])) || line[i - 1] == '_')) { i += 3; continue; }
    size_t j = i + 3;
    while (j < line.size() && is_alpha_up(line[j])) ++j;
    if (j == i + 3 || j >= line.size() || line[j] != '-') { i += 3; continue; }
    size_t k = j + 1;
    while (k < line.size() && is_digit(line[k])) ++k;
    if (k == j + 1) { i += 3; continue; }
    out.push_back(line.substr(i, k - i));
    i = k;
  }
  return out;
}

std::string family_of(const std::string& id) {
  auto p = id.rfind('-');
  return p == std::string::npos ? id : id.substr(0, p);
}
int number_of(const std::string& id) {
  auto p = id.rfind('-');
  return p == std::string::npos ? 0 : std::atoi(id.c_str() + p + 1);
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r|");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r|");
  return s.substr(a, b - a + 1);
}

void load_universe(const fs::path& file, CoverageResult& r) {
  std::ifstream in(file);
  if (!in) {
    r.notes.push_back("rule file not found: " + file.string());
    return;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("| BR-", 0) != 0) continue;
    auto ids = br_ids(line.substr(0, line.find('|', 2) == std::string::npos ? line.size() : line.find('|', 2)));
    if (ids.size() != 1) continue;
    auto c2 = line.find('|', 2);
    std::string rest = c2 == std::string::npos ? std::string() : line.substr(c2 + 1);
    std::string title = trim(rest.substr(0, rest.find('|')));
    if (title.size() > 110) title = utf8_truncate(title, 107) + "...";
    RuleInfo& ri = r.rules[ids[0]];
    if (ri.id.empty()) {
      ri.id = ids[0];
      ri.title = title;
      ri.origin = file.filename().string();
    }
  }
}

void add_evidence(CoverageResult& r, const std::string& id, Evidence e) {
  auto it = r.rules.find(id);
  if (it != r.rules.end()) it->second.evidence.push_back(std::move(e));
  else r.other_tags[id].push_back(std::move(e));
}

std::string shell_quote(const std::string& s) {
  std::string o = "'";
  for (char c : s) {
    if (c == '\'') o += "'\\''";
    else o += c;
  }
  return o + "'";
}

std::string exec_capture(const std::string& cmd) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return out;
  std::array<char, 4096> buf;
  size_t n;
  while ((n = fread(buf.data(), 1, buf.size(), p)) > 0) out.append(buf.data(), n);
  pclose(p);
  return out;
}

bool pending_line(const std::string& line) {
  std::string l;
  for (char c : line) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return l.find("pending") != std::string::npos || l.find("xfail") != std::string::npos || l.find("expected-failure") != std::string::npos ||
         l.find("expected failure") != std::string::npos;
}

void scan_file(const fs::path& file, const fs::path& base, CoverageResult& r) {
  std::error_code ec;
  if (fs::file_size(file, ec) > (2u << 20)) return;
  std::ifstream in(file);
  std::string line;
  const bool pending_file = file.filename().string().find("pending") != std::string::npos;
  std::map<std::string, std::pair<int, bool>> seen;  // id -> first line, any non-pending line
  int n = 0;
  while (std::getline(in, line)) {
    ++n;
    if (line.find("BR-") == std::string::npos) continue;
    const bool pend = pending_file || pending_line(line);
    for (auto& id : br_ids(line)) {
      auto& s = seen[id];
      if (s.first == 0) s.first = n;
      if (!pend) s.second = true;
    }
  }
  const std::string rel = fs::relative(file, base, ec).generic_string();
  for (auto& [id, s] : seen) {
    Evidence e;
    e.kind = "static";
    e.source = rel;
    e.name = rel + ":" + std::to_string(s.first);
    e.status = s.second ? "tagged" : "pending";
    add_evidence(r, id, std::move(e));
  }
}

bool skip_dir(const std::string& n) {
  return n == "build" || n == "out" || n == "__pycache__" || n == ".git" || n == "golden" || n == "golden_gen" || n == "node_modules" ||
         n == "3rdparty" || n == "_deps";
}

void scan_tree(const fs::path& dir, const fs::path& base, CoverageResult& r, int depth = 0) {
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    if (e.is_directory(ec)) {
      if (depth < 8 && !skip_dir(e.path().filename().string())) scan_tree(e.path(), base, r, depth + 1);
    } else if (e.is_regular_file(ec)) {
      const std::string ext = e.path().extension().string();
      if (ext == ".cpp" || ext == ".hpp" || ext == ".luau" || ext == ".lua" || ext == ".py" || ext == ".tsv") scan_file(e.path(), base, r);
    }
  }
}

}  // namespace

std::string RuleInfo::status() const {
  bool passing = false, failing = false, pending = false, tagged = false;
  for (auto& e : evidence) {
    if (e.status == "passing") passing = true;
    else if (e.status == "failing") failing = true;
    else if (e.status == "pending") pending = true;
    else tagged = true;
  }
  if (failing) return "failing";
  if (passing) return "passing";
  if (pending) return "pending";
  if (tagged) return "tagged";
  return "uncovered";
}

std::vector<std::pair<std::string, Evidence>> parse_test_output(const std::string& text, const std::string& source) {
  std::vector<std::pair<std::string, Evidence>> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string a, b, c;
    ls >> a >> b >> c;
    if (a.empty() || b.empty()) continue;
    std::string status, rule, name;
    if (a == "PASS" || a == "FAIL" || a == "XFAIL" || a == "XPASS" || a == "SKIP") {
      rule = b;
      name = c;
      status = a == "PASS" ? "passing" : (a == "XFAIL" ? "pending" : (a == "SKIP" ? "tagged" : "failing"));
    } else if (b == "ACTIVE" || b == "PENDING") {  // `--list` form: RULE ACTIVE|PENDING name
      rule = a;
      name = c;
      status = b == "PENDING" ? "pending" : "tagged";
    } else {
      continue;
    }
    if (br_ids(rule).empty()) {
      // local tags (TL-*, EC-*) are kept as other tags
    }
    Evidence e;
    e.kind = "test-output";
    e.source = source;
    e.name = name;
    e.status = status;
    out.emplace_back(rule, std::move(e));
  }
  return out;
}

CoverageResult build_coverage(const CoverageOptions& in_opt) {
  CoverageOptions opt = in_opt;
  if (opt.repo_root.empty()) opt.repo_root = repo_root();
  if (opt.engine_root.empty()) opt.engine_root = fs::path(TOOLING_SOURCE_DIR) / "..";
  opt.engine_root = fs::weakly_canonical(opt.engine_root);
  if (opt.rule_files.empty()) {
    for (const char* f : {"BR_RULESET.md", "AI_NATIVE_SPEC.md", "AI_NATIVE_SPEC_ENGINE.md"}) opt.rule_files.push_back(opt.repo_root / "analysis" / "rathena" / f);
  }
  if (opt.scenario_dirs.empty()) opt.scenario_dirs.push_back(opt.engine_root / "tooling" / "scenarios");
  CoverageResult r;
  for (auto& f : opt.rule_files) load_universe(f, r);
  for (auto& [id, ri] : r.rules) r.family_sizes[family_of(id)]++;
  if (r.rules.empty()) r.notes.push_back("no rules were read: the universe is empty");

  // ---- scenarios (run) -----------------------------------------------------------------------------
  for (auto& dir : opt.scenario_dirs) {
    for (auto& file : list_scenarios(dir.string())) {
      const std::string stem = fs::path(file).stem().string();
      ScenarioSpec spec;
      try {
        spec = parse_scenario(file);
      } catch (const std::exception& e) {
        r.notes.push_back("scenario " + stem + " could not be parsed: " + e.what());
        continue;
      }
      if (spec.rule_tags.empty()) continue;
      std::string status = "tagged";
      std::vector<std::string> tags = spec.rule_tags;
      if (opt.run_scenarios) {
        ScenarioResult res = run_scenario_spec(spec);
        status = res.passed ? "passing" : "failing";
        for (auto& t : res.rule_tags)
          if (std::find(tags.begin(), tags.end(), t) == tags.end()) tags.push_back(t);
        if (!res.passed) r.notes.push_back("scenario " + stem + " FAILED: " + utf8_truncate(res.message, 200));
      }
      for (auto& t : tags) {
        Evidence e;
        e.kind = "scenario";
        e.source = "scenarios/" + fs::path(file).filename().string();
        e.name = stem;
        e.status = status;
        add_evidence(r, t, std::move(e));
      }
    }
  }
  r.notes.push_back(opt.run_scenarios ? "scenarios were executed" : "scenarios were NOT executed (statically tagged only)");

  // ---- ruleset suites under the sim host --------------------------------------------------------------
  if (opt.run_suites) {
    for (const char* rs : {"prerenewal", "renewal"}) {
      ConformanceOptions co;
      co.ruleset = rs;
      co.engine_root = opt.engine_root;
      co.golden = true;
      co.suite = true;
      co.seed = opt.seed;
      ConformanceReport cr = run_conformance(co);
      if (!cr.error.empty()) {
        r.notes.push_back(std::string("ruleset suite ") + rs + " failed to run: " + cr.error);
        continue;
      }
      for (auto& t : cr.suite_tests) {
        Evidence e;
        e.kind = "ruleset-suite";
        e.source = "ruleset-" + std::string(rs) + " tests/main under the sim host";
        e.name = t.title;
        e.status = t.status == "PASS" ? "passing" : (t.status == "XFAIL" ? "pending" : "failing");
        add_evidence(r, t.rule, std::move(e));
      }
      Evidence g;
      g.kind = "ruleset-golden";
      g.source = "ruleset-" + std::string(rs) + " golden replay under the sim host";
      g.name = std::to_string(cr.golden_matched) + "/" + std::to_string(cr.golden_total) + " legacy cases";
      g.status = cr.golden_matched == cr.golden_total && cr.golden_total ? "passing" : "failing";
      add_evidence(r, "BR-RULESET-119", std::move(g));
    }
    r.notes.push_back("ruleset suites and golden replays were executed under the sim host");
  } else {
    r.notes.push_back("ruleset suites were NOT executed (pass --suites)");
  }

  // ---- test binaries / saved outputs -----------------------------------------------------------------------
  auto ingest = [&](const std::string& text, const std::string& source) {
    for (auto& [rule, ev] : parse_test_output(text, source)) add_evidence(r, rule, ev);
  };
  for (auto& b : opt.test_binaries) {
    ingest(exec_capture(shell_quote(b.string()) + " 2>&1"), fs::path(b).filename().string());
    r.notes.push_back("test binary executed: " + b.string());
  }
  for (auto& f : opt.test_outputs) {
    ingest(slurp(f), f.filename().string());
    r.notes.push_back("test output read: " + f.string());
  }

  // ---- static scan of every unit's tests ---------------------------------------------------------------------
  if (opt.static_scan) {
    std::error_code ec;
    for (auto& u : fs::directory_iterator(opt.engine_root, ec)) {
      if (!u.is_directory(ec) || skip_dir(u.path().filename().string())) continue;
      for (const char* sub : {"tests", "conformance", "rules", "scenarios"}) {
        fs::path d = u.path() / sub;
        if (fs::is_directory(d, ec)) scan_tree(d, opt.engine_root, r);
      }
    }
    r.notes.push_back("static tag scan of tests/, conformance/, rules/ and scenarios/ of every unit (tags are not executed)");
  }
  // dedupe identical evidence (the same scenario tagged and run)
  auto dedupe = [](std::vector<Evidence>& v) {
    std::vector<Evidence> out;
    for (auto& e : v) {
      bool dup = false;
      for (auto& o : out) dup = dup || (o.kind == e.kind && o.source == e.source && o.name == e.name && o.status == e.status);
      if (!dup) out.push_back(e);
    }
    v = std::move(out);
  };
  for (auto& [id, ri] : r.rules) dedupe(ri.evidence);
  for (auto& [id, v] : r.other_tags) dedupe(v);
  return r;
}

Val to_val(const CoverageResult& r) {
  VMap m;
  m["schema"] = 1;
  VMap fam;
  for (auto& [f, n] : r.family_sizes) fam[f] = static_cast<double>(n);
  fam["total"] = static_cast<double>(r.rules.size());
  m["universe"] = Val(std::move(fam));
  std::map<std::string, double> counts = {{"passing", 0}, {"failing", 0}, {"pending", 0}, {"tagged", 0}, {"uncovered", 0}};
  VArr rules, uncovered;
  for (auto& [id, ri] : r.rules) {
    const std::string st = ri.status();
    counts[st] += 1;
    VMap rm;
    rm["id"] = id;
    rm["title"] = ri.title;
    rm["source"] = ri.origin;
    rm["status"] = st;
    VArr ev;
    for (auto& e : ri.evidence) {
      VMap em;
      em["kind"] = e.kind;
      em["source"] = e.source;
      em["name"] = e.name;
      em["status"] = e.status;
      ev.push_back(Val(std::move(em)));
    }
    rm["covered_by"] = Val(std::move(ev));
    rules.push_back(Val(std::move(rm)));
    if (st == "uncovered") uncovered.push_back(id);
  }
  VMap sm;
  for (auto& [k, v] : counts) sm[k] = v;
  m["summary"] = Val(std::move(sm));
  m["rules"] = Val(std::move(rules));
  m["uncovered"] = Val(std::move(uncovered));
  VMap other;
  for (auto& [id, v] : r.other_tags) {
    VArr ev;
    for (auto& e : v) {
      VMap em;
      em["kind"] = e.kind;
      em["source"] = e.source;
      em["name"] = e.name;
      em["status"] = e.status;
      ev.push_back(Val(std::move(em)));
    }
    other[id] = Val(std::move(ev));
  }
  m["other_tags"] = Val(std::move(other));
  VArr notes;
  for (auto& n : r.notes) notes.push_back(n);
  m["notes"] = Val(std::move(notes));
  return Val(std::move(m));
}

namespace {
// "BR-LOGIN-001..BR-LOGIN-005, BR-LOGIN-007" style ranges of a sorted id list
std::string ranges(const std::vector<std::string>& ids) {
  std::string out;
  size_t i = 0;
  while (i < ids.size()) {
    size_t j = i;
    while (j + 1 < ids.size() && family_of(ids[j + 1]) == family_of(ids[i]) && number_of(ids[j + 1]) == number_of(ids[j]) + 1) ++j;
    if (!out.empty()) out += ", ";
    out += ids[i];
    if (j > i + 1) out += ".." + ids[j].substr(ids[j].rfind('-') + 1);
    else if (j == i + 1) out += ", " + ids[j];
    i = j + 1;
  }
  return out;
}
}  // namespace

std::string render_text(const CoverageResult& r) {
  std::ostringstream o;
  std::map<std::string, std::map<std::string, size_t>> by_family;
  std::map<std::string, size_t> totals;
  std::vector<std::string> uncovered;
  for (auto& [id, ri] : r.rules) {
    const std::string st = ri.status();
    by_family[family_of(id)][st]++;
    totals[st]++;
    if (st == "uncovered") uncovered.push_back(id);
  }
  o << "BR rule coverage: " << r.rules.size() << " rules\n";
  o << "  passing " << totals["passing"] << ", failing " << totals["failing"] << ", pending " << totals["pending"] << ", tagged only "
    << totals["tagged"] << ", uncovered " << totals["uncovered"] << "\n\n";
  o << "by family (passing / failing / pending / tagged / uncovered):\n";
  for (auto& [f, c] : by_family) {
    char b[160];
    std::snprintf(b, sizeof b, "  %-12s %4zu rules: %zu / %zu / %zu / %zu / %zu\n", f.c_str(), r.family_sizes.count(f) ? r.family_sizes.at(f) : 0,
                  c.count("passing") ? c.at("passing") : 0, c.count("failing") ? c.at("failing") : 0, c.count("pending") ? c.at("pending") : 0,
                  c.count("tagged") ? c.at("tagged") : 0, c.count("uncovered") ? c.at("uncovered") : 0);
    o << b;
  }
  o << "\nrules with passing or failing evidence, and the tests that cover them:\n";
  for (auto& [id, ri] : r.rules) {
    const std::string st = ri.status();
    if (st != "passing" && st != "failing") continue;
    o << "  " << id << "  " << st << "  " << utf8_truncate(ri.title, 60) << "\n";
    size_t shown = 0, total = 0;
    for (auto& e : ri.evidence) {
      if (e.status != "passing" && e.status != "failing") continue;
      ++total;
      if (shown >= 3) continue;
      ++shown;
      std::string nm = e.name.size() > 90 ? utf8_truncate(e.name, 87) + "..." : e.name;
      o << "      " << e.status << "  " << e.kind << "  " << nm << "  (" << e.source << ")\n";
    }
    if (total > shown) o << "      ... and " << (total - shown) << " more (see --json)\n";
  }
  o << "\nrules with no executed evidence but tagged or pending tests: ";
  size_t n = 0;
  for (auto& [id, ri] : r.rules) {
    const std::string st = ri.status();
    if (st == "pending" || st == "tagged") ++n;
  }
  o << n << " (see --json for the tests)\n";
  o << "\nUNCOVERED (" << uncovered.size() << "): " << (uncovered.empty() ? std::string("none") : ranges(uncovered)) << "\n";
  if (!r.other_tags.empty()) {
    o << "\nlocal (non-universe) tags seen: ";
    size_t k = 0;
    for (auto& [id, v] : r.other_tags) {
      if (k++) o << ", ";
      o << id;
      if (k >= 40) { o << ", ..."; break; }
    }
    o << "\n";
  }
  o << "\nnotes:\n";
  for (auto& n2 : r.notes) o << "  - " << n2 << "\n";
  return o.str();
}

RequireOutcome check_require(const CoverageResult& r, const std::vector<std::string>& specs, const std::string& level) {
  RequireOutcome out;
  std::set<std::string> want;
  for (const auto& raw : specs) {
    std::string spec = trim(raw);
    if (spec.empty()) continue;
    if (spec == "all") {
      for (auto& [id, _] : r.rules) want.insert(id);
      continue;
    }
    auto dots = spec.find("..");
    if (dots != std::string::npos) {
      std::string lo = spec.substr(0, dots), hi = spec.substr(dots + 2);
      const std::string fam = family_of(lo);
      if (hi.find('-') == std::string::npos) hi = fam + "-" + hi;
      if (family_of(hi) != fam) { out.error = "range spans two families: " + spec; out.ok = false; return out; }
      int a = number_of(lo), b = number_of(hi);
      bool any = false;
      for (auto& [id, _] : r.rules)
        if (family_of(id) == fam && number_of(id) >= a && number_of(id) <= b) { want.insert(id); any = true; }
      if (!any) { out.error = "no rule matches " + spec; out.ok = false; return out; }
      continue;
    }
    if (r.rules.count(spec)) { want.insert(spec); continue; }
    bool any = false;
    for (auto& [id, _] : r.rules)
      if (family_of(id) == spec) { want.insert(id); any = true; }
    if (!any) { out.error = "unknown rule or family: " + spec; out.ok = false; return out; }
  }
  for (auto& id : want) {
    const std::string st = r.rules.at(id).status();
    const bool good = level == "covered" ? st != "uncovered" : st == "passing";
    if (!good) out.failing.push_back(id);
  }
  out.ok = out.failing.empty();
  return out;
}

}  // namespace tooling::sim
