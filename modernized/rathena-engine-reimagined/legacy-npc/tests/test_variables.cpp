#include "legacy_npc/interpreter.hpp"
#include "legacy_npc/package.hpp"
#include "legacy_npc/parser.hpp"
#include "legacy_npc/variables.hpp"

#include "mini_test.hpp"

using namespace legacy_npc;

ACCEPT("LNPC-VAR-001", sigils_map_to_scopes) {
  struct { const char* t; Scope s; } cases[] = {
      {"name", Scope::Character}, {"@name", Scope::CharacterTemp}, {"$name", Scope::World},
      {"$@name", Scope::WorldTemp}, {"#name", Scope::Account}, {"##name", Scope::AccountGlobal},
      {".name", Scope::Npc}, {".@name", Scope::CallFrame}, {"'name", Scope::Instance}};
  for (auto& c : cases) {
    auto v = parse_var(c.t);
    CHECK(v.has_value());
    CHECK(v->scope == c.s);
    CHECK(v->name == "name");
  }
}

ACCEPT("LNPC-VAR-002", string_suffix_and_index) {
  auto v = parse_var("$@list$[5]");
  CHECK(v && v->scope == Scope::WorldTemp && v->is_string && v->index == 5 && v->name == "list");
  CHECK(!parse_var("x[-1]"));
  CHECK(!parse_var("x[99999999999]"));
  CHECK(!parse_var(""));
}

ACCEPT("LNPC-VAR-003", key_length_limit) {
  CHECK(parse_var(std::string(32, 'a')).has_value());
  CHECK(!parse_var(std::string(33, 'a')).has_value());
}

ACCEPT("LNPC-VAR-004", storage_keys_namespaced_under_legacy) {
  CHECK(storage_key(*parse_var("#acc")) == "legacy:account/acc");
  CHECK(storage_key(*parse_var("name$")) == "legacy:char/name$");
  CHECK(storage_key(*parse_var("$w")) != storage_key(*parse_var("$@w")));
}

ACCEPT("LNPC-VAR-005", sparse_array_zero_and_empty_delete) {
  SparseArray a;
  a.set(3, std::int64_t{7});
  CHECK(a.stored_count() == 1 && a.get_int(3) == 7);
  a.set(3, std::int64_t{0});
  CHECK(a.stored_count() == 0 && a.get_int(3) == 0);
  a.set(1, std::string("x"));
  a.set(1, std::string(""));
  CHECK(a.stored_count() == 0 && a.get_str(1).empty());
}

ACCEPT("LNPC-VAR-006", store_honours_sparse_semantics) {
  InMemoryVariableStore s;
  auto v = *parse_var("@arr[10]");
  s.set(v, std::int64_t{5});
  CHECK(std::get<std::int64_t>(s.get(v)) == 5);
  s.set(v, std::int64_t{0});
  CHECK(s.stored_count() == 0);
  CHECK(std::get<std::string>(s.get(*parse_var("@s$"))).empty());
}

ACCEPT("LNPC-VAR-007", string_values_truncated_below_255) {
  SparseArray a;
  a.set(0, std::string(400, 'z'));
  CHECK(a.get_str(0).size() == kMaxStringValueLen);
}

namespace {
struct FakeAttrs : AttributeBackend {
  std::int64_t get(const std::string& id) const override { return id == "legacy:param/zeny" ? 42 : 0; }
  void set(const std::string& id, std::int64_t v) override { last_id = id; last = v; }
  std::string last_id; std::int64_t last = 0;
};
}  // namespace

ACCEPT("LNPC-PARAM-001", param_variables_shim_over_attributes) {
  FakeAttrs b;
  ParamShim p(b);
  CHECK(is_param_variable("Zeny") && is_param_variable("BaseLevel"));
  CHECK(!is_param_variable("myvar"));
  CHECK(p.read("Zeny") == 42);
  p.write("BaseLevel", 99);
  CHECK(b.last_id == "legacy:param/baselevel" && b.last == 99);
}

// Q-H: `.` NPC variables are shared by a template and its duplicate clones (legacy: both point at one script_code, npc.cpp:4602, whose
// `local` registry holds the `.` variables, script.cpp:2658/2734). The parser records the clone relation; the Luau runtime shares the table.
ACCEPT_LUAU("LNPC-VAR-008", npc_vars_shared_by_duplicate_clones) {
  auto f = parse_file(
      "prontera,150,150,4\tscript\tGuide\t111,{\n .visits += 1; mes \"visit \" + .visits; close;\n}\n"
      "prontera,152,150,4\tduplicate(Guide)\tGuide2\t111\n", "t.txt");
  CHECK(f.npcs.size() == 2 && f.npcs[1].kind == DeclKind::Duplicate && f.npcs[1].duplicate_of == "Guide");
  auto& host = shared_host();
  host.clear_log();
  host.run_ok("qh", R"(
    local api = LEGACY.blocking.scripted({})
    local rt = LEGACY.new{ host = { dialogue = api } }
    rt:addFile("t.txt", "prontera,150,150,4\tscript\tGuide\t111,{\n .visits += 1; mes \"visit \" + .visits; close;\n}\nprontera,152,150,4\tduplicate(Guide)\tGuide2\t111\n")
    rt:run("Guide", 1); rt:run("Guide2", 2); rt:run("Guide", 1)
    print("@qh\t" .. rt.loader.npcs.Guide.vars.visits .. "\t" .. tostring(rt.loader.npcs.Guide.vars == rt.loader.npcs.Guide2.vars)
      .. "\t" .. tostring(rt.loader.npcs.Guide.prog == rt.loader.npcs.Guide2.prog))
  )");
  auto r = host.records("@qh");
  CHECK(r.size() == 1 && r[0][1] == "3" && r[0][2] == "true" && r[0][3] == "true");
}

// `.@` locals belong to the call frame: callsub gets a fresh set, the caller's survive
ACCEPT_LUAU("LNPC-VAR-009", frame_locals_isolated_per_callsub_frame) {
  struct Rec : HostApi {
    InMemoryVariableStore s;
    std::vector<std::string> mes;
    VariableStore& vars() override { return s; }
    void emit(std::string_view c, const std::vector<Value>& a) override {
      if (c == "mes" && !a.empty()) if (auto* t = std::get_if<std::string>(&a[0])) mes.push_back(*t);
    }
  } h;
  auto f = parse_file("-\tscript\tt\t-1,{\n .@a = 1; callsub L_S, 7; mes \"caller a=\" + .@a; end;\nL_S: .@a = getarg(0) * 100; mes \"callee a=\" + .@a; return;\n}\n", "t.txt");
  auto reg = CommandRegistry::with_defaults();
  Interpreter in(f.npcs.at(0).program, h, reg, {});
  CHECK(in.run() == RunOutcome::Finished);
  CHECK(h.mes.size() == 2 && h.mes[0] == "callee a=700" && h.mes[1] == "caller a=1");
}
