#include "rathena/persistence/in_memory.hpp"

#include <algorithm>
#include <cctype>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/item_mapping.hpp"

namespace rathena::persistence {

void FaultInjector::check(const std::string& label) {
	if (armed_.erase(label) > 0) throw RepositoryError("injected fault: " + label);
}

namespace {

/// Runs `fn` against `db`; on any exception the database is restored (all-or-nothing).
/// The FaultInjector is not part of the snapshot (it is test control state).
template <class Fn>
bool transactional(InMemoryDatabase& db, Fn&& fn) {
	FaultInjector faults = db.faults;
	InMemoryDatabase snapshot = db;
	try {
		fn();
		return true;
	} catch (const RepositoryError&) {
		faults = db.faults; // keep consumed one-shot faults consumed
		db = std::move(snapshot);
		db.faults = std::move(faults);
		return false;
	}
}

bool iequals(std::string_view a, std::string_view b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		       return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	       });
}

/// Every stored item must be representable by the ONE mapping for its table (D6 in code).
void check_items(const std::vector<StoredItem>& items, ItemTable t) {
	for (const auto& s : items) {
		try {
			(void)to_row(s.item, t);
		} catch (const MappingError& e) {
			throw RepositoryError(e.what());
		}
	}
}

} // namespace

// ------------------------------------------------------------------ accounts

std::optional<Account> InMemoryAccountRepository::find(AccountId id) const {
	if (const auto it = db_.accounts.find(id); it != db_.accounts.end()) return it->second;
	return std::nullopt;
}

std::optional<Account> InMemoryAccountRepository::find_by_userid(std::string_view userid, bool case_sensitive) const {
	std::optional<Account> found;
	for (const auto& [id, acc] : db_.accounts) {
		const bool match = case_sensitive ? acc.userid == userid : iequals(acc.userid, userid);
		if (!match) continue;
		if (found) return std::nullopt; // BR-LOGIN-038: more than one match -> deny
		found = acc;
	}
	return found;
}

AccountIdAllocation InMemoryAccountRepository::create(Account& account, std::optional<std::uint32_t> requested) {
	std::optional<std::uint32_t> current_max;
	if (!db_.accounts.empty()) current_max = db_.accounts.rbegin()->first.value;
	const auto alloc = allocate_account_id(requested, current_max);
	if (!alloc.ok()) return alloc;
	if (db_.accounts.contains(*alloc.id)) throw RepositoryError("duplicate account id");
	db_.faults.check("insert:login");
	account.id = *alloc.id;
	db_.accounts[account.id] = account;
	return alloc;
}

void InMemoryAccountRepository::save(const Account& account) {
	if (!db_.accounts.contains(account.id)) throw RepositoryError("save of unknown account");
	db_.faults.check("update:login");
	db_.accounts[account.id] = account;
}

bool InMemoryAccountRepository::remove(AccountId id) {
	if (!db_.accounts.contains(id)) return false;
	return transactional(db_, [&] {
		db_.faults.check("delete:login");
		db_.accounts.erase(id);
		db_.faults.check("delete:global_acc_reg_num");
		std::erase_if(db_.global_reg_num, [&](const auto& kv) { return std::get<0>(kv.first) == id; });
		db_.faults.check("delete:global_acc_reg_str");
		std::erase_if(db_.global_reg_str, [&](const auto& kv) { return std::get<0>(kv.first) == id; });
	});
}

void InMemoryAccountRepository::set_global_reg(AccountId id, const GlobalRegNum& reg) {
	db_.global_reg_num[{id, reg.key, reg.index}] = reg.value;
}

void InMemoryAccountRepository::set_global_reg(AccountId id, const GlobalRegStr& reg) {
	db_.global_reg_str[{id, reg.key, reg.index}] = reg.value;
}

std::vector<GlobalRegNum> InMemoryAccountRepository::global_regs_num(AccountId id) const {
	std::vector<GlobalRegNum> out;
	for (const auto& [k, v] : db_.global_reg_num)
		if (std::get<0>(k) == id) out.push_back({std::get<1>(k), std::get<2>(k), v});
	return out;
}

std::vector<GlobalRegStr> InMemoryAccountRepository::global_regs_str(AccountId id) const {
	std::vector<GlobalRegStr> out;
	for (const auto& [k, v] : db_.global_reg_str)
		if (std::get<0>(k) == id) out.push_back({std::get<1>(k), std::get<2>(k), v});
	return out;
}

// ------------------------------------------------------------------ characters

std::optional<Character> InMemoryCharacterRepository::find(CharId id) const {
	if (const auto it = db_.characters.find(id); it != db_.characters.end()) return it->second;
	return std::nullopt;
}

std::vector<Character> InMemoryCharacterRepository::list_by_account(AccountId account) const {
	std::vector<Character> out;
	for (const auto& [id, c] : db_.characters)
		if (c.account == account) out.push_back(c);
	return out;
}

