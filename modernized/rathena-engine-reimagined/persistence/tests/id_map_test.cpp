// Architecture 6.2: id_map persists client-visible numbers; freed numbers are tombstoned forever;
// collisions are load errors naming both claimants. Saves hold the namespaced string only.
#include "support.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

namespace {
const std::map<std::string, IdRange> kRanges{{"skill", {100000, 100999}}, {"item", {200000, 200999}}};
}

TEST(PERSIST_IDMAP_001, AllocatedNumbersAreStableAcrossRestarts) {
	auto db = memory_db();
	std::uint32_t first;
	{
		IdMap map(*db, kRanges);
		first = map.assign("skill", "mymod:skill/fireball");
		EXPECT_EQ(map.assign("skill", "mymod:skill/fireball"), first);  // idempotent
		EXPECT_GE(first, 100000u);
		EXPECT_LE(first, 100999u);
	}
	IdMap restarted(*db, kRanges);
	EXPECT_EQ(restarted.client_id("skill", "mymod:skill/fireball"), first);
	EXPECT_EQ(restarted.namespaced_id("skill", first), "mymod:skill/fireball");
}

TEST(PERSIST_IDMAP_002, RetiredNumberIsTombstonedAndNeverReassigned) {
	auto db = memory_db();
	IdMap map(*db, kRanges);
	const auto n = map.assign("item", "mymod:item/potion");
	map.retire("item", "mymod:item/potion");
	EXPECT_TRUE(map.is_tombstoned("item", n));
	const auto other = map.assign("item", "other:item/elixir");
	EXPECT_NE(other, n);
	EXPECT_THROW(map.assign("item", "third:item/x", n), IdCollisionError);  // explicit reuse refused
	IdMap restarted(*db, kRanges);
	EXPECT_TRUE(restarted.is_tombstoned("item", n));
}

TEST(PERSIST_IDMAP_003, ExplicitClientIdKeepsLegacyNumberAndCollisionNamesBothClaimants) {
	auto db = memory_db();
	IdMap map(*db, kRanges);
	EXPECT_EQ(map.assign("item", "legacy:item/red_potion", 501u), 501u);
	try {
		map.assign("item", "mymod:item/clone", 501u);
		FAIL() << "collision must throw";
	} catch (const IdCollisionError& e) {
		const std::string msg = e.what();
		EXPECT_NE(msg.find("legacy:item/red_potion"), std::string::npos);
		EXPECT_NE(msg.find("mymod:item/clone"), std::string::npos);
	}
}

TEST(PERSIST_IDMAP_004, RangeExhaustionIsAnErrorNotAWrapAround) {
	auto db = memory_db();
	IdMap map(*db, {{"status", {1, 2}}});
	map.assign("status", "m:status/a");
	map.assign("status", "m:status/b");
	EXPECT_THROW(map.assign("status", "m:status/c"), PersistError);
}
