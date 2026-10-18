// The script-host storage contract (mirrors tests/test_reload_storage_schema.cpp "CAP-E04" cases
// of the script-host unit, which use the unit's own mini harness and cannot be linked read-only)
// run against the in-memory reference backend AND the persistence adapter, write-through and
// write-behind, over SQLite.
#include <functional>

#include "harness.hpp"
#include "rathena/persist/script_host_adapter.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;
namespace sh = script_host;
using namespace std::chrono_literals;

namespace {

enum class Mode { Memory, WriteThrough, WriteBehind };
const char* name(Mode m) { return m == Mode::Memory ? "memory" : m == Mode::WriteThrough ? "sqlite-write-through" : "sqlite-write-behind"; }

struct Rig {
	explicit Rig(Mode mode, const std::string& db_path = ":memory:", std::uint64_t persist_quota = 1u << 20)
	    : stack(db_path), quarantine(stack.db), kv(stack.db, quarantine, KvPolicy{persist_quota, false}) {
		std::shared_ptr<sh::StorageBackend> backend;
		if (mode == Mode::Memory) {
			backend = std::make_shared<sh::MemoryStorageBackend>();
		} else {
			if (mode == Mode::WriteBehind) {
				wal = std::make_unique<Wal>(WalConfig{scratch_dir(std::string("adapter_") + name(mode) + std::to_string(counter++)), 1u << 22});
				WriteBehindOptions o;
				o.retry_interval = 5ms;
				svc = std::make_unique<WriteBehindService>(stack.db, *wal, o);
			}
			kv_backend = std::make_shared<KvStorageBackend>(kv, svc.get());
			backend = kv_backend;
		}
		api = std::make_unique<sh::StorageApi>(backend);
	}
	~Rig() {
		if (svc) svc->shutdown();  // service before adapter (documented lifetime rule)
	}
	static inline int counter = 0;
	Stack stack;
	Quarantine quarantine;
	KvStore kv;
	std::unique_ptr<Wal> wal;
	std::unique_ptr<WriteBehindService> svc;
	std::shared_ptr<KvStorageBackend> kv_backend;
	std::unique_ptr<sh::StorageApi> api;
};

void for_each_mode(const std::function<void(Rig&)>& body) {
	for (Mode m : {Mode::Memory, Mode::WriteThrough, Mode::WriteBehind}) {
		SCOPED_TRACE(name(m));
		Rig rig(m);
		body(rig);
	}
}

using sh::Scope;
} // namespace

TEST(CAP_E04_PERSIST, StorageIsNamespacedPerModAndCrossModReadNeedsPermission) {
	for_each_mode([](Rig& r) {
		auto& s = *r.api;
		s.set(Scope::Character, "char1", "modA", "gold", std::int64_t(5));
		EXPECT_TRUE(s.get(Scope::Character, "char1", "modA", "modA", "gold").has_value());
		EXPECT_THROW(s.get(Scope::Character, "char1", "modA", "modB", "gold"), sh::StorageError);
		s.grant_read("modB", "modA");
		EXPECT_TRUE(s.get(Scope::Character, "char1", "modA", "modB", "gold").has_value());
	});
}

TEST(CAP_E04_PERSIST, PerModQuotaIsEnforcedAndReportedAsQuotaExceeded) {
	for_each_mode([](Rig& r) {
		r.api->set_quota("m", 16);
		try {
			r.api->set(Scope::World, "world", "m", "k", std::string(1024, 'x'));
			FAIL();
		} catch (const sh::StorageError& e) {
			EXPECT_EQ(e.code, sh::StorageError::Code::QuotaExceeded);
		}
		EXPECT_EQ(r.api->usage("m"), 0u);
		EXPECT_FALSE(r.api->get(Scope::World, "world", "m", "m", "k").has_value());
	});
}

