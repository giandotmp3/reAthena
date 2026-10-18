// Records, mod KV, quarantine, id_map and codec details beyond the scaffold's acceptance tests.
#include <random>

#include "harness.hpp"
#include "rathena/persist/codec.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

namespace {
Value nested() {
	auto inner = std::make_shared<ValueTable>();
	inner->entries.emplace_back("pi", Value{3.25});
	inner->entries.emplace_back("flag", Value{true});
	auto t = std::make_shared<ValueTable>();
	t->entries.emplace_back("name", Value{"fenrir"});
	t->entries.emplace_back("big", Value{std::int64_t{9007199254740993LL}});  // beyond 2^53: must stay exact
	t->entries.emplace_back("inner", Value{std::shared_ptr<const ValueTable>(inner)});
	return Value{std::shared_ptr<const ValueTable>(t)};
}
} // namespace

TEST(PERSIST_KV_001, AllValueTypesRoundTripExactlyIncludingNestedTablesAndInt64) {
	Stack s;
	Quarantine q(s.db);
	KvStore kv(s.db, q);
	const KvKey k{character(7), "mymod", "t"};
	kv.set(k, nested());
	const auto got = kv.get(k);
	ASSERT_TRUE(got.has_value());
	EXPECT_EQ(encode_value(*got), encode_value(nested()));
	const auto& t = *std::get<std::shared_ptr<const ValueTable>>(got->data);
	EXPECT_EQ(std::get<std::int64_t>(t.entries[1].second.data), 9007199254740993LL);
	kv.set({account(7), "mymod", "d"}, Value{-0.5});
	EXPECT_EQ(std::get<double>(kv.get({account(7), "mymod", "d"})->data), -0.5);
	kv.set({global_account(), "mymod", "b"}, Value{false});
	EXPECT_EQ(std::get<bool>(kv.get({global_account(), "mymod", "b"})->data), false);
}

TEST(PERSIST_KV_002, StoredRowsSurviveReopenOfAFileDatabase) {
	const auto path = (scratch_dir("kv002") / "kv.db").string();
	{
		Stack s(path);
		Quarantine q(s.db);
		KvStore kv(s.db, q);
		kv.set({character(1), "m", "k"}, Value{std::int64_t{41}});
	}
	Stack s(path);
	Quarantine q(s.db);
	KvStore kv(s.db, q);
	EXPECT_EQ(std::get<std::int64_t>(kv.get({character(1), "m", "k"})->data), 41);
	EXPECT_GT(kv.bytes_used("m"), 0u);  // the quota ledger is rebuilt from the table
}

TEST(PERSIST_KV_003, QuotaIsPerModAcrossAllOwnersAndScopesAndSurvivesRestart) {
	const auto path = (scratch_dir("kv003") / "kv.db").string();
	{
		Stack s(path);
		Quarantine q(s.db);
		KvStore kv(s.db, q, KvPolicy{200, false});
		kv.set({character(1), "m", "a"}, Value{std::string(80, 'x')});
		kv.set({account(2), "m", "a"}, Value{std::string(80, 'x')});
		EXPECT_THROW(kv.set({OwnerRef{OwnerScope::World, 0}, "m", "a"}, Value{std::string(80, 'x')}), QuotaExceededError);
	}
	Stack s(path);
	Quarantine q(s.db);
	KvStore kv(s.db, q, KvPolicy{200, false});
	EXPECT_THROW(kv.set({character(3), "m", "z"}, Value{std::string(80, 'x')}), QuotaExceededError);
	kv.set_mod_quota("m", 1000);  // per-mod override
	EXPECT_NO_THROW(kv.set({character(3), "m", "z"}, Value{std::string(80, 'x')}));
}

