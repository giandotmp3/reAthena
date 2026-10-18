// BR-CHAR-032: a save is accepted only if the owner is online on this server (or final/restore),
// and the payload size matches.  BR-CHAR-054: bounded collections, restored once.
#include "support.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

namespace {
struct Fixture {
	std::unique_ptr<SqlConnection> db = memory_db();
	OnlineLeases leases;
	Quarantine quarantine{*db};
	RecordStore store{*db, leases, quarantine};
	static SaveRequest req(OwnerRef o, std::string payload) {
		SaveRequest r;
		r.record.key = {o, "mod:profile"};
		r.record.payload = blob(payload);
		r.declared_size = payload.size();
		return r;
	}
};
} // namespace

TEST(BR_CHAR_032, SaveAcceptedWhenOwnerOnlineAndSizeMatches) {
	PENDING_RULE("BR-CHAR-032");
	Fixture f;
	f.leases.acquire(character(150000));
	EXPECT_EQ(f.store.save(Fixture::req(character(150000), "abc")), SaveOutcome::Accepted);
	ASSERT_TRUE(f.store.load({character(150000), "mod:profile"}).has_value());
}

TEST(BR_CHAR_032, SaveRejectedWhenOwnerNotOnline) {
	PENDING_RULE("BR-CHAR-032");
	Fixture f;
	EXPECT_EQ(f.store.save(Fixture::req(character(150001), "abc")), SaveOutcome::RejectedNotOnline);
	EXPECT_FALSE(f.store.load({character(150001), "mod:profile"}).has_value());
}

TEST(BR_CHAR_032, SaveRejectedWhenDeclaredSizeDiffersFromPayload) {
	PENDING_RULE("BR-CHAR-032");
	Fixture f;
	f.leases.acquire(character(150002));
	auto r = Fixture::req(character(150002), "abc");
	r.declared_size = 4;
	EXPECT_EQ(f.store.save(r), SaveOutcome::RejectedSizeMismatch);
	EXPECT_FALSE(f.store.load({character(150002), "mod:profile"}).has_value());
}

TEST(BR_CHAR_032, FinalAndRestoreSavesAreAcceptedOffline) {
	PENDING_RULE("BR-CHAR-032");
	Fixture f;
	auto fin = Fixture::req(character(150003), "abc");
	fin.final_save = true;
	EXPECT_EQ(f.store.save(fin), SaveOutcome::Accepted);
	auto res = Fixture::req(character(150004), "xyz");
	res.restore = true;
	EXPECT_EQ(f.store.save(res), SaveOutcome::Accepted);
	// the size check still applies to final and restore saves
	auto bad = Fixture::req(character(150005), "xyz");
	bad.final_save = true;
	bad.declared_size = 99;
	EXPECT_EQ(f.store.save(bad), SaveOutcome::RejectedSizeMismatch);
}

// Interpretation (generic form of "at most 20 cooldowns persisted; restored once on load"): the
// cap and restore-once flag are per-kind policy supplied by the ruleset; persistence has no
// notion of cooldowns. Over-cap is rejected, not silently truncated.
TEST(BR_CHAR_054, CollectionCapAcceptsExactlyMaxItemsAndRejectsMore) {
	PENDING_RULE("BR-CHAR-054");
	Fixture f;
	f.store.set_collection_policy("mod:timers", {20, true});
	const RecordKey key{character(150010), "mod:timers"};
	std::vector<Blob> items(20, blob("t"));
	EXPECT_NO_THROW(f.store.save_collection(key, items));
	items.push_back(blob("t"));
	EXPECT_THROW(f.store.save_collection(key, items), CollectionCapExceededError);
}

TEST(BR_CHAR_054, RestoreOnceCollectionIsHandedBackByTheFirstLoadOnly) {
	PENDING_RULE("BR-CHAR-054");
	Fixture f;
	f.store.set_collection_policy("mod:timers", {20, true});
	const RecordKey key{character(150011), "mod:timers"};
	f.store.save_collection(key, {blob("a"), blob("b")});
	EXPECT_EQ(f.store.load_collection(key).size(), 2u);
	EXPECT_TRUE(f.store.load_collection(key).empty());
}
