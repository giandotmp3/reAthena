// Schema reader, generators, and the checked-in engine.d.luau.
#include <cstdlib>

#include "fixtures.hpp"
#include "script_host/api_schema.hpp"
#include "script_host/yaml_lite.hpp"

using namespace script_host;

TEST("CAP-E03", "yaml reader: block maps, block sequences, flow collections, quotes and comments") {
  auto n = yaml::parse(R"(
# top comment
a: 1
list:
  - x
  - { k: v, n: [1, 2, 3] }   # trailing comment
  - key: val
    other: "quoted: text # not a comment"
nested:
  inner:
    deep: yes
empty:
seq_at_key_indent:
- p
- q
)");
  CHECK(n.is_map());
  CHECK_EQ(n.find("a")->scalar, std::string("1"));
  auto* l = n.find("list");
  CHECK_EQ(l->seq.size(), std::size_t(3));
  CHECK_EQ(l->seq[0].scalar, std::string("x"));
  CHECK_EQ(l->seq[1].find("n")->seq.size(), std::size_t(3));
  CHECK_EQ(l->seq[2].find("other")->scalar, std::string("quoted: text # not a comment"));
  CHECK_EQ(n.find("nested")->find("inner")->find("deep")->scalar, std::string("yes"));
  CHECK(n.find("empty")->kind == yaml::Node::Kind::Null);
  CHECK_EQ(n.find("seq_at_key_indent")->seq.size(), std::size_t(2));
}

TEST("CAP-E03", "yaml reader: malformed input is a LoadError with a line number") {
  for (const char* bad : {"a: [1, 2", "a: {x: 1", "a: 'open", "a: 1\na: 2", "\tkey: v", "a:\n    b: 1\n  c: 2", "- x\ny: 1"}) {
    try {
      yaml::parse(bad);
      throw mt::Failure(std::string("accepted: ") + bad);
    } catch (const LoadError& e) {
      CHECK(fx::contains(e.what(), "yaml line"));
    }
  }
}

TEST("CAP-E03", "api schema: the shipped schema loads and every function and method is declared") {
  auto s = load_api_schema_yaml(fx::slurp(API_SCHEMA_PATH));
  CHECK_EQ(s.api_version, 1);
  CHECK(s.functions.size() >= 13);
  auto d = generate_luau_declarations(s);
  for (auto& f : s.functions) CHECK_MSG(d.find(f.name + ":") != std::string::npos || d.find("function " + f.name) != std::string::npos, f.name);
  for (auto& t : s.types) {
    CHECK(d.find(t.name) != std::string::npos);
    for (auto& m : t.methods) CHECK_MSG(d.find("function " + m.name + "(self") != std::string::npos, t.name + ":" + m.name);
  }
  CHECK(d.find("declare function say(") != std::string::npos);   // dialogue is also global
}

TEST("CAP-E03", "api schema: generation is deterministic and the checked-in engine.d.luau is current") {
  auto s = load_api_schema_yaml(fx::slurp(API_SCHEMA_PATH));
  auto a = generate_luau_declarations(s), b = generate_luau_declarations(s);
  CHECK(a == b);
  CHECK_MSG(a == fx::slurp(API_DECL_PATH), "api/engine.d.luau is stale: run the generate_api target");
  CHECK(generate_cpp_bindings(s) == generate_cpp_bindings(s));
}

TEST("CAP-E03", "api schema: bindings table lists every function with its arity") {
  auto s = load_api_schema_yaml(fx::slurp(API_SCHEMA_PATH));
  auto cpp = generate_cpp_bindings(s);
  CHECK(fx::contains(cpp, "{\"events\", \"on\", 3,"));
  CHECK(fx::contains(cpp, "{\"dialogue\", \"choose\", 1, \"number\"}"));
  CHECK(fx::contains(cpp, "kFunctionCount = " + std::to_string(s.functions.size())));
}