TEST(PERSIST_KV_004, RowVersionsMigrateOnReadAreWrittenBackAndMissingPathsAreTyped) {
	Stack s;
	Quarantine q(s.db);
	KvStore kv(s.db, q);
	KvMigrationRegistry reg;
	kv.set_migrations(&reg);
	const KvKey k{character(1), "m", "gold"};
	kv.set(k, Value{std::int64_t{10}});  // version 1
	int calls = 0;
	reg.register_step("m", 1, [&](const Value& v) { ++calls; return Value{std::get<std::int64_t>(v.data) * 2}; });
	reg.register_step("m", 2, [&](const Value& v) { ++calls; return Value{std::get<std::int64_t>(v.data) + 1}; });
	kv.set_mod_version("m", 3);
	EXPECT_EQ(std::get<std::int64_t>(kv.get(k)->data), 21);  // (10*2)+1
	EXPECT_EQ(calls, 2);
	EXPECT_EQ(std::get<std::int64_t>(kv.get(k)->data), 21);
	EXPECT_EQ(calls, 2);  // written back at version 3: not migrated again
	EXPECT_EQ(scalar(s.db, "SELECT row_version FROM kv_entries WHERE mod_id = 'm'"), 3);
	kv.set_mod_version("m", 4);  // no 3 -> 4 step
	EXPECT_THROW(kv.get(k), MigrationError);
	EXPECT_EQ(scalar(s.db, "SELECT COUNT(*) FROM kv_entries"), 1);  // refused, not deleted or quarantined
}

TEST(PERSIST_KV_005, EveryKindOfCorruptionQuarantinesOnlyThatRowAndKeepsItsBytes) {
	Stack s;
	Quarantine q(s.db);
	KvStore kv(s.db, q);
	for (const char* key : {"trunc", "tag", "trail", "codec", "ok"}) kv.set({character(1), "m", key}, nested());
	auto corrupt = [&](const char* key, Blob b) {
		s.db.execute({"UPDATE kv_entries SET value = ? WHERE entry_key = ?", {SqlValue{std::move(b)}, SqlValue{std::string(key)}}});
	};
	const std::string good = encode_value(nested());
	corrupt("trunc", Blob{good.substr(0, good.size() - 3)});
	corrupt("tag", Blob{std::string(1, 'P') + std::string(1, '\x77')});
	corrupt("trail", Blob{good + "x"});
	corrupt("codec", Blob{"\x01\x02"});
	for (const char* key : {"trunc", "tag", "trail", "codec"}) {
		EXPECT_THROW(kv.get({character(1), "m", key}), QuarantinedRowError) << key;
		EXPECT_THROW(kv.get({character(1), "m", key}), QuarantinedRowError) << key << " (second read still typed)";
		EXPECT_THROW(kv.set({character(1), "m", key}, Value{std::int64_t{1}}), QuarantinedRowError) << "no silent overwrite of evidence";
	}
	EXPECT_TRUE(kv.get({character(1), "m", "ok"}).has_value());
	EXPECT_EQ(q.list().size(), 4u);
	for (const auto& row : q.list()) EXPECT_EQ(row.reason.find("fenrir"), std::string::npos);
	EXPECT_EQ(scalar(s.db, "SELECT COUNT(*) FROM kv_entries"), 1);  // moved out of the live table
	EXPECT_EQ(kv.bytes_used("m"), encoded_size(nested()) + 2 /* key "ok" */);
	const auto rel = q.release(q.list().front().quarantine_id);
	ASSERT_TRUE(rel.has_value());
	EXPECT_FALSE(rel->raw.bytes.empty());
}

TEST(PERSIST_KV_006, QuarantineIsPerOwnerSoOtherOwnersOfTheSameModKeyAreUnaffected) {
	Stack s;
	Quarantine q(s.db);
	KvStore kv(s.db, q);
	kv.set({character(1), "m", "k"}, Value{std::int64_t{1}});
	kv.set({character(2), "m", "k"}, Value{std::int64_t{2}});
	s.db.execute({"UPDATE kv_entries SET value = ? WHERE owner_ref = '1'", {SqlValue{Blob{"zz"}}}});
	EXPECT_THROW(kv.get({character(1), "m", "k"}), QuarantinedRowError);
	EXPECT_EQ(std::get<std::int64_t>(kv.get({character(2), "m", "k"})->data), 2);
}

