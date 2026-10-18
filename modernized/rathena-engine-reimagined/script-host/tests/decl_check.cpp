// Loads the generated engine.d.luau through Luau::Frontend (the real type
// checker) and type-checks mod snippets against it.
//   luau/typecheck/ok_*.luau   must produce no type errors
//   luau/typecheck/bad_*.luau  must produce at least one type error
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Luau/BuiltinDefinitions.h"
#include "Luau/Config.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/Error.h"

namespace fs = std::filesystem;

static std::string slurp(const std::string& p, bool& ok) {
  std::ifstream f(p, std::ios::binary);
  ok = static_cast<bool>(f);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

struct Resolver : Luau::FileResolver {
  std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override {
    bool ok;
    std::string s = slurp(name, ok);
    if (!ok) return std::nullopt;
    return Luau::SourceCode{s, Luau::SourceCode::Module};
  }
};

int main() {
  Resolver resolver;
  Luau::NullConfigResolver config;
  Luau::FrontendOptions opts;
  Luau::Frontend frontend(&resolver, &config, opts);
  Luau::registerBuiltinGlobals(frontend, frontend.globals, false);

  bool ok;
  std::string defs = slurp(API_DECL_PATH, ok);
  if (!ok) { std::fprintf(stderr, "cannot read %s\n", API_DECL_PATH); return 2; }
  auto loaded = frontend.loadDefinitionFile(frontend.globals, frontend.globals.globalScope, defs, "@engine", false);
  if (!loaded.success) {
    std::fprintf(stderr, "engine.d.luau failed to load:\n");
    for (auto& e : loaded.parseResult.errors) std::fprintf(stderr, "  parse: %s (line %d)\n", e.getMessage().c_str(), e.getLocation().begin.line + 1);
    if (loaded.module)
      for (auto& e : loaded.module->errors) std::fprintf(stderr, "  type: %s\n", Luau::toString(e).c_str());
    return 1;
  }
  Luau::freeze(frontend.globals.globalTypes);
  std::printf("engine.d.luau: loaded\n");

  std::vector<std::string> files;
  if (fs::exists(MOD_SNIPPET_DIR))
    for (auto& e : fs::directory_iterator(MOD_SNIPPET_DIR))
      if (e.path().extension() == ".luau") files.push_back(e.path().string());
  std::sort(files.begin(), files.end());
  int bad = 0;
  for (auto& f : files) {
    std::string base = fs::path(f).filename().string();
    bool expect_errors = base.rfind("bad_", 0) == 0;
    Luau::CheckResult r = frontend.check(f);
    std::printf("%s: %zu type error(s)\n", base.c_str(), r.errors.size());
    for (auto& e : r.errors) std::printf("    %s\n", Luau::toString(e).c_str());
    if (expect_errors == r.errors.empty()) {
      std::fprintf(stderr, "UNEXPECTED: %s should %s\n", base.c_str(), expect_errors ? "fail to type-check" : "type-check cleanly");
      ++bad;
    }
  }
  return bad == 0 ? 0 : 1;
}