TEST(CAP_E04_PERSIST, LoaderRefusesAStoredVersionWithNoMigrationPath) {
	for_each_mode([](Rig& r) {
		r.api->declare_migrate("m", 3, [](std::uint32_t, const sh::Row& row) { return row; });
		EXPECT_THROW(r.api->check_loadable("m", 1), sh::StorageError);
	});
}

TEST(CAP_E04_PERSIST, TypedValuesRoundTripExactlyAndAreStoredByValue) {
	for_each_mode([](Rig& r) {
		auto& s = *r.api;
		auto t = std::make_shared<sh::ValueTable>();
		t->entries["n"] = std::int64_t(9007199254740993LL);
		t->array[1] = std::string("one");
		t->array[-3] = 2.5;
		t->entries["nil"] = std::monostate{};
		auto inner = std::make_shared<sh::ValueTable>();
		inner->entries["deep"] = true;
		t->entries["inner"] = sh::Value{inner};
		s.set(Scope::Account, "acc", "m", "tbl", sh::Value{t});
		auto got = s.get(Scope::Account, "acc", "m", "m", "tbl");
		ASSERT_TRUE(got.has_value());
		auto& back = std::get<std::shared_ptr<sh::ValueTable>>(*got);
		EXPECT_EQ(std::get<std::int64_t>(back->entries.at("n")), 9007199254740993LL);
		EXPECT_EQ(std::get<std::string>(back->array.at(1)), "one");
		EXPECT_EQ(std::get<double>(back->array.at(-3)), 2.5);
		EXPECT_TRUE(std::holds_alternative<std::monostate>(back->entries.at("nil")));
		EXPECT_TRUE(std::get<bool>(std::get<std::shared_ptr<sh::ValueTable>>(back->entries.at("inner"))->entries.at("deep")));
		EXPECT_NE(back, t);
		EXPECT_TRUE(sh::value_equal(*got, sh::Value{t}));
		s.set(Scope::World, "world", "m", "d", sh::Value{3.25});
		s.set(Scope::World, "world", "m", "b", sh::Value{true});
		s.set(Scope::World, "world", "m", "s", sh::Value{std::string("hi")});
		EXPECT_EQ(std::get<double>(*s.get(Scope::World, "world", "m", "m", "d")), 3.25);
		EXPECT_EQ(std::get<bool>(*s.get(Scope::World, "world", "m", "m", "b")), true);
		EXPECT_EQ(std::get<std::string>(*s.get(Scope::World, "world", "m", "m", "s")), "hi");
	});
}

TEST(CAP_E04_PERSIST, ScopesAndOwnersNeverAlias) {
	for_each_mode([](Rig& r) {
		auto& s = *r.api;
		s.set(Scope::Character, "a", "m", "k", std::int64_t(1));
		s.set(Scope::Character, "b", "m", "k", std::int64_t(2));
		s.set(Scope::Account, "a", "m", "k", std::int64_t(3));
		s.set(Scope::World, "a", "m", "k", std::int64_t(4));
		EXPECT_EQ(std::get<std::int64_t>(*s.get(Scope::Character, "a", "m", "m", "k")), 1);
		EXPECT_EQ(std::get<std::int64_t>(*s.get(Scope::Character, "b", "m", "m", "k")), 2);
		EXPECT_EQ(std::get<std::int64_t>(*s.get(Scope::Account, "a", "m", "m", "k")), 3);
		EXPECT_EQ(std::get<std::int64_t>(*s.get(Scope::World, "a", "m", "m", "k")), 4);
		EXPECT_FALSE(s.get(Scope::Character, "c", "m", "m", "k").has_value());
		EXPECT_EQ(s.keys(Scope::Character, "a", "m").size(), 1u);
		// non-numeric and numeric owner strings both work and are distinct
		s.set(Scope::Character, "77", "m", "k", std::int64_t(5));
		s.set(Scope::Character, "077", "m", "k", std::int64_t(6));
		EXPECT_EQ(std::get<std::int64_t>(*s.get(Scope::Character, "77", "m", "m", "k")), 5);
		EXPECT_EQ(std::get<std::int64_t>(*s.get(Scope::Character, "077", "m", "m", "k")), 6);
	});
}