TEST(PERSIST_KV_007, TypeMismatchedColumnsAreTreatedAsCorruptNotCrashed) {
	Stack s;
	Quarantine q(s.db);
	KvStore kv(s.db, q);
	kv.set({character(1), "m", "k"}, Value{std::int64_t{1}});
	s.db.execute({"UPDATE kv_entries SET value = 'text instead of blob'", {}});  // SQLite is dynamically typed
	EXPECT_THROW(kv.get({character(1), "m", "k"}), QuarantinedRowError);
	EXPECT_EQ(q.list().size(), 1u);
}

TEST(PERSIST_RECORD_001, StaleRevisionSavesAreReportedNotSilentlyApplied) {
	Stack s;
	OnlineLeases leases;
	Quarantine q(s.db);
	RecordStore store(s.db, leases, q);
	auto save = [&](std::uint64_t rev, const char* payload) {
		SaveRequest r;
		r.record = make_record(5, rev, payload);
		r.final_save = true;
		r.declared_size = std::string(payload).size();
		return store.save(r);
	};
	EXPECT_EQ(save(2, "new"), SaveOutcome::Accepted);
	EXPECT_EQ(save(1, "old"), SaveOutcome::RejectedStaleRevision);
	EXPECT_EQ(store.load({character(5), "mod:profile"})->payload.bytes, "new");
	EXPECT_EQ(save(2, "same"), SaveOutcome::Accepted);  // equal revision replaces (idempotent replays)
}

TEST(PERSIST_RECORD_002, CorruptRecordIsQuarantinedWithATypedReadError) {
	Stack s;
	OnlineLeases leases;
	Quarantine q(s.db);
	RecordStore store(s.db, leases, q);
	SaveRequest r;
	r.record = make_record(5, 1, "abc");
	r.final_save = true;
	r.declared_size = 3;
	ASSERT_EQ(store.save(r), SaveOutcome::Accepted);
	s.db.execute({"UPDATE persist_records SET revision = 'oops'", {}});
	EXPECT_THROW(store.load({character(5), "mod:profile"}), QuarantinedRowError);
	EXPECT_THROW(store.load({character(5), "mod:profile"}), QuarantinedRowError);
	EXPECT_TRUE(q.contains("persist_records", "rec/mod:profile"));
	EXPECT_FALSE(store.load({character(6), "mod:profile"}).has_value());  // others unaffected
}

TEST(PERSIST_RECORD_003, CollectionsKeepOrderAndRestoreOnceDoesNotConsumeOtherKinds) {
	Stack s;
	OnlineLeases leases;
	Quarantine q(s.db);
	RecordStore store(s.db, leases, q);
	store.set_collection_policy("mod:once", {3, true});
	store.set_collection_policy("mod:keep", {3, false});
	const RecordKey once{character(1), "mod:once"}, keep{character(1), "mod:keep"};
	store.save_collection(once, {blob("c"), blob("a"), blob("b")});
	store.save_collection(keep, {blob("z"), blob("y")});
	const auto got = store.load_collection(once);
	ASSERT_EQ(got.size(), 3u);
	EXPECT_EQ(got[0].bytes, "c");
	EXPECT_EQ(got[2].bytes, "b");
	EXPECT_EQ(store.load_collection(keep).size(), 2u);
	EXPECT_EQ(store.load_collection(keep).size(), 2u);  // restore_once off: stays
	EXPECT_THROW(store.save_collection(once, {blob("1"), blob("2"), blob("3"), blob("4")}), CollectionCapExceededError);
	store.save_collection(keep, {blob("only")});  // a new save replaces
	EXPECT_EQ(store.load_collection(keep).size(), 1u);
}

