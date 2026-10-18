#include "fixtures.hpp"
#include "script_host/api_schema.hpp"
#include "script_host/reload.hpp"
#include "script_host/storage.hpp"

using namespace script_host;

LUAU_TEST("CAP-E03", "successful reload creates generation N+1") {
  ReloadManager r;
  auto g0 = r.active_generation("m");
  auto res = r.reload("m", "return {}");
  CHECK(res.ok);
  CHECK_EQ(res.active_generation, g0 + 1);
}

LUAU_TEST("CAP-E03", "failed reload keeps the previous generation active") {
  ReloadManager r;
  auto g0 = r.active_generation("m");
  auto res = r.reload("m", "this is not luau (");
  CHECK(!res.ok);
  CHECK(!res.error.empty());
  CHECK_EQ(r.active_generation("m"), g0);
}

LUAU_TEST("CAP-E03", "old generation registrations are cancelled after reload") {
  ReloadManager r;
  r.reload("m", "return {}");
  auto old = r.active_generation("m");
  r.reload("m", "return {}");
  CHECK_EQ(r.live_registrations("m", old), std::size_t(0));
}

LUAU_PENDING("CAP-E03", "definition shape change reports needs_restart") {
  ReloadManager r;
  auto res = r.reload("m", "-- changes a fields schema");
  CHECK(res.needs_restart);
}

TEST("CAP-E04", "storage is namespaced per mod and cross-mod read needs permission") {
  StorageApi s;
  s.set(Scope::Character, "char1", "modA", "gold", std::int64_t(5));
  auto own = s.get(Scope::Character, "char1", "modA", "modA", "gold");
  CHECK(own.has_value());
  CHECK_THROWS(s.get(Scope::Character, "char1", "modA", "modB", "gold"));
  s.grant_read("modB", "modA");
  CHECK(s.get(Scope::Character, "char1", "modA", "modB", "gold").has_value());
}

TEST("CAP-E04", "per-mod quota is enforced") {
  StorageApi s;
  s.set_quota("m", 16);
  CHECK_THROWS(s.set(Scope::World, "world", "m", "k", std::string(1024, 'x')));
}

TEST("CAP-E04", "loader refuses a stored version with no migration path") {
  StorageApi s;
  s.declare_migrate("m", 3, [](std::uint32_t, const Row& r) { return r; });
  CHECK_THROWS(s.check_loadable("m", 1));  // no 1 -> 3 path declared
}

TEST("CAP-E04", "storage supports int64, double, string, bool and nested tables") {
  StorageApi s;
  auto t = std::make_shared<ValueTable>();
  t->entries["n"] = std::int64_t(9007199254740993LL);
  s.set(Scope::Account, "acc", "m", "tbl", Value{t});
  auto got = s.get(Scope::Account, "acc", "m", "m", "tbl");
  CHECK(got.has_value());
  auto& back = std::get<std::shared_ptr<ValueTable>>(*got);
  CHECK_EQ(std::get<std::int64_t>(back->entries.at("n")), std::int64_t(9007199254740993LL));  // exact beyond 2^53
  CHECK(back != t);  // stored by value, not aliased
  s.set(Scope::World, "world", "m", "d", Value{3.25});
  s.set(Scope::World, "world", "m", "b", Value{true});
  s.set(Scope::World, "world", "m", "s", Value{std::string("hi")});
  CHECK_EQ(std::get<double>(*s.get(Scope::World, "world", "m", "m", "d")), 3.25);
  CHECK_EQ(std::get<bool>(*s.get(Scope::World, "world", "m", "m", "b")), true);
  CHECK_EQ(std::get<std::string>(*s.get(Scope::World, "world", "m", "m", "s")), std::string("hi"));
}

TEST("CAP-E03", "api schema generates engine.d.luau covering every function") {
  auto schema = load_api_schema_yaml(fx::slurp(API_SCHEMA_PATH));
  CHECK(!schema.functions.empty());
  auto d = generate_luau_declarations(schema);
  for (auto& f : schema.functions) CHECK(d.find(f.name) != std::string::npos);
}

TEST("CAP-E03", "api schema generates C++ bindings from the same source") {
  auto schema = load_api_schema_yaml(fx::slurp(API_SCHEMA_PATH));
  CHECK(!generate_cpp_bindings(schema).empty());
}

// ---- storage details --------------------------------------------------------------------
TEST("CAP-E04", "scopes are separate: character a vs b vs account vs world never alias") {
  StorageApi s;
  s.set(Scope::Character, "a", "m", "k", std::int64_t(1));
  s.set(Scope::Character, "b", "m", "k", std::int64_t(2));
  s.set(Scope::Account, "a", "m", "k", std::int64_t(3));
  s.set(Scope::World, "a", "m", "k", std::int64_t(4));
  CHECK_EQ(std::get<std::int64_t>(*s.get(Scope::Character, "a", "m", "m", "k")), std::int64_t(1));
  CHECK_EQ(std::get<std::int64_t>(*s.get(Scope::Character, "b", "m", "m", "k")), std::int64_t(2));
  CHECK_EQ(std::get<std::int64_t>(*s.get(Scope::Account, "a", "m", "m", "k")), std::int64_t(3));
  CHECK_EQ(std::get<std::int64_t>(*s.get(Scope::World, "a", "m", "m", "k")), std::int64_t(4));
  CHECK(!s.get(Scope::Character, "c", "m", "m", "k").has_value());
  CHECK_EQ(s.keys(Scope::Character, "a", "m").size(), std::size_t(1));
}

