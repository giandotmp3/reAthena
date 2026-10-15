// Item value type + its ONE persistence mapping (spec §2.2, defect D6 handled in code).
// Not a BR rule: the tests are named after the defect id (D6) and the tables involved.
#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/item_mapping.hpp"
#include "rathena/persistence/table_names.hpp"

using namespace rathena::persistence;

namespace {

Item sample(bool with_inventory_only_fields) {
	Item it;
	it.nameid = 1'201;
	it.amount = 30'000;
	it.identify = 1;
	it.refine = 10;
	it.attribute = 1;
	it.card = {4'001, 4'002, 0, 255};
	it.option[0] = {1, -25, 3};
	it.option[4] = {std::numeric_limits<std::int16_t>::max(), std::numeric_limits<std::int16_t>::min(), -1};
	it.bound = 2;
	it.unique_id = 0xFEDCBA9876543210ULL;
	it.enchantgrade = 4;
	if (with_inventory_only_fields) {
		it.equip = 0x2;
		it.expire_time = 1'700'000'000;
		it.favorite = 1;
		it.equip_switch = 0x20;
	}
	return it;
}

} // namespace

TEST(D6_ItemMapping, D6_RoundTripsThroughEveryItemTable) {
	for (const auto t : all_item_tables()) {
		const auto& p = profile(t);
		Item it = sample(false);
		if (p.has_equip) it.equip = 0x2;
		if (p.has_expire_time) it.expire_time = 1'700'000'000;
		if (p.has_favorite) it.favorite = 1;
		if (p.has_equip_switch) it.equip_switch = 0x20;
		EXPECT_EQ(from_row(to_row(it, t), t), it) << to_string(t);
	}
}

TEST(D6_ItemMapping, D6_InventoryCarriesEveryField) {
	const Item it = sample(true);
	EXPECT_EQ(from_row(to_row(it, ItemTable::Inventory), ItemTable::Inventory), it);
}

TEST(D6_ItemMapping, D6_ColumnSetsMatchLegacySchema) {
	// Column order/presence from sql-files/main.sql.
	EXPECT_EQ(item_columns(ItemTable::Inventory).size(), 31u);
	EXPECT_EQ(item_columns(ItemTable::Cart).size(), 29u);
	EXPECT_EQ(item_columns(ItemTable::Storage).size(), 29u);
	EXPECT_EQ(item_columns(ItemTable::GuildStorage).size(), 29u);
	EXPECT_EQ(item_columns(ItemTable::MailAttachments).size(), 27u);
	const auto mail = item_columns(ItemTable::MailAttachments);
	EXPECT_EQ(std::count(mail.begin(), mail.end(), "equip"), 0);
	EXPECT_EQ(std::count(mail.begin(), mail.end(), "expire_time"), 0);
}

TEST(D6_ItemMapping, D6_ToRowRefusesSilentLoss) {
	Item it = sample(false);
	it.equip = 0x2;
	EXPECT_THROW((void)to_row(it, ItemTable::MailAttachments), MappingError);
	it = sample(false);
	it.favorite = 1;
	EXPECT_THROW((void)to_row(it, ItemTable::Storage), MappingError);
}