TEST(PERSIST_IDMAP_005, RetiredStringIdRevivesItsOldNumberAndNothingElseCanTakeIt) {
	Stack s;
	IdMap map(s.db, {{"item", {100, 110}}});
	const auto a = map.assign("item", "m:item/a");
	const auto b = map.assign("item", "m:item/b");
	map.retire("item", "m:item/a");
	EXPECT_FALSE(map.client_id("item", "m:item/a").has_value());
	EXPECT_FALSE(map.namespaced_id("item", a).has_value());
	const auto c = map.assign("item", "m:item/c");
	EXPECT_NE(c, a);
	EXPECT_NE(c, b);
	EXPECT_EQ(map.assign("item", "m:item/a"), a);  // the mod returns: same number, same save data meaning
	EXPECT_FALSE(map.is_tombstoned("item", a));
	EXPECT_THROW(map.assign("item", "m:item/a", b), IdCollisionError);  // cannot also claim another number
}

TEST(PERSIST_IDMAP_006, ExhaustedRangeNeverReusesTombstonesAndHolesFromExplicitIdsAreFilled) {
	Stack s;
	IdMap map(s.db, {{"status", {1, 4}}});
	map.assign("status", "m:status/a", 2u);  // explicit id inside the range leaves holes at 1, 3, 4
	EXPECT_EQ(map.assign("status", "m:status/b"), 3u);
	EXPECT_EQ(map.assign("status", "m:status/c"), 4u);
	EXPECT_EQ(map.assign("status", "m:status/d"), 1u);  // the one hole
	map.retire("status", "m:status/d");
	EXPECT_THROW(map.assign("status", "m:status/e"), IdRangeExhaustedError);  // the tombstone is not recycled
	EXPECT_THROW(map.assign("unknown_kind", "m:x/y"), PersistError);
}

TEST(PERSIST_CODEC_001, DecodersRejectMutatedAndRandomBytesWithoutCrashing) {
	std::mt19937_64 rng(12345);
	const std::string good = encode_value(nested());
	int rejected = 0, accepted = 0;
	for (int i = 0; i < 4000; ++i) {
		std::string m = good;
		const int flips = 1 + static_cast<int>(rng() % 4);
		for (int f = 0; f < flips; ++f) m[rng() % m.size()] = static_cast<char>(rng());
		if (rng() % 5 == 0) m.resize(rng() % (m.size() + 1));
		try { (void)decode_value(m); ++accepted; } catch (const DecodeError&) { ++rejected; }
	}
	EXPECT_GT(rejected, 0);
	(void)accepted;
	for (int i = 0; i < 2000; ++i) {
		std::string junk(rng() % 64, '\0');
		for (auto& c : junk) c = static_cast<char>(rng());
		try { (void)decode_value(junk); } catch (const DecodeError&) {}
		try { (void)decode_batch(junk); } catch (const DecodeError&) {}
	}
	// a hostile length prefix must not allocate gigabytes
	ByteWriter w;
	w.u64(1);
	w.u32(0xFFFFFFFFu);
	EXPECT_THROW(decode_batch(w.bytes()), DecodeError);
	// depth bomb
	std::string bomb(1, kCodecPersistValue);
	for (int i = 0; i < 100; ++i) { bomb += '\x05'; bomb += std::string("\x01\0\0\0", 4); bomb += std::string("\x01\0\0\0k", 5); }
	EXPECT_THROW(decode_value(bomb), DecodeError);
}

TEST(PERSIST_CODEC_002, ValuesNestedPastTheLimitAreRefusedAtEncodeTime) {
	std::shared_ptr<const ValueTable> cur = std::make_shared<ValueTable>();
	for (int i = 0; i < 20; ++i) {
		auto t = std::make_shared<ValueTable>();
		t->entries.emplace_back("c", Value{cur});
		cur = t;
	}
	EXPECT_THROW(encode_value(Value{cur}), PersistError);
}