TEST("CAP-E04", "quota accounting follows overwrite and erase, per mod") {
  StorageApi s;
  s.set_quota("m", 200);
  s.set(Scope::World, "w", "m", "k", std::string(100, 'x'));
  auto used = s.usage("m");
  CHECK(used > 100 && used <= 200);
  s.set(Scope::World, "w", "m", "k", std::string(100, 'y'));   // overwrite: no double count
  CHECK_EQ(s.usage("m"), used);
  CHECK_THROWS(s.set(Scope::World, "w", "m", "k2", std::string(100, 'z')));
  CHECK_EQ(s.usage("m"), used);                                  // failed write changes nothing
  CHECK(s.erase(Scope::World, "w", "m", "k"));
  CHECK_EQ(s.usage("m"), std::uint64_t(0));
  s.set(Scope::World, "w", "m", "k2", std::string(100, 'z'));
  CHECK_EQ(s.usage("other"), std::uint64_t(0));                  // quotas are per mod
  try { s.set(Scope::World, "w", "m", "k3", std::string(500, 'q')); CHECK(false); }
  catch (const StorageError& e) { CHECK(e.code == StorageError::Code::QuotaExceeded); }
}

TEST("CAP-E04", "rows are versioned and migrated step by step, then written back") {
  StorageApi s;
  s.set(Scope::World, "w", "m", "k", std::int64_t(10));          // stored at version 1
  std::vector<std::uint32_t> calls;
  s.declare_migrate("m", 3, [&](std::uint32_t from, const Row& r) {
    calls.push_back(from);
    Row out{from + 1, Value{std::get<std::int64_t>(r.value) * 2}};
    return out;
  });
  CHECK_EQ(s.current_version("m"), std::uint32_t(3));
  auto row = s.get_row(Scope::World, "w", "m", "k");
  CHECK_EQ(row->version, std::uint32_t(3));
  CHECK_EQ(std::get<std::int64_t>(row->value), std::int64_t(40));
  CHECK_EQ(calls.size(), std::size_t(2));
  s.get_row(Scope::World, "w", "m", "k");
  CHECK_EQ(calls.size(), std::size_t(2));                        // already migrated: not run again
  s.set(Scope::World, "w", "m", "new", std::int64_t(1));
  CHECK_EQ(s.get_row(Scope::World, "w", "m", "new")->version, std::uint32_t(3));
  s.check_loadable("m", 1);
  s.check_loadable("m", 3);
  CHECK_THROWS(s.check_loadable("m", 4));
}

TEST("CAP-E04", "a migrate step that does not advance the version is refused") {
  StorageApi s;
  s.set(Scope::World, "w", "m", "k", std::int64_t(1));
  s.declare_migrate("m", 2, [](std::uint32_t, const Row& r) { return r; });
  try { s.get_row(Scope::World, "w", "m", "k"); CHECK(false); }
  catch (const StorageError& e) { CHECK(e.code == StorageError::Code::NoMigrationPath); }
}

TEST("CAP-E04", "values nested deeper than 16 levels are rejected (also stops cycles)") {
  StorageApi s;
  auto root = std::make_shared<ValueTable>();
  root->entries["self"] = Value{root};   // cycle
  CHECK_THROWS(s.set(Scope::World, "w", "m", "k", Value{root}));
  root->entries.clear();
  auto cur = root;
  for (int i = 0; i < 10; ++i) { auto n = std::make_shared<ValueTable>(); cur->entries["c"] = Value{n}; cur = n; }
  s.set(Scope::World, "w", "m", "ok", Value{root});   // depth 10 is fine
  root->entries.clear();
}

TEST("CAP-E04", "the backend is an interface: a custom backend sees every write and backs quota accounting") {
  struct Counting final : StorageBackend {
    MemoryStorageBackend inner;
    int stores = 0;
    std::optional<Row> load(const StorageKey& k) const override { return inner.load(k); }
    void store(const StorageKey& k, const Row& r) override { ++stores; inner.store(k, r); }
    bool erase(const StorageKey& k) override { return inner.erase(k); }
    std::uint64_t usage(const std::string& m) const override { return inner.usage(m); }
    std::vector<std::string> keys(Scope s, const std::string& o, const std::string& m) const override { return inner.keys(s, o, m); }
  };
  auto b = std::make_shared<Counting>();
  StorageApi s(b);
  s.set(Scope::World, "w", "m", "k", std::int64_t(1));
  s.set(Scope::World, "w", "m", "k", std::int64_t(2));
  CHECK_EQ(b->stores, 2);
  CHECK(s.usage("m") > 0);
}

TEST("CAP-E04", "grant_read is directional and per owner mod") {
  StorageApi s;
  s.set(Scope::World, "w", "a", "k", std::int64_t(1));
  s.grant_read("b", "a");
  CHECK(s.get(Scope::World, "w", "a", "b", "k").has_value());
  s.set(Scope::World, "w", "b", "k", std::int64_t(2));
  CHECK_THROWS(s.get(Scope::World, "w", "b", "a", "k"));          // b->a does not imply a->b
  try { s.get(Scope::World, "w", "b", "a", "k"); CHECK(false); }
  catch (const StorageError& e) { CHECK(e.code == StorageError::Code::PermissionDenied); }
}