TEST("CAP-E03", "api schema: bad schemas are rejected with the reason") {
  auto err = [](const std::string& y) {
    try { load_api_schema_yaml(y); } catch (const LoadError& e) { return std::string(e.what()); }
    return std::string();
  };
  CHECK(fx::contains(err("api_version: 2\n"), "unsupported api_version"));
  CHECK(fx::contains(err("namespaces:\n  a:\n    functions: []\n"), "api_version"));
  CHECK(fx::contains(err("api_version: 1\nnamespaces:\n  a:\n    functions:\n      - { name: f, params: [{ name: x, type: Nope }] }\n"), "undeclared type 'Nope'"));
  CHECK(fx::contains(err("api_version: 1\nnamespaces:\n  a:\n    functions:\n      - { name: f }\n      - { name: f }\n"), "duplicate function a.f"));
  CHECK(fx::contains(err("api_version: 1\ntypes:\n  T: {}\n"), "declares nothing"));
  CHECK(fx::contains(err("api_version: 1\nnamespaces:\n  a:\n    also_global: true\n    functions: []\n"), "no functions"));
  CHECK(err("api_version: 1\nnamespaces:\n  a:\n    functions:\n      - { name: f, params: [{ name: x, type: \"{string}?\" }], returns: \"(number) -> ()\" }\n").empty());
}

TEST("CAP-E03", "api schema: values and the engine table are generated; bad values and unknown engine members are rejected") {
  auto s = load_api_schema_yaml(
      "api_version: 1\nnamespaces:\n  cfg:\n    values:\n      - { name: flag, type: boolean, doc: \"a flag\" }\n  fx:\n    functions:\n      - { name: f, returns: number }\nengine:\n  - cfg\n  - fx\n");
  auto d = generate_luau_declarations(s);
  CHECK(fx::contains(d, "declare cfg: {\n  -- a flag\n  flag: boolean,\n}"));
  CHECK(fx::contains(d, "export type Engine = {\n  cfg: typeof(cfg),\n  fx: typeof(fx),\n}"));
  auto err = [](const std::string& y) {
    try { load_api_schema_yaml(y); } catch (const LoadError& e) { return std::string(e.what()); }
    return std::string();
  };
  CHECK(fx::contains(err("api_version: 1\nnamespaces:\n  cfg:\n    values:\n      - { name: flag, type: Nope }\n"), "value cfg.flag uses undeclared type 'Nope'"));
  CHECK(fx::contains(err("api_version: 1\nnamespaces:\n  cfg:\n    values:\n      - { name: a, type: boolean }\n      - { name: a, type: boolean }\n"), "duplicate value cfg.a"));
  CHECK(fx::contains(err("api_version: 1\nnamespaces:\n  cfg:\n    functions:\n      - { name: a }\n    values:\n      - { name: a, type: boolean }\n"), "both a function and a value"));
  CHECK(fx::contains(err("api_version: 1\nnamespaces:\n  cfg:\n    values:\n      - { name: a, type: boolean }\nengine:\n  - nope\n"), "engine lists unknown namespace 'nope'"));
  CHECK(fx::contains(err("api_version: 1\nnamespaces:\n  cfg:\n    other: 1\n"), "needs a 'functions' list"));
  // the shipped schema exposes the pipeline API, num.f32 and the config table, and builds the engine table from them
  auto shipped = load_api_schema_yaml(fx::slurp(API_SCHEMA_PATH));
  auto has_fn = [&](const std::string& ns, const std::string& name) {
    for (auto& f : shipped.functions) if (f.ns == ns && f.name == name) return true;
    return false;
  };
  for (const char* n : {"define", "stage", "insert_before", "insert_after", "replace", "cancel", "move", "run", "describe", "order"}) CHECK_MSG(has_fn("pipeline", n), n);
  CHECK(has_fn("num", "f32"));
  CHECK(!shipped.values.empty() && shipped.values[0].ns == "config" && shipped.values[0].name == "legacy_quirks");
  CHECK(shipped.engine.size() >= 9);
}

TEST("CAP-E03", "engine.d.luau parses and type-checks mod snippets through Luau::Frontend") {
#ifdef HAVE_DECL_CHECK
  int rc = std::system(("\"" DECL_CHECK_EXE "\" > /dev/null 2>&1"));
  CHECK_MSG(rc == 0, "decl_check failed; run it directly for the diagnostics");
#else
  throw mt::Skip("Luau analysis libraries (Luau.Analysis/Config/EqSat) not found; declarations were not type-checked");
#endif
}
