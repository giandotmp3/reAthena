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

PENDING("LNPC-VAR-008", npc_vars_shared_by_duplicate_clones, "Q-H unconfirmed; needs interpreter + loader") {
  // Inferred: `.` vars live on the script_code object shared by duplicate() clones.
  throw legacy_npc::NotImplemented("duplicate clone sharing");
}

PENDING("LNPC-VAR-009", frame_locals_isolated_per_callsub_frame, "needs interpreter") {
  throw legacy_npc::NotImplemented("heap frame locals");
}