std::optional<Character> InMemoryCharacterRepository::find_by_name(std::string_view name) const {
	for (const auto& [id, c] : db_.characters)
		if (c.name == name) return c;
	return std::nullopt;
}

CharId InMemoryCharacterRepository::create(Character character) {
	if (find_by_name(character.name)) throw RepositoryError("duplicate character name");
	check_items(character.inventory, ItemTable::Inventory);
	check_items(character.cart, ItemTable::Cart);
	character.id = CharId{db_.next_char_id++};
	db_.characters[character.id] = character;
	return character.id;
}

void InMemoryCharacterRepository::save(const Character& character) {
	if (!db_.characters.contains(character.id)) throw RepositoryError("save of unknown character");
	check_items(character.inventory, ItemTable::Inventory);
	check_items(character.cart, ItemTable::Cart);
	const bool ok = transactional(db_, [&] {
		db_.faults.check("update:char");
		db_.characters[character.id] = character;
	});
	if (!ok) throw RepositoryError("character save failed");
}

bool InMemoryCharacterRepository::remove(CharId) {
	throw NotImplemented("CharacterRepository::remove (BR-CHAR-013 cleanup plan, char refactor phase 3)");
}

// ------------------------------------------------------------------ parties

std::optional<Party> InMemoryPartyRepository::find(PartyId id) const {
	if (const auto it = db_.parties.find(id); it != db_.parties.end()) return it->second;
	return std::nullopt;
}

PartyId InMemoryPartyRepository::create(Party party) {
	for (const auto& [id, p] : db_.parties)
		if (p.name == party.name) throw RepositoryError("duplicate party name");
	party.id = PartyId{db_.next_party_id++};
	db_.parties[party.id] = party;
	return party.id;
}

void InMemoryPartyRepository::save(const Party& party) {
	if (!db_.parties.contains(party.id)) throw RepositoryError("save of unknown party");
	db_.parties[party.id] = party;
}

bool InMemoryPartyRepository::remove(PartyId id) {
	if (!db_.parties.contains(id)) return false;
	return transactional(db_, [&] {
		db_.parties.erase(id);
		db_.faults.check("update:char.party_id");
		for (auto& [cid, c] : db_.characters)
			if (c.party == id) c.party.reset();
	});
}

// ------------------------------------------------------------------ guilds

std::optional<Guild> InMemoryGuildRepository::find(GuildId id) const {
	if (const auto it = db_.guilds.find(id); it != db_.guilds.end()) return it->second;
	return std::nullopt;
}

GuildId InMemoryGuildRepository::create(Guild guild) {
	for (const auto& [id, g] : db_.guilds)
		if (g.name == guild.name) throw RepositoryError("duplicate guild name");
	check_items(guild.storage, ItemTable::GuildStorage);
	guild.id = GuildId{db_.next_guild_id++};
	db_.guilds[guild.id] = guild;
	return guild.id;
}

void InMemoryGuildRepository::save(const Guild& guild) {
	if (!db_.guilds.contains(guild.id)) throw RepositoryError("save of unknown guild");
	check_items(guild.storage, ItemTable::GuildStorage);
	db_.guilds[guild.id] = guild;
}

bool InMemoryGuildRepository::remove(GuildId id) {
	if (!db_.guilds.contains(id)) return false;
	return transactional(db_, [&] {
		db_.guilds.erase(id);
		db_.faults.check("update:char.guild_id");
		for (auto& [cid, c] : db_.characters)
			if (c.guild == id) c.guild.reset();
	});
}

// ------------------------------------------------------------------ mail

std::optional<Mail> InMemoryMailRepository::find(MailId id) const {
	if (const auto it = db_.mails.find(id); it != db_.mails.end()) return it->second;
	return std::nullopt;
}

std::vector<Mail> InMemoryMailRepository::inbox(CharId dest, std::size_t limit) const {
	std::vector<Mail> out;
	for (auto it = db_.mails.rbegin(); it != db_.mails.rend() && out.size() < limit; ++it)
		if (it->second.dest == dest) out.push_back(it->second);
	return out;
}

MailId InMemoryMailRepository::create(Mail mail) {
	if (mail.attachments.size() > kMailMaxItems) throw RepositoryError("too many mail attachments");
	for (const auto& item : mail.attachments) {
		try {
			(void)to_row(item, ItemTable::MailAttachments);
		} catch (const MappingError& e) {
			throw RepositoryError(e.what());
		}
	}
	mail.id = MailId{db_.next_mail_id++};
	db_.mails[mail.id] = mail;
	return mail.id;
}

void InMemoryMailRepository::save(const Mail& mail) {
	if (!db_.mails.contains(mail.id)) throw RepositoryError("save of unknown mail");
	if (mail.attachments.size() > kMailMaxItems) throw RepositoryError("too many mail attachments");
	db_.mails[mail.id] = mail;
}

bool InMemoryMailRepository::remove(MailId id) { return db_.mails.erase(id) > 0; }

} // namespace rathena::persistence