TEST(CAP_E04_PERSIST, QuotaAccountingFollowsOverwriteAndEraseAndFailedWritesChangeNothing) {
	for_each_mode([](Rig& r) {
		auto& s = *r.api;
		s.set_quota("m", 200);
		s.set(Scope::World, "w", "m", "k", std::string(100, 'x'));
		const auto used = s.usage("m");
		EXPECT_TRUE(used > 100 && used <= 200);
		s.set(Scope::World, "w", "m", "k", std::string(100, 'y'));
		EXPECT_EQ(s.usage("m"), used);
		EXPECT_THROW(s.set(Scope::World, "w", "m", "k2", std::string(100, 'z')), sh::StorageError);
		EXPECT_EQ(s.usage("m"), used);
		EXPECT_TRUE(s.erase(Scope::World, "w", "m", "k"));
		EXPECT_FALSE(s.erase(Scope::World, "w", "m", "k"));
		EXPECT_EQ(s.usage("m"), 0u);
		s.set(Scope::World, "w", "m", "k2", std::string(100, 'z'));
		EXPECT_EQ(s.usage("other"), 0u);
		try { s.set(Scope::World, "w", "m", "k3", std::string(500, 'q')); FAIL(); }
		catch (const sh::StorageError& e) { EXPECT_EQ(e.code, sh::StorageError::Code::QuotaExceeded); }
	});
}

TEST(CAP_E04_PERSIST, RowsAreVersionedMigratedStepByStepAndWrittenBack) {
	for_each_mode([](Rig& r) {
		auto& s = *r.api;
		s.set(Scope::World, "w", "m", "k", std::int64_t(10));
		std::vector<std::uint32_t> calls;
		s.declare_migrate("m", 3, [&](std::uint32_t from, const sh::Row& row) {
			calls.push_back(from);
			return sh::Row{from + 1, sh::Value{std::get<std::int64_t>(row.value) * 2}};
		});
		auto row = s.get_row(Scope::World, "w", "m", "k");
		EXPECT_EQ(row->version, 3u);
		EXPECT_EQ(std::get<std::int64_t>(row->value), 40);
		EXPECT_EQ(calls.size(), 2u);
		s.get_row(Scope::World, "w", "m", "k");
		EXPECT_EQ(calls.size(), 2u);
		s.set(Scope::World, "w", "m", "new", std::int64_t(1));
		EXPECT_EQ(s.get_row(Scope::World, "w", "m", "new")->version, 3u);
		s.check_loadable("m", 1);
		s.check_loadable("m", 3);
		EXPECT_THROW(s.check_loadable("m", 4), sh::StorageError);
	});
}

TEST(CAP_E04_PERSIST, AMigrateStepThatDoesNotAdvanceIsRefused) {
	for_each_mode([](Rig& r) {
		r.api->set(Scope::World, "w", "m", "k", std::int64_t(1));
		r.api->declare_migrate("m", 2, [](std::uint32_t, const sh::Row& row) { return row; });
		try { r.api->get_row(Scope::World, "w", "m", "k"); FAIL(); }
		catch (const sh::StorageError& e) { EXPECT_EQ(e.code, sh::StorageError::Code::NoMigrationPath); }
	});
}

TEST(CAP_E04_PERSIST, ValuesNestedDeeperThanSixteenOrCyclicAreRejected) {
	for_each_mode([](Rig& r) {
		auto root = std::make_shared<sh::ValueTable>();
		root->entries["self"] = sh::Value{root};
		EXPECT_THROW(r.api->set(Scope::World, "w", "m", "k", sh::Value{root}), sh::StorageError);
		root->entries.clear();
		auto cur = root;
		for (int i = 0; i < 10; ++i) { auto n = std::make_shared<sh::ValueTable>(); cur->entries["c"] = sh::Value{n}; cur = n; }
		EXPECT_NO_THROW(r.api->set(Scope::World, "w", "m", "ok", sh::Value{root}));
		root->entries.clear();
	});
}

