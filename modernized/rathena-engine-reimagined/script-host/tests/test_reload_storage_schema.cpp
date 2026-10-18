#include "fixtures.hpp"
#include "script_host/api_schema.hpp"
#include "script_host/reload.hpp"
#include "script_host/storage.hpp"

using namespace script_host;

PENDING("CAP-E03", "successful reload creates generation N+1") {
  ReloadManager r;
  auto g0 = r.active_generation("m");
  auto res = r.reload("m", "return {}");
  CHECK(res.ok);
  CHECK_EQ(res.active_generation, g0 + 1);
}

PENDING("CAP-E03", "failed reload keeps the previous generation active") {
  ReloadManager r;
  auto g0 = r.active_generation("m");
  auto res = r.reload("m", "this is not luau (");
  CHECK(!res.ok);
  CHECK(!res.error.empty());
  CHECK_EQ(r.active_generation("m"), g0);
}

PENDING("CAP-E03", "old generation registrations are cancelled after reload") {
  ReloadManager r;
  r.reload("m", "return {}");
  auto old = r.active_generation("m");
  r.reload("m", "return {}");
  CHECK_EQ(r.live_registrations("m", old), std::size_t(0));
}

PENDING("CAP-E03", "definition shape change reports needs_restart") {
  ReloadManager r;
  auto res = r.reload("m", "-- changes a fields schema");
  CHECK(res.needs_restart);
}

PENDING("CAP-E04", "storage is namespaced per mod and cross-mod read needs permission") {
  StorageApi s;
  s.set(Scope::Character, "char1", "modA", "gold", std::int64_t(5));
  auto own = s.get(Scope::Character, "char1", "modA", "modA", "gold");
  CHECK(own.has_value());
  CHECK_THROWS(s.get(Scope::Character, "char1", "modA", "modB", "gold"));
  s.grant_read("modB", "modA");
  CHECK(s.get(Scope::Character, "char1", "modA", "modB", "gold").has_value());
}

PENDING("CAP-E04", "per-mod quota is enforced") {
  StorageApi s;
  s.set_quota("m", 16);
  CHECK_THROWS(s.set(Scope::World, "world", "m", "k", std::string(1024, 'x')));
}

PENDING("CAP-E04", "loader refuses a stored version with no migration path") {
  StorageApi s;
  s.declare_migrate("m", 3, [](std::uint32_t, const Row& r) { return r; });
  CHECK_THROWS(s.check_loadable("m", 1));  // no 1 -> 3 path declared
}

PENDING("CAP-E04", "storage supports int64, double, string, bool and nested tables") {
  StorageApi s;
  auto t = std::make_shared<ValueTable>();
  t->entries["n"] = std::int64_t(9007199254740993LL);
  s.set(Scope::Account, "acc", "m", "tbl", Value{t});
  auto got = s.get(Scope::Account, "acc", "m", "m", "tbl");
  CHECK(got.has_value());
}

PENDING("CAP-E03", "api schema generates engine.d.luau covering every function") {
  auto schema = load_api_schema_yaml(fx::slurp(API_SCHEMA_PATH));
  CHECK(!schema.functions.empty());
  auto d = generate_luau_declarations(schema);
  for (auto& f : schema.functions) CHECK(d.find(f.name) != std::string::npos);
}

PENDING("CAP-E03", "api schema generates C++ bindings from the same source") {
  auto schema = load_api_schema_yaml(fx::slurp(API_SCHEMA_PATH));
  CHECK(!generate_cpp_bindings(schema).empty());
}
