// Repository contract smoke tests against the in-memory fakes (Character, Party, Guild, Mail).
// Not BR rules themselves; they pin the CRUD contract the char/map refactors rely on.
#include <gtest/gtest.h>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/in_memory.hpp"
#include "rathena/persistence/sql_repositories.hpp"
#include "rathena/persistence/sqlite.hpp"

using namespace rathena::persistence;

namespace {

Item potion(std::uint32_t amount) {
	Item it;
	it.nameid = 501;
	it.amount = amount;
	it.identify = 1;
	return it;
}

} // namespace

TEST(CharacterRepository, CreateAssignsIdsFromStartCharNum) {
	InMemoryDatabase db;
	InMemoryCharacterRepository repo(db);
	Character c;
	c.account = AccountId{2'000'000};
	c.name = "Novice01";
	c.zeny = Zeny::of(1'000);
	c.inventory.push_back({0, potion(5)});
	const auto id = repo.create(c);
	EXPECT_EQ(id.value, kStartCharNum);
	const auto loaded = repo.find(id);
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->zeny.value(), 1'000);
	EXPECT_EQ(loaded->inventory.at(0).item, potion(5));
	EXPECT_EQ(repo.list_by_account(AccountId{2'000'000}).size(), 1u);
}

TEST(CharacterRepository, DuplicateNameIsRejected) {
	InMemoryDatabase db;
	InMemoryCharacterRepository repo(db);
	Character c;
	c.name = "SameName";
	(void)repo.create(c);
	EXPECT_THROW((void)repo.create(c), RepositoryError);
}

TEST(CharacterRepository, CartRejectsInventoryOnlyFieldsThroughTheOneMapping) {
	InMemoryDatabase db;
	InMemoryCharacterRepository repo(db);
	Character c;
	c.name = "Merchant1";
	Item it = potion(1);
	it.favorite = 1; // cart_inventory has no `favorite` column
	c.cart.push_back({0, it});
	EXPECT_THROW((void)repo.create(c), RepositoryError);
}

TEST(CharacterRepository, FailedSaveLeavesPreviousState) {
	InMemoryDatabase db;
	InMemoryCharacterRepository repo(db);
	Character c;
	c.name = "Saver001";
	c.zeny = Zeny::of(10);
	c.id = repo.create(c);
	c.zeny = Zeny::of(999);
	db.faults.fail_on("update:char");
	EXPECT_THROW(repo.save(c), RepositoryError);
	EXPECT_EQ(repo.find(c.id)->zeny.value(), 10);
}

TEST(PartyRepository, RemoveClearsMembersPartyReference) {
	InMemoryDatabase db;
	InMemoryCharacterRepository chars(db);
	InMemoryPartyRepository parties(db);
	Party p;
	p.name = "Adventurers";
	const auto pid = parties.create(p);
	Character c;
	c.name = "Member01";
	c.party = pid;
	const auto cid = chars.create(c);
	ASSERT_TRUE(parties.remove(pid));
	EXPECT_FALSE(chars.find(cid)->party.has_value());
}

TEST(PartyRepository, RemoveIsAtomic) {
	InMemoryDatabase db;
	InMemoryCharacterRepository chars(db);
	InMemoryPartyRepository parties(db);
	Party p;
	p.name = "Adventurers";
	const auto pid = parties.create(p);
	Character c;
	c.name = "Member01";
	c.party = pid;
	const auto cid = chars.create(c);
	db.faults.fail_on("update:char.party_id");
	EXPECT_FALSE(parties.remove(pid));
	EXPECT_TRUE(parties.find(pid).has_value());
	EXPECT_EQ(chars.find(cid)->party, pid);
}

TEST(GuildRepository, StorageUsesTheOneItemMapping) {
	InMemoryDatabase db;
	InMemoryGuildRepository guilds(db);
	Guild g;
	g.name = "Knights";
	g.storage.push_back({0, potion(100)});
	const auto gid = guilds.create(g);
	EXPECT_EQ(guilds.find(gid)->storage.at(0).item.amount, 100u);
	Item bad = potion(1);
	bad.equip_switch = 1; // guild_storage has no `equip_switch`
	g.id = gid;
	g.storage.push_back({0, bad});
	EXPECT_THROW(guilds.save(g), RepositoryError);
}

TEST(MailRepository, InboxIsNewestFirstAndLimited) {
	InMemoryDatabase db;
	InMemoryMailRepository mail(db);
	for (int i = 0; i < 35; ++i) {
		Mail m;
		m.dest = CharId{150'001};
		m.title = "mail " + std::to_string(i);
		(void)mail.create(m);
	}
	const auto inbox = mail.inbox(CharId{150'001}, 30);
	ASSERT_EQ(inbox.size(), 30u);
	EXPECT_EQ(inbox.front().title, "mail 34");
}

TEST(MailRepository, AttachmentLimitAndMappingEnforced) {
	InMemoryDatabase db;
	InMemoryMailRepository mail(db);
	Mail m;
	m.dest = CharId{150'001};
	m.attachments.assign(kMailMaxItems + 1, potion(1));
	EXPECT_THROW((void)mail.create(m), RepositoryError);
	m.attachments.assign(1, potion(1));
	m.attachments[0].equip = 0x2; // mail_attachments has no `equip`
	EXPECT_THROW((void)mail.create(m), RepositoryError);
}

TEST(SqlRepositories, NotYetPortedRepositoriesReportNotImplemented) {
	// Character, party, guild and mail move to the portable subset in DATABASE_DECISION.md
	// increment 3; until then they must fail loudly rather than silently do nothing.
	SqliteConnection conn(":memory:");
	SqlCharacterRepository chars(conn, TableNames{});
	SqlPartyRepository parties(conn, TableNames{});
	SqlGuildRepository guilds(conn, TableNames{});
	SqlMailRepository mail(conn, TableNames{});
	EXPECT_THROW((void)chars.find(CharId{150'000}), NotImplemented);
	EXPECT_THROW((void)parties.find(PartyId{1}), NotImplemented);
	EXPECT_THROW((void)guilds.find(GuildId{1}), NotImplemented);
	EXPECT_THROW((void)mail.find(MailId{1}), NotImplemented);
}
