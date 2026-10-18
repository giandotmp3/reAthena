// BR-CHAR-050 (registry scopes; 0 or empty deletes the entry), BR-INTER-015 (global registry
// routing by scope), BR-LOGIN-036 (login-wide `##` registry shared across accounts).
// The sigil -> OwnerScope mapping belongs to the `legacy` mod; persistence guarantees the scope
// partitions and the delete-on-zero compatibility switch.
#include "support.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;

namespace {
struct Fixture {
	std::unique_ptr<SqlConnection> db = memory_db();
	Quarantine quarantine{*db};
	KvStore kv;
	explicit Fixture(bool legacy_delete = false) : kv(*db, quarantine, KvPolicy{1u << 20, legacy_delete}) {}
};
} // namespace

TEST(BR_CHAR_050, ScopesArePartitionedCharacterAccountGlobal) {
	Fixture f;
	f.kv.set({character(1), "legacy", "x"}, Value{std::int64_t{1}});
	f.kv.set({account(1), "legacy", "x"}, Value{std::int64_t{2}});
	f.kv.set({global_account(), "legacy", "x"}, Value{std::int64_t{3}});
	EXPECT_EQ(std::get<std::int64_t>(f.kv.get({character(1), "legacy", "x"})->data), 1);
	EXPECT_EQ(std::get<std::int64_t>(f.kv.get({account(1), "legacy", "x"})->data), 2);
	EXPECT_EQ(std::get<std::int64_t>(f.kv.get({global_account(), "legacy", "x"})->data), 3);
}

TEST(BR_CHAR_050, ZeroOrEmptyValueDeletesEntryWhenCompatibilitySwitchIsOn) {
	Fixture f(true);
	const KvKey k{character(2), "legacy", "n"};
	f.kv.set(k, Value{std::int64_t{5}});
	f.kv.set(k, Value{std::int64_t{0}});
	EXPECT_FALSE(f.kv.get(k).has_value());
	const KvKey s{character(2), "legacy", "s"};
	f.kv.set(s, Value{"text"});
	f.kv.set(s, Value{""});
	EXPECT_FALSE(f.kv.get(s).has_value());
}

TEST(BR_CHAR_050, ZeroIsStoredAsIsWhenCompatibilitySwitchIsOff) {
	Fixture f(false);
	const KvKey k{character(3), "mymod", "n"};
	f.kv.set(k, Value{std::int64_t{0}});
	ASSERT_TRUE(f.kv.get(k).has_value());
}

TEST(BR_INTER_015, GlobalScopeEntriesAreNotVisibleInAccountScopeAndViceVersa) {
	Fixture f;
	f.kv.set({global_account(), "legacy", "only_global"}, Value{std::int64_t{9}});
	f.kv.set({account(7), "legacy", "only_account"}, Value{std::int64_t{8}});
	EXPECT_FALSE(f.kv.get({account(7), "legacy", "only_global"}).has_value());
	EXPECT_FALSE(f.kv.get({global_account(), "legacy", "only_account"}).has_value());
}

TEST(BR_LOGIN_036, LoginWideRegistryIsOneStoreSharedByEveryAccount) {
	Fixture f;
	f.kv.set({global_account(), "legacy", "shared"}, Value{std::int64_t{42}});
	// any account's view of the login-wide scope resolves to the same single row
	auto v = f.kv.get({global_account(), "legacy", "shared"});
	ASSERT_TRUE(v.has_value());
	EXPECT_EQ(std::get<std::int64_t>(v->data), 42);
	f.kv.set({global_account(), "legacy", "shared"}, Value{std::int64_t{43}});
	EXPECT_EQ(std::get<std::int64_t>(f.kv.get({global_account(), "legacy", "shared"})->data), 43);
}
