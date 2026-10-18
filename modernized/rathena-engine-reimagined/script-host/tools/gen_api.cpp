// gen_api: api/engine.yaml -> engine.d.luau and C++ binding tables.
//   gen_api schema.yaml [--luau out.d.luau] [--cpp out.hpp] [--check]
// With --check nothing is written; exit 1 if an output file differs.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "script_host/api_schema.hpp"
#include "script_host/not_implemented.hpp"

static bool read_file(const std::string& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream o;
  o << f.rdbuf();
  out = o.str();
  return true;
}

int main(int argc, char** argv) {
  std::string schema, luau, cpp;
  bool check = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--luau") && i + 1 < argc) luau = argv[++i];
    else if (!std::strcmp(argv[i], "--cpp") && i + 1 < argc) cpp = argv[++i];
    else if (!std::strcmp(argv[i], "--check")) check = true;
    else if (schema.empty()) schema = argv[i];
    else { std::fprintf(stderr, "unexpected argument %s\n", argv[i]); return 2; }
  }
  if (schema.empty()) { std::fprintf(stderr, "usage: gen_api schema.yaml [--luau out] [--cpp out] [--check]\n"); return 2; }
  std::string text;
  if (!read_file(schema, text)) { std::fprintf(stderr, "cannot read %s\n", schema.c_str()); return 2; }
  try {
    auto s = script_host::load_api_schema_yaml(text);
    int rc = 0;
    auto emit = [&](const std::string& path, const std::string& content) {
      if (path.empty()) return;
      if (check) {
        std::string cur;
        if (!read_file(path, cur) || cur != content) { std::fprintf(stderr, "%s is out of date\n", path.c_str()); rc = 1; }
        return;
      }
      std::ofstream f(path, std::ios::binary);
      f << content;
    };
    emit(luau, script_host::generate_luau_declarations(s));
    emit(cpp, script_host::generate_cpp_bindings(s));
    return rc;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "gen_api: %s\n", e.what());
    return 1;
  }
}
