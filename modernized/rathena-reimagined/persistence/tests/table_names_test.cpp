// M5 / defect D2: table-name defaults are correct (legacy char.cpp:2726 overwrote clan_table
// with "clan_alliance", left clan_alliance_table empty, and had no elemental default).
// Plus identifier safety for operator-supplied table-name overrides.
#include <gtest/gtest.h>

#include "rathena/persistence/table_names.hpp"

using namespace rathena::persistence;

TEST(M5_TableNames, M5_ClanAndElementalDefaultsAreCorrect) {
	const TableNames t;
	EXPECT_EQ(t.clan, "clan");
	EXPECT_EQ(t.clan_alliance, "clan_alliance");
	EXPECT_EQ(t.elemental, "elemental");
	EXPECT_EQ(t.get("clan_table"), "clan");
	EXPECT_EQ(t.get("clan_alliance_table"), "clan_alliance");
	EXPECT_EQ(t.get("elemental_db"), "elemental");
}

TEST(M5_TableNames, M5_OverrideOneKeyDoesNotClobberAnother) {
	TableNames t;
	ASSERT_TRUE(t.set("clan_alliance_table", "my_clan_alliance"));
	EXPECT_EQ(t.clan, "clan");
	EXPECT_EQ(t.clan_alliance, "my_clan_alliance");
}

TEST(M5_TableNames, M5_RejectsUnsafeOrUnknownOverrides) {
	TableNames t;
	EXPECT_FALSE(t.set("char_db", "char`; DROP TABLE login; --"));
	EXPECT_FALSE(t.set("char_db", ""));
	EXPECT_FALSE(t.set("no_such_key", "x"));
	EXPECT_EQ(t.character, "char");
	EXPECT_THROW((void)quote_identifier("a b"), std::invalid_argument);
	EXPECT_EQ(quote_identifier("char"), "`char`");
}