TEST(CAP_E04_PERSIST, GrantReadIsDirectionalAndPerOwnerMod) {
	for_each_mode([](Rig& r) {
		auto& s = *r.api;
		s.set(Scope::World, "w", "a", "k", std::int64_t(1));
		s.grant_read("b", "a");
		EXPECT_TRUE(s.get(Scope::World, "w", "a", "b", "k").has_value());
		s.set(Scope::World, "w", "b", "k", std::int64_t(2));
		try { s.get(Scope::World, "w", "b", "a", "k"); FAIL(); }
		catch (const sh::StorageError& e) { EXPECT_EQ(e.code, sh::StorageError::Code::PermissionDenied); }
	});
}

TEST(PERSIST_ADAPTER_001, WrittenRowsSurviveRestartInBothWriteModes) {
	for (Mode m : {Mode::WriteThrough, Mode::WriteBehind}) {
		SCOPED_TRACE(name(m));
		const auto path = (scratch_dir(std::string("adapter001_") + name(m)) / "kv.db").string();
		{
			Rig r(m, path);
			r.api->set(Scope::Character, "42", "quests", "q1", std::int64_t(7));
			r.api->set(Scope::World, "world", "quests", "w", std::string("hello"));
			r.api->set(Scope::World, "world", "quests", "gone", std::int64_t(1));
			r.api->erase(Scope::World, "world", "quests", "gone");
		}  // ~Rig drains the write-behind service
		Rig r(Mode::WriteThrough, path);
		EXPECT_EQ(std::get<std::int64_t>(*r.api->get(Scope::Character, "42", "quests", "quests", "q1")), 7);
		EXPECT_EQ(std::get<std::string>(*r.api->get(Scope::World, "world", "quests", "quests", "w")), "hello");
		EXPECT_FALSE(r.api->get(Scope::World, "world", "quests", "quests", "gone").has_value());
		EXPECT_EQ(r.api->keys(Scope::World, "world", "quests"), std::vector<std::string>{"w"});
		EXPECT_GT(r.api->usage("quests"), 0u);  // quota ledger is rebuilt from the table
	}
}

TEST(PERSIST_ADAPTER_002, WriteBehindServesReadYourWritesBeforeTheFlushAndMergesKeys) {
	Rig r(Mode::WriteBehind);
	// the sim has read these keys before (hot path), so absent/present answers are cached
	EXPECT_FALSE(r.api->get(Scope::Character, "1", "m", "m", "a").has_value());
	EXPECT_FALSE(r.api->get(Scope::Character, "1", "m", "m", "b").has_value());
	r.stack.db.set_outage(true);  // nothing can reach the database right now
	r.api->set(Scope::Character, "1", "m", "a", std::int64_t(1));
	r.api->set(Scope::Character, "1", "m", "b", std::int64_t(2));
	r.api->erase(Scope::Character, "1", "m", "a");
	EXPECT_FALSE(r.api->get(Scope::Character, "1", "m", "m", "a").has_value());
	EXPECT_EQ(std::get<std::int64_t>(*r.api->get(Scope::Character, "1", "m", "m", "b")), 2);
	EXPECT_THROW(r.api->get(Scope::Character, "9", "m", "m", "never-seen"), RepositoryError);  // honest, not "absent"
	EXPECT_GT(r.kv_backend->overlay_size(), 0u);
	r.stack.db.set_outage(false);
	(void)r.svc->handoff();
	EXPECT_TRUE(eventually([&] { return r.kv_backend->overlay_size() == 0; }));  // flushed rows are clean cache entries
	EXPECT_EQ(std::get<std::int64_t>(*r.api->get(Scope::Character, "1", "m", "m", "b")), 2);
	EXPECT_EQ(scalar(r.stack.raw_view(), "SELECT COUNT(*) FROM kv_entries"), 1);
}

