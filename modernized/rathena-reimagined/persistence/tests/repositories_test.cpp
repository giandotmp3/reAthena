// The character-side repository contract (Character, Party, Guild, Mail, account Storage), run on
// every implementation: the in-memory fake, SQLite (:memory:, always) and PostgreSQL (when
// RATHENA_TEST_PG_URL is set; skipped otherwise). Test names carry the backend as the parameter
// suffix (/inmemory, /sqlite, /postgresql). The fake's behaviour is the contract; the SQL
// repositories run on the clean target schema (target_schema.hpp).
//
// Atomicity: `failing_at(table)` gives repositories whose next write to that table fails -- the
// fake arms its "write:<table>" fault, the SQL repositories get a TableNames in which that table
// does not exist -- so each multi-statement write is shown to roll back when it fails midway.
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <tuple>

#include "pg_test_support.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/foreign_keys.hpp"
#include "rathena/persistence/in_memory.hpp"
#include "rathena/persistence/item_mapping.hpp"
#include "rathena/persistence/postgres.hpp"
#include "rathena/persistence/sql_repositories.hpp"
#include "rathena/persistence/sqlite.hpp"
#include "rathena/persistence/target_schema.hpp"

using namespace rathena::persistence;

namespace {

enum class Backend { InMemory, Sqlite, Postgres };

std::string backend_name(Backend b) {
	switch (b) {
	case Backend::InMemory: return "inmemory";
	case Backend::Sqlite: return "sqlite";
	case Backend::Postgres: return "postgresql";
	}
	return "unknown";
}

struct Repos {
	std::unique_ptr<AccountRepository> accounts;
	std::unique_ptr<CharacterRepository> chars;
	std::unique_ptr<PartyRepository> parties;
	std::unique_ptr<GuildRepository> guilds;
	std::unique_ptr<MailRepository> mail;
	std::unique_ptr<StorageRepository> storage;
};

Item potion(std::uint32_t amount) {
	Item it;
	it.nameid = 501;
	it.amount = amount;
	it.identify = 1;
	return it;
}

/// An item that uses every column the inventory has: cards, random options (negative values
/// too), a 64-bit unique id, expiry, favourite, equip switch, enchant grade.
Item rich_item() {
	Item it;
	it.nameid = 1'201;
	it.amount = 1;
	it.equip = 0x2;
	it.identify = 1;
	it.refine = 10;
	it.attribute = 1;
	it.card = {4'001, 4'002, 0, 4'004};
	it.option[0] = {1, 50, 0};
	it.option[1] = {-3, -250, -1};
	it.option[4] = {32'000, 32'767, 127};
	it.expire_time = 1'900'000'000;
	it.favorite = 1;
	it.bound = 2;
	it.unique_id = 0x7fff'ffff'ffff'fff0ULL;
	it.equip_switch = 0x10;
	it.enchantgrade = 3;
	return it;
}

/// A rich item as the given table can hold it (the ONE mapping drops nothing silently).
Item rich_item_for(ItemTable t) {
	Item it = rich_item();
	const auto& p = profile(t);
	if (!p.has_equip) it.equip = 0;
	if (!p.has_expire_time) it.expire_time = 0;
	if (!p.has_favorite) it.favorite = 0;
	if (!p.has_equip_switch) it.equip_switch = 0;
	return it;
}

std::vector<Item> values_of(const std::vector<StoredItem>& items) {
	std::vector<Item> out;
	for (const auto& s : items) out.push_back(s.item);
	return out;
}

/// Every row has an id and they ascend (the contract's read order).
void expect_row_ids_assigned(const std::vector<StoredItem>& items) {
	for (std::size_t i = 0; i < items.size(); ++i) {
		EXPECT_NE(items[i].row_id, 0u) << "row " << i;
		if (i > 0) {
			EXPECT_LT(items[i - 1].row_id, items[i].row_id) << "rows are ordered by row id";
		}
	}
}

class RepositoryContract : public ::testing::TestWithParam<Backend> {
protected:
	void SetUp() override {
		switch (GetParam()) {
		case Backend::InMemory: db_ = std::make_unique<InMemoryDatabase>(); break;
		case Backend::Sqlite:
			sqlite_ = std::make_unique<SqliteConnection>(":memory:");
			conn_ = sqlite_.get();
			break;
		case Backend::Postgres:
			if (const auto why = pgtest::pg_unavailable()) GTEST_SKIP() << *why;
			pg_ = std::make_unique<pgtest::PgScratchSchema>();
			conn_ = &pg_->conn();
			break;
		}
		if (conn_ != nullptr) ASSERT_TRUE(create_target_schema(*conn_));
		repos_ = make(TableNames{});
	}

	Repos make(const TableNames& names) {
		Repos r;
		if (db_) {
			r.accounts = std::make_unique<InMemoryAccountRepository>(*db_);
			r.chars = std::make_unique<InMemoryCharacterRepository>(*db_);
			r.parties = std::make_unique<InMemoryPartyRepository>(*db_);
			r.guilds = std::make_unique<InMemoryGuildRepository>(*db_);
			r.mail = std::make_unique<InMemoryMailRepository>(*db_);
			r.storage = std::make_unique<InMemoryStorageRepository>(*db_);
		} else {
			r.accounts = std::make_unique<SqlAccountRepository>(*conn_, names);
			r.chars = std::make_unique<SqlCharacterRepository>(*conn_, names);
			r.parties = std::make_unique<SqlPartyRepository>(*conn_, names);
			r.guilds = std::make_unique<SqlGuildRepository>(*conn_, names);
			r.mail = std::make_unique<SqlMailRepository>(*conn_, names);
			r.storage = std::make_unique<SqlStorageRepository>(*conn_, names);
		}
		return r;
	}

	/// Repositories whose next write to `logical_table` (a TableNames field) fails.
	Repos failing_at(std::string_view logical_table) {
		if (db_) {
			db_->faults.fail_on("write:" + std::string(resolve_table(TableNames{}, logical_table)));
			return make(TableNames{});
		}
		TableNames broken;
		using F = std::string TableNames::*;
		const std::pair<std::string_view, F> fields[] = {
		    {"character", &TableNames::character},         {"cart_inventory", &TableNames::cart_inventory},
		    {"guild_member", &TableNames::guild_member},   {"guild_storage", &TableNames::guild_storage},
		    {"mail", &TableNames::mail},                   {"mail_attachments", &TableNames::mail_attachments},
		    {"storage", &TableNames::storage}};
		for (const auto& [name, field] : fields)
			if (name == logical_table) broken.*field = "missing_table_for_rollback_test";
		return make(broken);
	}

	CharacterRepository& chars() { return *repos_.chars; }
	PartyRepository& parties() { return *repos_.parties; }
	GuildRepository& guilds() { return *repos_.guilds; }
	MailRepository& mail() { return *repos_.mail; }
	StorageRepository& storage() { return *repos_.storage; }

	AccountId account(std::string userid) {
		Account a;
		a.userid = std::move(userid);
		a.pass_hash = "$argon2id$v=19$m=65536,t=2,p=1$ZmFrZXNhbHQ$ZmFrZWhhc2g"; // fake, same shape
		EXPECT_TRUE(repos_.accounts->create(a).ok());
		return a.id;
	}

	/// A stored character on a fresh account (the SQL schema requires the account to exist).
	Character character(std::string name) {
		Character c;
		c.account = account("acc_" + name);
		c.name = std::move(name);
		c.id = chars().create(c);
		return c;
	}

	/// Clans have no repository yet; the SQL schema needs the referenced row.
	void clan_exists(std::uint32_t id) {
		if (conn_ != nullptr)
			conn_->execute({"INSERT INTO \"clan\" (\"clan_id\", \"name\") VALUES (?, ?)", {std::uint64_t{id}, std::string("Clan")}});
	}

	Repos repos_;

private:
	std::unique_ptr<InMemoryDatabase> db_;
	std::unique_ptr<SqliteConnection> sqlite_;
	std::unique_ptr<pgtest::PgScratchSchema> pg_;
	SqlConnection* conn_ = nullptr;
};

} // namespace

INSTANTIATE_TEST_SUITE_P(Backends, RepositoryContract,
                         ::testing::Values(Backend::InMemory, Backend::Sqlite, Backend::Postgres),
                         [](const ::testing::TestParamInfo<Backend>& info) { return backend_name(info.param); });

// ---------------------------------------------------------------- characters

TEST_P(RepositoryContract, CharacterCreateAssignsIdsFromStartCharNum) {
	const auto acc = account("owner01");
	Character a;
	a.account = acc;
	a.name = "Novice01";
	a.zeny = Zeny::of(1'000);
	a.inventory.push_back({0, potion(5)});
	Character b = a;
	b.name = "Novice02";
	b.slot = 1;
	const auto id_a = chars().create(a);
	const auto id_b = chars().create(b);
	EXPECT_EQ(id_a.value, kStartCharNum);
	EXPECT_EQ(id_b.value, kStartCharNum + 1);
	const auto loaded = chars().find(id_a);
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->zeny.value(), 1'000);
	ASSERT_EQ(loaded->inventory.size(), 1u);
	EXPECT_EQ(loaded->inventory[0].item, potion(5));
	const auto listed = chars().list_by_account(acc);
	ASSERT_EQ(listed.size(), 2u);
	EXPECT_EQ(listed[0].id, id_a);
	EXPECT_EQ(listed[1].id, id_b);
	EXPECT_EQ(chars().find_by_name("Novice02")->id, id_b);
	EXPECT_FALSE(chars().find_by_name("novice02").has_value()) << "names match byte for byte";
	EXPECT_FALSE(chars().find(CharId{kStartCharNum + 99}).has_value());
}

TEST_P(RepositoryContract, CharacterRoundTripPreservesEveryField) {
	const auto partner = character("Partner1");
	const auto father = character("Father01");
	const auto mother = character("Mother01");
	Party p;
	p.name = "RoundTrip";
	const auto pid = parties().create(p);
	Guild g;
	g.name = "RoundTripGuild";
	g.master = partner.id;
	const auto gid = guilds().create(g);
	clan_exists(3);

	Character c;
	c.account = account("roundtrip");
	c.slot = 7;
	c.name = "RoundTrip";
	c.job_class = 4'054;
	c.base_level = 175;
	c.zeny = Zeny::of(123'456);
	c.party = pid;
	c.guild = gid;
	c.clan = 3;
	c.partner = partner.id;
	c.father = father.id;
	c.mother = mother.id;
	c.pet_id = 77;
	c.homun_id = 88;
	c.elemental_id = 99;
	c.delete_date = 1'800'000'000;
	c.id = chars().create(c);
	auto loaded = chars().find(c.id);
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(*loaded, c);

	c.base_level = 200;
	c.party.reset();
	c.guild.reset();
	c.clan.reset();
	c.partner.reset();
	c.pet_id.reset();
	c.delete_date = 0;
	chars().save(c);
	EXPECT_EQ(chars().find(c.id), c) << "unset references read back as none";
}

TEST_P(RepositoryContract, BR_MAP_045_ZenyBoundsSurviveARoundTrip) {
	auto c = character("Rich0001");
	c.zeny = Zeny::of(Zeny::kMax);
	chars().save(c);
	EXPECT_EQ(chars().find(c.id)->zeny.value(), Zeny::kMax);
	c.zeny = Zeny::of(0);
	chars().save(c);
	EXPECT_EQ(chars().find(c.id)->zeny.value(), 0);
}

TEST_P(RepositoryContract, CharacterNamesAreUnique) {
	auto first = character("SameName");
	Character dup;
	dup.account = first.account;
	dup.name = "SameName";
	EXPECT_THROW((void)chars().create(dup), RepositoryError);
	auto other = character("Other001");
	other.name = "SameName";
	EXPECT_THROW(chars().save(other), RepositoryError) << "rename onto a taken name";
	EXPECT_EQ(chars().find(other.id)->name, "Other001");
}

TEST_P(RepositoryContract, SaveOfAnUnknownCharacterThrows) {
	Character ghost;
	ghost.id = CharId{kStartCharNum + 500};
	ghost.account = account("ghost001");
	ghost.name = "Ghost001";
	EXPECT_THROW(chars().save(ghost), RepositoryError);
}

TEST_P(RepositoryContract, D6_ItemsRoundTripCardsRandomOptionsAndUniqueId) {
	auto c = character("Holder01");
	c.inventory = {{0, rich_item_for(ItemTable::Inventory)}, {0, potion(30)}};
	c.cart = {{0, rich_item_for(ItemTable::Cart)}, {0, potion(2)}};
	chars().save(c);
	auto loaded = chars().find(c.id);
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(values_of(loaded->inventory), values_of(c.inventory));
	EXPECT_EQ(values_of(loaded->cart), values_of(c.cart));
	expect_row_ids_assigned(loaded->inventory);
	expect_row_ids_assigned(loaded->cart);

	// Saving what was loaded keeps every row's identity; a new row gets a fresh, larger id; a
	// dropped row is gone.
	auto edited = *loaded;
	const auto kept = edited.inventory[0].row_id;
	edited.inventory.erase(edited.inventory.begin() + 1);
	edited.inventory.push_back({0, potion(1)});
	chars().save(edited);
	const auto again = chars().find(c.id);
	ASSERT_EQ(again->inventory.size(), 2u);
	EXPECT_EQ(again->inventory[0].row_id, kept);
	EXPECT_EQ(again->inventory[0].item, rich_item_for(ItemTable::Inventory));
	EXPECT_GT(again->inventory[1].row_id, loaded->inventory[1].row_id);
	EXPECT_EQ(again->inventory[1].item, potion(1));
	EXPECT_EQ(again->cart, loaded->cart);
}

TEST_P(RepositoryContract, D6_CartRejectsInventoryOnlyFieldsThroughTheOneMapping) {
	Character c;
	c.account = account("merchant");
	c.name = "Merchant1";
	Item it = potion(1);
	it.favorite = 1; // cart_inventory has no `favorite` column
	c.cart.push_back({0, it});
	EXPECT_THROW((void)chars().create(c), RepositoryError);
	EXPECT_FALSE(chars().find_by_name("Merchant1").has_value()) << "nothing was written";
}

TEST_P(RepositoryContract, CharacterCreateIsAtomic) {
	Character c;
	c.account = account("atomic01");
	c.name = "Atomic01";
	c.inventory.push_back({0, potion(1)});
	c.cart.push_back({0, potion(2)});
	auto failing = failing_at("cart_inventory"); // fails after the char row and its inventory
	EXPECT_THROW((void)failing.chars->create(c), RepositoryError);
	EXPECT_FALSE(chars().find_by_name("Atomic01").has_value());
	EXPECT_TRUE(chars().list_by_account(c.account).empty());
}

TEST_P(RepositoryContract, CharacterSaveIsAtomic) {
	auto c = character("Saver001");
	c.zeny = Zeny::of(10);
	c.inventory = {{0, potion(1)}};
	c.cart = {{0, potion(2)}};
	chars().save(c);
	const auto before = chars().find(c.id);

	auto changed = *before;
	changed.zeny = Zeny::of(999);
	changed.base_level = 99;
	changed.inventory.push_back({0, potion(3)});
	changed.cart.clear();
	auto failing = failing_at("cart_inventory"); // the char row and inventory are written first
	EXPECT_THROW(failing.chars->save(changed), RepositoryError);
	EXPECT_EQ(chars().find(c.id), before) << "a failed save leaves the previous state";
}

// ---------------------------------------------------------------- BR-CHAR-013 character delete

TEST_P(RepositoryContract, BR_CHAR_013_RemoveCleansUpRelatedData) {
	auto victim = character("Victim01");
	auto partner = character("Partner1");
	auto friend1 = character("Friend01");
	auto master2 = character("Master02");

	// married: both sides point at each other, the partner holds a ring
	victim.partner = partner.id;
	partner.partner = victim.id;
	partner.inventory = {{0, potion(7)}, {0, Item{2'635, 1, 0, 1, 0, 0, {}, {}, 0, 0, 0, 0, 0, 0}}};
	chars().save(partner);

	// victim leads party P (BR-CHAR-035: the party is dissolved), friend1 is a member
	Party p;
	p.name = "VictimParty";
	p.leader_char = victim.id;
	const auto pid = parties().create(p);
	victim.party = pid;
	friend1.party = pid;
	chars().save(friend1);

	// victim is master of guild G1 (broken) and friend1 is in it; guild G2 (master2) keeps going
	Guild g1;
	g1.name = "VictimGuild";
	g1.master = victim.id;
	g1.members = {{victim.id, 0, 0}, {friend1.id, 1, 5}};
	g1.storage = {{0, potion(9)}};
	const auto g1_id = guilds().create(g1);
	friend1.guild = g1_id;
	chars().save(friend1);
	Guild g2;
	g2.name = "OtherGuild";
	g2.master = master2.id;
	g2.members = {{master2.id, 0, 0}};
	const auto g2_id = guilds().create(g2);

	victim.inventory = {{0, potion(1)}};
	victim.cart = {{0, potion(2)}};
	chars().save(victim);

	Mail received;
	received.dest = victim.id;
	received.title = "for the victim";
	received.attachments = {potion(1), potion(2)};
	const auto received_id = mail().create(received);
	Mail sent;
	sent.sender = victim.id;
	sent.send_name = "Victim01";
	sent.dest = partner.id;
	sent.title = "from the victim";
	const auto sent_id = mail().create(sent);

	ASSERT_TRUE(chars().remove(victim.id));
	EXPECT_FALSE(chars().find(victim.id).has_value());
	EXPECT_FALSE(chars().find_by_name("Victim01").has_value());

	const auto p_after = chars().find(partner.id);
	EXPECT_FALSE(p_after->partner.has_value()) << "divorced";
	EXPECT_EQ(values_of(p_after->inventory), (std::vector<Item>{potion(7)})) << "the partner's ring went too";

	EXPECT_FALSE(parties().find(pid).has_value()) << "BR-CHAR-035: the leader left";
	EXPECT_FALSE(chars().find(friend1.id)->party.has_value());

	EXPECT_FALSE(guilds().find(g1_id).has_value()) << "the master's guild is broken";
	EXPECT_FALSE(chars().find(friend1.id)->guild.has_value());
	ASSERT_TRUE(guilds().find(g2_id).has_value());

	EXPECT_FALSE(mail().find(received_id).has_value()) << "received mail is deleted";
	const auto sent_after = mail().find(sent_id);
	ASSERT_TRUE(sent_after.has_value());
	EXPECT_FALSE(sent_after->sender.has_value()) << "sent mail now comes from the server";

	EXPECT_FALSE(chars().remove(victim.id));
}

TEST_P(RepositoryContract, BR_CHAR_013_RemovingAMemberLeavesTheGuildAndKeepsTheParty) {
	auto leader = character("Leader01");
	auto member = character("Member01");
	Party p;
	p.name = "Keepers";
	p.leader_char = leader.id;
	const auto pid = parties().create(p);
	leader.party = pid;
	member.party = pid;
	chars().save(leader);
	chars().save(member);
	Guild g;
	g.name = "KeepGuild";
	g.master = leader.id;
	g.members = {{leader.id, 0, 0}, {member.id, 1, 0}};
	const auto gid = guilds().create(g);

	ASSERT_TRUE(chars().remove(member.id));
	EXPECT_TRUE(parties().find(pid).has_value());
	EXPECT_EQ(parties().members(pid), (std::vector<CharId>{leader.id}));
	const auto g_after = guilds().find(gid);
	ASSERT_TRUE(g_after.has_value());
	EXPECT_EQ(g_after->members, (std::vector<GuildMember>{{leader.id, 0, 0}})) << "the member left";

	// BR-CHAR-036: the last member leaving dissolves the party
	ASSERT_TRUE(chars().remove(CharId{0}) == false);
	Party solo;
	solo.name = "Solo";
	solo.leader_char = CharId{1};
	const auto solo_id = parties().create(solo);
	auto loner = character("Loner001");
	loner.party = solo_id;
	chars().save(loner);
	ASSERT_TRUE(chars().remove(loner.id));
	EXPECT_FALSE(parties().find(solo_id).has_value());
}

TEST_P(RepositoryContract, BR_CHAR_013_RemoveIsAtomic) {
	auto victim = character("Victim02");
	auto partner = character("Partner2");
	victim.partner = partner.id;
	partner.partner = victim.id;
	chars().save(partner);
	Party p;
	p.name = "StaysParty";
	p.leader_char = victim.id;
	const auto pid = parties().create(p);
	victim.party = pid;
	victim.inventory = {{0, potion(1)}};
	chars().save(victim);
	Mail m;
	m.dest = victim.id;
	m.title = "kept";
	const auto mid = mail().create(m);
	const auto before = chars().find(victim.id);

	auto failing = failing_at("mail"); // after divorce, party dissolution and item deletes
	EXPECT_FALSE(failing.chars->remove(victim.id));
	EXPECT_EQ(chars().find(victim.id), before);
	EXPECT_EQ(chars().find(partner.id)->partner, victim.id) << "the divorce was rolled back";
	EXPECT_TRUE(parties().find(pid).has_value()) << "the dissolution was rolled back";
	EXPECT_TRUE(mail().find(mid).has_value());
}

// ---------------------------------------------------------------- parties

TEST_P(RepositoryContract, PartyCreateFindSaveAndMembers) {
	auto leader = character("PLeader1");
	auto second = character("PSecond1");
	Party p;
	p.name = "Adventurers";
	p.leader_account = leader.account;
	p.leader_char = leader.id;
	p.exp_share = true;
	p.id = parties().create(p);
	EXPECT_EQ(parties().find(p.id), p);

	Party dup;
	dup.name = "Adventurers";
	EXPECT_THROW((void)parties().create(dup), RepositoryError) << "BR-CHAR-033: unique name";

	p.exp_share = false;
	p.leader_char = second.id;
	parties().save(p);
	EXPECT_EQ(parties().find(p.id), p);

	EXPECT_TRUE(parties().members(p.id).empty());
	second.party = p.id;
	leader.party = p.id;
	chars().save(second);
	chars().save(leader);
	EXPECT_EQ(parties().members(p.id), (std::vector<CharId>{leader.id, second.id})) << "ordered by char id";

	Party ghost;
	ghost.id = PartyId{9'999};
	ghost.name = "Ghosts";
	EXPECT_THROW(parties().save(ghost), RepositoryError);
	EXPECT_FALSE(parties().find(PartyId{9'999}).has_value());
}

TEST_P(RepositoryContract, PartyRemoveClearsMembersPartyReference) {
	Party p;
	p.name = "Adventurers";
	const auto pid = parties().create(p);
	auto c = character("Member01");
	c.party = pid;
	chars().save(c);
	ASSERT_TRUE(parties().remove(pid));
	EXPECT_FALSE(parties().find(pid).has_value());
	EXPECT_FALSE(chars().find(c.id)->party.has_value());
	EXPECT_FALSE(parties().remove(pid));
}

TEST_P(RepositoryContract, PartyRemoveIsAtomic) {
	Party p;
	p.name = "Adventurers";
	const auto pid = parties().create(p);
	auto c = character("Member01");
	c.party = pid;
	chars().save(c);
	auto failing = failing_at("character"); // the party row goes first, then the members' reference
	EXPECT_FALSE(failing.parties->remove(pid));
	EXPECT_TRUE(parties().find(pid).has_value());
	EXPECT_EQ(chars().find(c.id)->party, pid);
}

// ---------------------------------------------------------------- guilds

TEST_P(RepositoryContract, GuildRoundTripMembersPositionsAndStorage) {
	auto master = character("GMaster1");
	auto m1 = character("GMember1");
	auto m2 = character("GMember2");
	Guild g;
	g.name = "Knights";
	g.master = master.id;
	g.master_name = "GMaster1";
	g.guild_lv = 12;
	g.members = {{m2.id, 2, 300}, {master.id, 0, 0}, {m1.id, 1, 1'000'000'000'000}};
	g.positions = {{2, "Recruit", 0x00, 0}, {0, "Master", 0x11, 10}, {1, "Officer", 0x01, 100}};
	g.storage = {{0, rich_item_for(ItemTable::GuildStorage)}, {0, potion(100)}};
	g.id = guilds().create(g);

	const auto loaded = guilds().find(g.id);
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->name, "Knights");
	EXPECT_EQ(loaded->master, master.id);
	EXPECT_EQ(loaded->master_name, "GMaster1");
	EXPECT_EQ(loaded->guild_lv, 12u);
	EXPECT_EQ(loaded->members, (std::vector<GuildMember>{{master.id, 0, 0}, {m1.id, 1, 1'000'000'000'000}, {m2.id, 2, 300}}))
	    << "by char id";
	EXPECT_EQ(loaded->positions,
	          (std::vector<GuildPosition>{{0, "Master", 0x11, 10}, {1, "Officer", 0x01, 100}, {2, "Recruit", 0x00, 0}}))
	    << "by index";
	EXPECT_EQ(values_of(loaded->storage), values_of(g.storage));
	expect_row_ids_assigned(loaded->storage);

	auto edited = *loaded;
	edited.guild_lv = 13;
	edited.members.pop_back();
	edited.positions[2].name = "Squire";
	edited.storage.push_back({0, potion(5)});
	guilds().save(edited);
	const auto again = guilds().find(g.id);
	ASSERT_TRUE(again.has_value());
	EXPECT_EQ(again->guild_lv, 13u);
	EXPECT_EQ(again->members, edited.members);
	EXPECT_EQ(again->positions, edited.positions);
	EXPECT_EQ(values_of(again->storage), values_of(edited.storage));
	EXPECT_EQ(again->storage[0].row_id, loaded->storage[0].row_id);
}

TEST_P(RepositoryContract, GuildNamesAreUniqueAndStorageUsesTheOneItemMapping) {
	auto master = character("GMaster1");
	Guild g;
	g.name = "Knights";
	g.master = master.id;
	g.storage.push_back({0, potion(100)});
	g.id = guilds().create(g);
	EXPECT_EQ(guilds().find(g.id)->storage.at(0).item.amount, 100u);

	Guild dup;
	dup.name = "Knights";
	dup.master = master.id;
	EXPECT_THROW((void)guilds().create(dup), RepositoryError) << "BR-CHAR-037: unique name";

	auto bad = *guilds().find(g.id);
	Item it = potion(1);
	it.equip_switch = 1; // guild_storage has no `equip_switch`
	bad.storage.push_back({0, it});
	EXPECT_THROW(guilds().save(bad), RepositoryError);
	EXPECT_EQ(guilds().find(g.id)->storage.size(), 1u);

	Guild ghost = g;
	ghost.id = GuildId{9'999};
	ghost.name = "Ghosts";
	EXPECT_THROW(guilds().save(ghost), RepositoryError);
}

TEST_P(RepositoryContract, GuildSaveIsAtomic) {
	auto master = character("GMaster1");
	auto member = character("GMember1");
	Guild g;
	g.name = "Knights";
	g.master = master.id;
	g.members = {{master.id, 0, 0}, {member.id, 1, 0}};
	g.positions = {{0, "Master", 0x11, 0}, {1, "Member", 0, 0}};
	g.storage = {{0, potion(1)}};
	g.id = guilds().create(g);
	const auto before = guilds().find(g.id);

	auto changed = *before;
	changed.guild_lv = 2;
	changed.members.pop_back();
	changed.positions[1].exp_mode = 50;
	changed.storage.clear();
	auto failing = failing_at("guild_storage"); // after the guild row, members and positions
	EXPECT_THROW(failing.guilds->save(changed), RepositoryError);
	EXPECT_EQ(guilds().find(g.id), before);
}

TEST_P(RepositoryContract, BR_CHAR_042_GuildRemoveDeletesAllGuildDataAndClearsMembers) {
	auto master = character("GMaster1");
	auto member = character("GMember1");
	Guild g;
	g.name = "Knights";
	g.master = master.id;
	g.members = {{master.id, 0, 0}, {member.id, 1, 0}};
	g.positions = {{0, "Master", 0x11, 0}};
	g.storage = {{0, potion(1)}};
	const auto gid = guilds().create(g);
	master.guild = gid;
	member.guild = gid;
	chars().save(master);
	chars().save(member);

	ASSERT_TRUE(guilds().remove(gid));
	EXPECT_FALSE(guilds().find(gid).has_value());
	EXPECT_FALSE(chars().find(master.id)->guild.has_value());
	EXPECT_FALSE(chars().find(member.id)->guild.has_value());
	EXPECT_FALSE(guilds().remove(gid));
	// The name is free again: nothing of the old guild is left behind.
	Guild again = g;
	EXPECT_NO_THROW((void)guilds().create(again));
}

TEST_P(RepositoryContract, GuildRemoveIsAtomic) {
	auto master = character("GMaster1");
	Guild g;
	g.name = "Knights";
	g.master = master.id;
	g.members = {{master.id, 0, 0}};
	g.storage = {{0, potion(1)}};
	const auto gid = guilds().create(g);
	master.guild = gid;
	chars().save(master);
	const auto before = guilds().find(gid);

	auto failing = failing_at("guild_member"); // after the members' reference, storage and positions
	EXPECT_FALSE(failing.guilds->remove(gid));
	EXPECT_EQ(guilds().find(gid), before);
	EXPECT_EQ(chars().find(master.id)->guild, gid);
}

// ---------------------------------------------------------------- mail

TEST_P(RepositoryContract, MailRoundTripWithSeveralAttachments) {
	auto sender = character("Sender01");
	auto dest = character("Receiver");
	Mail m;
	m.sender = sender.id;
	m.send_name = "Sender01";
	m.dest = dest.id;
	m.dest_name = "Receiver";
	m.title = "Supplies";
	m.message = "Five things for you.";
	m.zeny = Zeny::of(Zeny::kMax);
	m.time = 1'760'000'000;
	m.status = MailStatus::Unread;
	for (std::uint32_t i = 0; i < kMailMaxItems; ++i) {
		Item it = rich_item_for(ItemTable::MailAttachments);
		it.nameid += i;
		it.unique_id += i;
		m.attachments.push_back(it);
	}
	m.id = mail().create(m);
	EXPECT_EQ(mail().find(m.id), m) << "attachments come back in their order, cards/options/unique id intact";

	m.status = MailStatus::Read;
	m.zeny = Zeny::of(0);
	m.attachments.erase(m.attachments.begin() + 1, m.attachments.end()); // BR-CHAR-048: items taken
	mail().save(m);
	EXPECT_EQ(mail().find(m.id), m);

	Mail server;
	server.dest = dest.id;
	server.title = "From the server";
	server.id = mail().create(server);
	EXPECT_FALSE(mail().find(server.id)->sender.has_value()) << "no sender = the server";
}

TEST_P(RepositoryContract, MailInboxIsNewestFirstAndLimited) {
	auto dest = character("Receiver");
	auto other = character("Bystandr");
	for (int i = 0; i < 35; ++i) {
		Mail m;
		m.dest = i % 7 == 0 ? other.id : dest.id; // 5 of them for someone else
		m.title = "mail " + std::to_string(i);
		(void)mail().create(m);
	}
	const auto inbox = mail().inbox(dest.id, 25);
	ASSERT_EQ(inbox.size(), 25u);
	EXPECT_EQ(inbox.front().title, "mail 34");
	EXPECT_TRUE(std::is_sorted(inbox.begin(), inbox.end(), [](const Mail& a, const Mail& b) { return a.id > b.id; }));
	EXPECT_TRUE(std::all_of(inbox.begin(), inbox.end(), [&](const Mail& m) { return m.dest == dest.id; }));
	EXPECT_EQ(mail().inbox(dest.id, 100).size(), 30u);
	EXPECT_EQ(mail().inbox(other.id, 100).size(), 5u);
}

TEST_P(RepositoryContract, MailAttachmentLimitAndMappingEnforced) {
	auto dest = character("Receiver");
	Mail m;
	m.dest = dest.id;
	m.attachments.assign(kMailMaxItems + 1, potion(1));
	EXPECT_THROW((void)mail().create(m), RepositoryError);
	m.attachments.assign(1, potion(1));
	m.attachments[0].equip = 0x2; // mail_attachments has no `equip`
	EXPECT_THROW((void)mail().create(m), RepositoryError);
	EXPECT_TRUE(mail().inbox(dest.id, 30).empty());

	m.attachments.assign(1, potion(1));
	m.id = mail().create(m);
	m.attachments.assign(kMailMaxItems + 1, potion(1));
	EXPECT_THROW(mail().save(m), RepositoryError);
	EXPECT_EQ(mail().find(m.id)->attachments.size(), 1u);

	Mail ghost = m;
	ghost.id = MailId{9'999};
	ghost.attachments.clear();
	EXPECT_THROW(mail().save(ghost), RepositoryError);
}

TEST_P(RepositoryContract, MailCreateAndSaveAreAtomic) {
	auto dest = character("Receiver");
	Mail m;
	m.dest = dest.id;
	m.title = "atomic";
	m.attachments = {potion(1), potion(2)};
	{
		auto failing = failing_at("mail_attachments"); // after the mail row
		EXPECT_THROW((void)failing.mail->create(m), RepositoryError);
		EXPECT_TRUE(mail().inbox(dest.id, 30).empty());
	}
	m.id = mail().create(m);
	auto changed = m;
	changed.status = MailStatus::Read;
	changed.attachments.clear();
	auto failing = failing_at("mail_attachments");
	EXPECT_THROW(failing.mail->save(changed), RepositoryError);
	EXPECT_EQ(mail().find(m.id), m);
}

TEST_P(RepositoryContract, MailRemoveTakesTheAttachments) {
	auto dest = character("Receiver");
	Mail m;
	m.dest = dest.id;
	m.attachments = {potion(1), potion(2), potion(3)};
	const auto id = mail().create(m);
	ASSERT_TRUE(mail().remove(id));
	EXPECT_FALSE(mail().find(id).has_value());
	EXPECT_FALSE(mail().remove(id));
	m.attachments = {potion(4)};
	const auto next = mail().create(m);
	EXPECT_EQ(mail().find(next)->attachments, (std::vector<Item>{potion(4)})) << "no stale attachment rows";
}

// ---------------------------------------------------------------- account storage

TEST_P(RepositoryContract, D6_StorageRoundTripThroughTheOneItemMapping) {
	const auto acc = account("storage1");
	EXPECT_TRUE(storage().load(acc).empty());
	const std::vector<StoredItem> items = {{0, rich_item_for(ItemTable::Storage)}, {0, potion(100)}};
	storage().save(acc, items);
	auto loaded = storage().load(acc);
	EXPECT_EQ(values_of(loaded), values_of(items));
	expect_row_ids_assigned(loaded);

	Item bad = potion(1);
	bad.favorite = 1; // storage has no `favorite`
	auto with_bad = loaded;
	with_bad.push_back({0, bad});
	EXPECT_THROW(storage().save(acc, with_bad), RepositoryError);
	EXPECT_EQ(storage().load(acc), loaded);

	loaded.erase(loaded.begin());
	storage().save(acc, loaded);
	EXPECT_EQ(storage().load(acc), loaded);
	storage().save(acc, {});
	EXPECT_TRUE(storage().load(acc).empty());
}

// ---------------------------------------------------------------- target schema (SQL backends)

namespace {

class TargetSchemaContract : public ::testing::TestWithParam<SqlDialect> {
protected:
	void SetUp() override {
		if (GetParam() == SqlDialect::Postgres) {
			if (const auto why = pgtest::pg_unavailable()) GTEST_SKIP() << *why;
			pg_ = std::make_unique<pgtest::PgScratchSchema>();
			conn_ = &pg_->conn();
		} else {
			sqlite_ = std::make_unique<SqliteConnection>(":memory:");
			conn_ = sqlite_.get();
		}
		ASSERT_TRUE(create_target_schema(*conn_));
	}
	SqlConnection& conn() { return *conn_; }

	CharId character(SqlAccountRepository& accounts, SqlCharacterRepository& chars, std::string name) {
		Account a;
		a.userid = "acc_" + name;
		EXPECT_TRUE(accounts.create(a).ok());
		Character c;
		c.account = a.id;
		c.name = std::move(name);
		return chars.create(c);
	}

private:
	std::unique_ptr<SqliteConnection> sqlite_;
	std::unique_ptr<pgtest::PgScratchSchema> pg_;
	SqlConnection* conn_ = nullptr;
};

std::string text(const SqlValue& v) {
	if (const auto* s = std::get_if<std::string>(&v)) return *s;
	return {};
}

} // namespace

INSTANTIATE_TEST_SUITE_P(Backends, TargetSchemaContract, ::testing::Values(SqlDialect::Sqlite, SqlDialect::Postgres),
                         [](const ::testing::TestParamInfo<SqlDialect>& info) { return std::string(to_string(info.param)); });

TEST_P(TargetSchemaContract, BR_MAP_045_ZenyChecksRejectWhatZenyCannotHold) {
	SqlAccountRepository accounts(conn(), TableNames{});
	SqlCharacterRepository chars(conn(), TableNames{});
	const auto id = character(accounts, chars, "Checked1");
	for (const std::int64_t bad : {std::int64_t{-1}, std::int64_t{Zeny::kMax} + 1})
		EXPECT_THROW(conn().execute({"UPDATE \"char\" SET \"zeny\" = ? WHERE \"char_id\" = ?", {bad, std::uint64_t{id.value}}}),
		             RepositoryError)
		    << bad;
	EXPECT_NO_THROW(conn().execute({"UPDATE \"char\" SET \"zeny\" = ? WHERE \"char_id\" = ?",
	                                {std::int64_t{Zeny::kMax}, std::uint64_t{id.value}}}));
}

TEST_P(TargetSchemaContract, ForeignKeysAreExactlyTargetForeignKeys) {
	using Fk = std::tuple<std::string, std::string, std::string, std::string, std::string>;
	auto rule = [](OnDelete a) { return a == OnDelete::Cascade ? "CASCADE" : a == OnDelete::SetNull ? "SET NULL" : "RESTRICT"; };
	std::set<Fk> expected;
	const TableNames names;
	for (const auto& fk : target_foreign_keys())
		expected.insert({std::string(resolve_table(names, fk.child_table)), std::string(fk.column),
		                 std::string(resolve_table(names, fk.parent_table)), std::string(fk.parent_column), rule(fk.on_delete)});
	std::set<Fk> actual;
	if (GetParam() == SqlDialect::Sqlite) {
		for (const auto& t : conn().query({"SELECT \"name\" FROM \"sqlite_master\" WHERE \"type\" = 'table'", {}}))
			for (const auto& r : conn().query({"SELECT * FROM pragma_foreign_key_list(?)", {text(t.at("name"))}}))
				actual.insert({text(t.at("name")), text(r.at("from")), text(r.at("table")), text(r.at("to")), text(r.at("on_delete"))});
	} else {
		for (const auto& r : conn().query(
		         {"SELECT kcu.table_name AS child, kcu.column_name AS col, ccu.table_name AS parent, ccu.column_name AS pcol,"
		          " rc.delete_rule AS rule FROM information_schema.referential_constraints rc"
		          " JOIN information_schema.key_column_usage kcu ON kcu.constraint_name = rc.constraint_name"
		          "  AND kcu.constraint_schema = rc.constraint_schema"
		          " JOIN information_schema.constraint_column_usage ccu ON ccu.constraint_name = rc.constraint_name"
		          "  AND ccu.constraint_schema = rc.constraint_schema"
		          " WHERE rc.constraint_schema = current_schema()",
		          {}}))
			actual.insert({text(r.at("child")), text(r.at("col")), text(r.at("parent")), text(r.at("pcol")), text(r.at("rule"))});
	}
	EXPECT_EQ(actual, expected);
}

TEST_P(TargetSchemaContract, CharactersNeedTheirAccountAndKeepIt) {
	SqlAccountRepository accounts(conn(), TableNames{});
	SqlCharacterRepository chars(conn(), TableNames{});
	Character orphan;
	orphan.account = AccountId{2'500'000};
	orphan.name = "Orphan01";
	EXPECT_THROW((void)chars.create(orphan), RepositoryError) << "FK char.account_id -> login";
	const auto id = character(accounts, chars, "Owned001");
	const auto owner = chars.find(id)->account;
	EXPECT_FALSE(accounts.remove(owner)) << "RESTRICT: an account with characters is not deleted (BR-LOGIN-037 rolls back)";
	EXPECT_TRUE(accounts.find(owner).has_value());
}

TEST_P(TargetSchemaContract, BR_CHAR_013_AGuildMasterRowCannotBeDeletedDirectly) {
	SqlAccountRepository accounts(conn(), TableNames{});
	SqlCharacterRepository chars(conn(), TableNames{});
	SqlGuildRepository guilds(conn(), TableNames{});
	const auto master = character(accounts, chars, "GMaster1");
	Guild g;
	g.name = "Knights";
	g.master = master;
	(void)guilds.create(g);
	EXPECT_THROW(conn().execute({"DELETE FROM \"char\" WHERE \"char_id\" = ?", {std::uint64_t{master.value}}}), RepositoryError)
	    << "RESTRICT: the plan breaks the guild first";
	EXPECT_TRUE(chars.remove(master)) << "the repository runs the plan, so the delete succeeds";
}

TEST(TargetSchema, CharacterTablesKeepTheLegacyTableAndColumnNames) {
	// The legacy servers and the reimagined units share table and column names; the target only
	// drops login.user_pass (HITL #1) and adds login.pass_hash/pass_scheme.
	std::ifstream in(std::string(RATHENA_LEGACY_SQL_DIR) + "/sqlite/main.sql");
	ASSERT_TRUE(in.good());
	const std::string legacy_sql((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	SqliteConnection legacy(":memory:");
	legacy.execute_script(legacy_sql);
	SqliteConnection target(":memory:");
	ASSERT_TRUE(create_target_schema(target));
	auto columns = [](SqliteConnection& c, const std::string& table) {
		std::set<std::string> out;
		for (const auto& r : c.query({"SELECT \"name\" FROM pragma_table_info(?)", {table}})) out.insert(text(r.at("name")));
		return out;
	};
	std::size_t compared = 0;
	for (const auto& t : target.query({"SELECT \"name\" FROM \"sqlite_master\" WHERE \"type\" = 'table'"
	                                   " AND \"name\" NOT IN ('sqlite_sequence', 'schema_migrations')",
	                                   {}})) {
		const auto table = text(t.at("name"));
		auto legacy_cols = columns(legacy, table);
		ASSERT_FALSE(legacy_cols.empty()) << table << " is not a legacy table";
		auto target_cols = columns(target, table);
		if (table == "login") {
			legacy_cols.erase("user_pass");
			target_cols.erase("pass_hash");
			target_cols.erase("pass_scheme");
		}
		EXPECT_EQ(target_cols, legacy_cols) << table;
		++compared;
	}
	EXPECT_EQ(compared, 29u);
}