TEST(D6_ItemMapping, D6_FromRowRejectsOutOfRangeValues) {
	Row r = to_row(sample(false), ItemTable::Cart);
	r["refine"] = std::int64_t{256};
	EXPECT_THROW((void)from_row(r, ItemTable::Cart), MappingError);
	r = to_row(sample(false), ItemTable::Cart);
	r["amount"] = std::int64_t{-1};
	EXPECT_THROW((void)from_row(r, ItemTable::Cart), MappingError);
	r = to_row(sample(false), ItemTable::Cart);
	r["option_id0"] = std::int64_t{40'000};
	EXPECT_THROW((void)from_row(r, ItemTable::Cart), MappingError);
}

TEST(D6_ItemMapping, D6_FromRowRejectsMissingOrNullColumns) {
	Row r = to_row(sample(false), ItemTable::Storage);
	r.erase("unique_id");
	EXPECT_THROW((void)from_row(r, ItemTable::Storage), MappingError);
	r = to_row(sample(false), ItemTable::Storage);
	r["nameid"] = std::monostate{};
	EXPECT_THROW((void)from_row(r, ItemTable::Storage), MappingError);
}

TEST(D6_ItemMapping, D6_AmountIsUnsigned32AfterM4) {
	// D3: legacy C++ used int16 for amount; storage is INT UNSIGNED.
	Item it = sample(false);
	it.amount = 100'000;
	EXPECT_EQ(from_row(to_row(it, ItemTable::Storage), ItemTable::Storage).amount, 100'000u);
}

TEST(D6_ItemMapping, D6_InsertStatementIsParameterized) {
	const TableNames names;
	const auto st = insert_statement(names, ItemTable::GuildStorage, 42, sample(false));
	EXPECT_EQ(st.sql.rfind("INSERT INTO \"guild_storage\" (\"guild_id\", \"nameid\", \"amount\"", 0), 0u);
	EXPECT_EQ(static_cast<std::size_t>(std::count(st.sql.begin(), st.sql.end(), '?')), st.params.size());
	EXPECT_EQ(st.params.size(), 1u + item_columns(ItemTable::GuildStorage).size());
	EXPECT_EQ(std::get<std::uint64_t>(st.params[0]), 42u);
}

TEST(D6_ItemMapping, D6_MailAttachmentsNeedAnIndex) {
	const TableNames names;
	EXPECT_THROW((void)insert_statement(names, ItemTable::MailAttachments, 7, sample(false)), MappingError);
	const auto st = insert_statement(names, ItemTable::MailAttachments, 7, sample(false), 2u);
	EXPECT_NE(st.sql.find("(\"id\", \"index\", \"nameid\""), std::string::npos);
	EXPECT_THROW((void)insert_statement(names, ItemTable::Inventory, 7, sample(false), 2u), MappingError);
}

TEST(D6_ItemMapping, D6_StoredRowsKeepTheirRowIdAndReadInRowIdOrder) {
	const TableNames names;
	const auto fresh = insert_statement(names, ItemTable::Cart, 7, StoredItem{0, sample(false)});
	EXPECT_EQ(fresh.sql.rfind("INSERT INTO \"cart_inventory\" (\"char_id\", ", 0), 0u) << "row id 0: the database assigns one";
	const auto kept = insert_statement(names, ItemTable::Cart, 7, StoredItem{42, sample(false)});
	EXPECT_EQ(kept.sql.rfind("INSERT INTO \"cart_inventory\" (\"id\", \"char_id\", ", 0), 0u);
	EXPECT_EQ(std::get<std::uint64_t>(kept.params[0]), 42u);
	EXPECT_EQ(kept.params.size(), fresh.params.size() + 1);
	EXPECT_THROW((void)insert_statement(names, ItemTable::MailAttachments, 7, StoredItem{1, sample(false)}), MappingError);
	EXPECT_NE(select_statement(names, ItemTable::Cart, 7).sql.find("ORDER BY \"id\""), std::string::npos);
	EXPECT_NE(select_statement(names, ItemTable::MailAttachments, 7).sql.find("ORDER BY \"index\""), std::string::npos);
	EXPECT_EQ(delete_statement(names, ItemTable::Storage, 9).sql, "DELETE FROM \"storage\" WHERE \"account_id\" = ?");
}

TEST(D6_ItemMapping, D6_SelectUsesConfiguredTableName) {
	TableNames names;
	ASSERT_TRUE(names.set("inventory_db", "inventory_custom"));
	const auto st = select_statement(names, ItemTable::Inventory, 150'000);
	EXPECT_NE(st.sql.find("FROM \"inventory_custom\" WHERE \"char_id\" = ?"), std::string::npos);
}