TEST(PERSIST_ADAPTER_003, PersistenceLayerQuotaStopsAWriteEvenWhenTheApiQuotaWasRaised) {
	for (Mode m : {Mode::WriteThrough, Mode::WriteBehind}) {
		SCOPED_TRACE(name(m));
		Rig r(m, ":memory:", 120);
		r.api->set_quota("greedy", 1u << 30);  // a mod (or a bug) lifting the API-level limit
		r.api->set(Scope::World, "w", "greedy", "a", std::string(60, 'x'));
		try {
			r.api->set(Scope::World, "w", "greedy", "b", std::string(100, 'x'));
			FAIL() << "the persistence layer must refuse";
		} catch (const sh::StorageError& e) {
			EXPECT_EQ(e.code, sh::StorageError::Code::QuotaExceeded);
		}
		EXPECT_FALSE(r.api->get(Scope::World, "w", "greedy", "greedy", "b").has_value());
		r.kv_backend->set_mod_quota("greedy", 1000);
		EXPECT_NO_THROW(r.api->set(Scope::World, "w", "greedy", "b", std::string(100, 'x')));
	}
}

TEST(PERSIST_ADAPTER_004, CorruptStoredRowGivesTheOwningModATypedCorruptErrorOthersAreUnaffected) {
	Rig r(Mode::WriteThrough);
	r.api->set(Scope::World, "w", "bad_mod", "k", std::int64_t(1));
	r.api->set(Scope::World, "w", "good_mod", "k", std::int64_t(2));
	r.stack.db.execute({"UPDATE kv_entries SET value = ? WHERE mod_id = 'bad_mod'", {SqlValue{Blob{"S\x05\xff\xff\xff\xff"}}}});
	for (int i = 0; i < 2; ++i) {
		try {
			r.api->get(Scope::World, "w", "bad_mod", "bad_mod", "k");
			FAIL();
		} catch (const sh::StorageError& e) {
			EXPECT_EQ(e.code, sh::StorageError::Code::Corrupt);
		}
	}
	EXPECT_EQ(std::get<std::int64_t>(*r.api->get(Scope::World, "w", "good_mod", "good_mod", "k")), 2);
	EXPECT_TRUE(r.quarantine.contains("kv_entries", "kv/bad_mod/k"));
	EXPECT_THROW(r.api->set(Scope::World, "w", "bad_mod", "k", std::int64_t(3)), sh::StorageError);  // no silent overwrite
	EXPECT_EQ(r.api->usage("bad_mod"), 0u);
}

TEST(PERSIST_ADAPTER_005, WalFullRefusalSurfacesAsATypedErrorAndTheQuotaReservationIsUndone) {
	Stack s;
	Quarantine q(s.db);
	KvStore kv(s.db, q);
	Wal wal({scratch_dir("adapter005"), 150});
	auto svc = std::make_unique<WriteBehindService>(s.db, wal, WriteBehindOptions{5ms, 256, {}});
	auto backend = std::make_shared<KvStorageBackend>(kv, svc.get());
	sh::StorageApi api(backend);
	EXPECT_FALSE(api.get(Scope::World, "w", "m", "m", "a").has_value());  // warm the cache while online
	EXPECT_FALSE(api.get(Scope::World, "w", "m", "m", "b").has_value());
	s.db.set_outage(true);
	api.set(Scope::World, "w", "m", "a", std::string(300, 'x'));
	(void)svc->handoff();
	ASSERT_TRUE(eventually([&] { return svc->refusing_writes(); }));
	const auto used = api.usage("m");
	EXPECT_THROW(api.set(Scope::World, "w", "m", "b", std::int64_t(1)), WalFullError);
	EXPECT_EQ(api.usage("m"), used);  // refused write holds no quota
	EXPECT_FALSE(api.get(Scope::World, "w", "m", "m", "b").has_value());
	s.db.set_outage(false);
	svc->shutdown();
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM kv_entries"), 1);
}
