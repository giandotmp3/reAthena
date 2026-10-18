// Architecture 3.1 "Corrupt mod KV row": quarantined, typed error on read, other mods unaffected.
// Architecture 5: storage quota enforced in the persistence layer, not only the API.
#include "support.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

TEST(PERSIST_QUAR_001, CorruptRowIsQuarantinedAndReadRaisesTypedErrorOtherModsUnaffected) {
	auto db = memory_db();
	Quarantine q(*db);
	KvStore kv(*db, q);
	kv.set({character(1), "good_mod", "k"}, Value{std::int64_t{1}});
	kv.set({character(1), "bad_mod", "k"}, Value{std::int64_t{2}});
	// corrupt the stored encoding of one row behind the store's back
	db->execute({"UPDATE kv_entries SET value = ? WHERE mod_id = ?", {SqlValue{Blob{"\xff\xfe garbage"}}, SqlValue{std::string("bad_mod")}}});
	EXPECT_THROW(kv.get({character(1), "bad_mod", "k"}), QuarantinedRowError);
	EXPECT_TRUE(q.contains("kv_entries", "kv/bad_mod/k"));
	ASSERT_TRUE(kv.get({character(1), "good_mod", "k"}).has_value());
	EXPECT_EQ(q.list().size(), 1u);
	EXPECT_EQ(q.list()[0].reason.find("garbage"), std::string::npos);  // reason holds no payload bytes
}

TEST(PERSIST_QUAR_002, ReleasedRowIsReturnedRawAndLeavesQuarantine) {
	auto db = memory_db();
	Quarantine q(*db);
	QuarantinedRow row;
	row.table = "kv_entries";
	row.row_ref = "kv/m/k";
	row.reason = "decode failure";
	row.raw = blob("raw");
	const auto id = q.put(row);
	auto released = q.release(id);
	ASSERT_TRUE(released.has_value());
	EXPECT_EQ(released->raw.bytes, "raw");
	EXPECT_TRUE(q.list().empty());
}

TEST(PERSIST_QUOTA_001, WriteOverModQuotaIsRefusedAndStoredStateUnchanged) {
	auto db = memory_db();
	Quarantine q(*db);
	KvStore kv(*db, q, KvPolicy{64, false});
	kv.set({character(1), "greedy", "a"}, Value{std::string(40, 'x')});
	EXPECT_THROW(kv.set({character(1), "greedy", "b"}, Value{std::string(40, 'x')}), QuotaExceededError);
	EXPECT_FALSE(kv.get({character(1), "greedy", "b"}).has_value());
	// quota is per mod: another mod is unaffected
	EXPECT_NO_THROW(kv.set({character(1), "polite", "a"}, Value{std::string(40, 'x')}));
}

TEST(PERSIST_QUOTA_002, OverwriteAndEraseReleaseQuota) {
	auto db = memory_db();
	Quarantine q(*db);
	KvStore kv(*db, q, KvPolicy{64, false});
	kv.set({character(1), "m", "a"}, Value{std::string(50, 'x')});
	kv.set({character(1), "m", "a"}, Value{std::string(10, 'x')});  // overwrite shrinks
	EXPECT_LE(kv.bytes_used("m"), 20u);
	EXPECT_TRUE(kv.erase({character(1), "m", "a"}));
	EXPECT_EQ(kv.bytes_used("m"), 0u);
}
