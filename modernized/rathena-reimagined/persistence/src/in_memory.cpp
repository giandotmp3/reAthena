#include "rathena/persistence/in_memory.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "rathena/persistence/character_delete.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/foreign_keys.hpp"
#include "rathena/persistence/item_mapping.hpp"

namespace rathena::persistence {

void FaultInjector::check(const std::string& label) {
	if (armed_.erase(label) > 0) throw RepositoryError("injected fault: " + label);
}

namespace {

constexpr std::uint32_t kWeddingRingM = 2634; ///< WEDDING_RING_M (common/mmo.hpp)
constexpr std::uint32_t kWeddingRingF = 2635; ///< WEDDING_RING_F

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

/// The fault label of a write to `logical_table` (a TableNames field), by its default name.
std::string write_label(std::string_view logical_table) {
	return "write:" + std::string(resolve_table(TableNames{}, logical_table));
}

bool iequals(std::string_view a, std::string_view b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		       return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	       });
}

/// The SQL schema's name CHECK (1..NAME_LENGTH-1 bytes), kept here so the fake refuses the same.
void check_name(std::string_view name, const char* what) {
	if (name.empty() || name.size() >= kNameLength) throw RepositoryError(std::string(what) + " name must be 1..23 bytes");
}

/// Every stored item must be representable by the ONE mapping for its table (D6 in code).
void check_item(const Item& item, ItemTable t) {
	try {
		(void)to_row(item, t);
	} catch (const MappingError& e) {
		throw RepositoryError(e.what());
	}
}

void check_items(const std::vector<StoredItem>& items, ItemTable t) {
	for (const auto& s : items) check_item(s.item, t);
}

/// Gives new rows (row id 0) a fresh id and orders by row id, as the SQL tables return them.
void assign_rows(InMemoryDatabase& db, std::vector<StoredItem>& items) {
	for (auto& s : items)
		if (s.row_id == 0) s.row_id = db.next_item_row_id++;
	std::sort(items.begin(), items.end(), [](const StoredItem& a, const StoredItem& b) { return a.row_id < b.row_id; });
}

/// The guild table keys and CHECKs: positions 0..19 unique, tax 0..100, one row per member.
void check_guild(const Guild& g) {
	check_name(g.name, "guild");
	std::set<std::uint32_t> positions;
	for (const auto& p : g.positions) {
		if (p.index >= kMaxGuildPositions || !positions.insert(p.index).second || p.exp_mode > 100 ||
		    p.name.size() >= kNameLength)
			throw RepositoryError("invalid guild position");
	}
	std::set<CharId> members;
	for (const auto& m : g.members)
		if (m.position >= kMaxGuildPositions || !members.insert(m.char_id).second) throw RepositoryError("invalid guild member");
	check_items(g.storage, ItemTable::GuildStorage);
}

/// Members by char id, positions by index, storage by row id (the SQL read order).
void normalize(InMemoryDatabase& db, Guild& g) {
	std::sort(g.members.begin(), g.members.end(), [](const auto& a, const auto& b) { return a.char_id < b.char_id; });
	std::sort(g.positions.begin(), g.positions.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
	assign_rows(db, g.storage);
}

void check_mail(const Mail& mail) {
	if (mail.attachments.size() > kMailMaxItems) throw RepositoryError("too many mail attachments");
	for (const auto& item : mail.attachments) check_item(item, ItemTable::MailAttachments);
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
	check_name(character.name, "character");
	if (find_by_name(character.name)) throw RepositoryError("duplicate character name");
	check_items(character.inventory, ItemTable::Inventory);
	check_items(character.cart, ItemTable::Cart);
	const bool ok = transactional(db_, [&] {
		db_.faults.check("write:char");
		character.id = CharId{db_.next_char_id++};
		db_.faults.check("write:inventory");
		assign_rows(db_, character.inventory);
		db_.faults.check("write:cart_inventory");
		assign_rows(db_, character.cart);
		db_.characters[character.id] = character;
	});
	if (!ok) throw RepositoryError("character create failed");
	return character.id;
}

void InMemoryCharacterRepository::save(const Character& character) {
	if (!db_.characters.contains(character.id)) throw RepositoryError("save of unknown character");
	check_name(character.name, "character");
	if (const auto other = find_by_name(character.name); other && other->id != character.id)
		throw RepositoryError("duplicate character name");
	check_items(character.inventory, ItemTable::Inventory);
	check_items(character.cart, ItemTable::Cart);
	Character stored = character;
	const bool ok = transactional(db_, [&] {
		db_.faults.check("write:char");
		db_.faults.check("write:inventory");
		assign_rows(db_, stored.inventory);
		db_.faults.check("write:cart_inventory");
		assign_rows(db_, stored.cart);
		db_.characters[stored.id] = stored;
	});
	if (!ok) throw RepositoryError("character save failed");
}

bool InMemoryCharacterRepository::remove(CharId id) {
	if (!db_.characters.contains(id)) return false;
	return transactional(db_, [&] {
		const Character c = db_.characters.at(id);
		for (const auto& step : character_delete_plan()) {
			db_.faults.check(write_label(step.logical_table));
			switch (step.step) {
			case CleanupStep::Divorce:
				if (c.partner) {
					if (auto it = db_.characters.find(*c.partner); it != db_.characters.end()) {
						it->second.partner.reset();
						auto& inv = it->second.inventory; // the partner's ring goes too (one ring)
						const auto ring = std::find_if(inv.begin(), inv.end(), [](const StoredItem& s) {
							return s.item.nameid == kWeddingRingM || s.item.nameid == kWeddingRingF;
						});
						if (ring != inv.end()) inv.erase(ring);
					}
				}
				break;
			case CleanupStep::LeaveParty:
				if (c.party) {
					const auto party = db_.parties.find(*c.party);
					const bool leader = party != db_.parties.end() && party->second.leader_char == id;
					db_.characters.at(id).party.reset();
					const bool empty = std::none_of(db_.characters.begin(), db_.characters.end(),
					                                [&](const auto& kv) { return kv.second.party == c.party; });
					if (leader || empty) { // BR-CHAR-035 / BR-CHAR-036: the party is dissolved
						for (auto& [cid, other] : db_.characters)
							if (other.party == c.party) other.party.reset();
						db_.parties.erase(*c.party);
					}
				}
				break;
			case CleanupStep::DeleteReceivedMail:
				std::erase_if(db_.mails, [&](const auto& kv) { return kv.second.dest == id; });
				break;
			case CleanupStep::AnonymizeSentMail:
				for (auto& [mid, m] : db_.mails)
					if (m.sender == id) m.sender.reset();
				break;
			case CleanupStep::GuildBreakOrLeave:
				for (auto it = db_.guilds.begin(); it != db_.guilds.end();) {
					if (it->second.master == id) { // break (BR-CHAR-042)
						for (auto& [cid, other] : db_.characters)
							if (other.guild == it->first) other.guild.reset();
						it = db_.guilds.erase(it);
					} else { // leave
						std::erase_if(it->second.members, [&](const GuildMember& m) { return m.char_id == id; });
						++it;
					}
				}
				break;
			case CleanupStep::DeleteCharacterRow:
				db_.characters.erase(id);
				for (auto& [cid, other] : db_.characters) { // the schema's ON DELETE SET NULL
					if (other.partner == id) other.partner.reset();
					if (other.father == id) other.father.reset();
					if (other.mother == id) other.mother.reset();
				}
				break;
			default:
				// De-adopt (the parents' `child` column), companions, friends, skills, registries,
				// quests, ...: tables the fake does not hold. Inventory and cart go with the row.
				break;
			}
		}
	});
}

// ------------------------------------------------------------------ parties

std::optional<Party> InMemoryPartyRepository::find(PartyId id) const {
	if (const auto it = db_.parties.find(id); it != db_.parties.end()) return it->second;
	return std::nullopt;
}

PartyId InMemoryPartyRepository::create(Party party) {
	check_name(party.name, "party");
	for (const auto& [id, p] : db_.parties)
		if (p.name == party.name) throw RepositoryError("duplicate party name");
	db_.faults.check("write:party");
	party.id = PartyId{db_.next_party_id++};
	db_.parties[party.id] = party;
	return party.id;
}

void InMemoryPartyRepository::save(const Party& party) {
	if (!db_.parties.contains(party.id)) throw RepositoryError("save of unknown party");
	check_name(party.name, "party");
	for (const auto& [id, p] : db_.parties)
		if (id != party.id && p.name == party.name) throw RepositoryError("duplicate party name");
	db_.faults.check("write:party");
	db_.parties[party.id] = party;
}

bool InMemoryPartyRepository::remove(PartyId id) {
	if (!db_.parties.contains(id)) return false;
	return transactional(db_, [&] {
		db_.faults.check("write:party");
		db_.parties.erase(id);
		db_.faults.check("write:char");
		for (auto& [cid, c] : db_.characters)
			if (c.party == id) c.party.reset();
	});
}

std::vector<CharId> InMemoryPartyRepository::members(PartyId id) const {
	std::vector<CharId> out;
	for (const auto& [cid, c] : db_.characters)
		if (c.party == id) out.push_back(cid);
	return out;
}

// ------------------------------------------------------------------ guilds

std::optional<Guild> InMemoryGuildRepository::find(GuildId id) const {
	if (const auto it = db_.guilds.find(id); it != db_.guilds.end()) return it->second;
	return std::nullopt;
}

GuildId InMemoryGuildRepository::create(Guild guild) {
	check_guild(guild);
	for (const auto& [id, g] : db_.guilds)
		if (g.name == guild.name) throw RepositoryError("duplicate guild name");
	const bool ok = transactional(db_, [&] {
		db_.faults.check("write:guild");
		guild.id = GuildId{db_.next_guild_id++};
		db_.faults.check("write:guild_member");
		db_.faults.check("write:guild_position");
		db_.faults.check("write:guild_storage");
		normalize(db_, guild);
		db_.guilds[guild.id] = guild;
	});
	if (!ok) throw RepositoryError("guild create failed");
	return guild.id;
}

void InMemoryGuildRepository::save(const Guild& guild) {
	if (!db_.guilds.contains(guild.id)) throw RepositoryError("save of unknown guild");
	check_guild(guild);
	for (const auto& [id, g] : db_.guilds)
		if (id != guild.id && g.name == guild.name) throw RepositoryError("duplicate guild name");
	Guild stored = guild;
	const bool ok = transactional(db_, [&] {
		db_.faults.check("write:guild");
		db_.faults.check("write:guild_member");
		db_.faults.check("write:guild_position");
		db_.faults.check("write:guild_storage");
		normalize(db_, stored);
		db_.guilds[stored.id] = stored;
	});
	if (!ok) throw RepositoryError("guild save failed");
}

bool InMemoryGuildRepository::remove(GuildId id) {
	if (!db_.guilds.contains(id)) return false;
	return transactional(db_, [&] {
		db_.faults.check("write:char");
		for (auto& [cid, c] : db_.characters)
			if (c.guild == id) c.guild.reset();
		db_.faults.check("write:guild_storage");
		db_.faults.check("write:guild_position");
		db_.faults.check("write:guild_member");
		db_.faults.check("write:guild");
		db_.guilds.erase(id);
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
	check_mail(mail);
	const bool ok = transactional(db_, [&] {
		db_.faults.check("write:mail");
		mail.id = MailId{db_.next_mail_id++};
		db_.faults.check("write:mail_attachments");
		db_.mails[mail.id] = mail;
	});
	if (!ok) throw RepositoryError("mail create failed");
	return mail.id;
}

void InMemoryMailRepository::save(const Mail& mail) {
	if (!db_.mails.contains(mail.id)) throw RepositoryError("save of unknown mail");
	check_mail(mail);
	const bool ok = transactional(db_, [&] {
		db_.faults.check("write:mail");
		db_.faults.check("write:mail_attachments");
		db_.mails[mail.id] = mail;
	});
	if (!ok) throw RepositoryError("mail save failed");
}

bool InMemoryMailRepository::remove(MailId id) {
	if (!db_.mails.contains(id)) return false;
	return transactional(db_, [&] {
		db_.faults.check("write:mail_attachments");
		db_.faults.check("write:mail");
		db_.mails.erase(id);
	});
}

// ------------------------------------------------------------------ account storage

std::vector<StoredItem> InMemoryStorageRepository::load(AccountId account) const {
	if (const auto it = db_.storage.find(account); it != db_.storage.end()) return it->second;
	return {};
}

void InMemoryStorageRepository::save(AccountId account, const std::vector<StoredItem>& items) {
	check_items(items, ItemTable::Storage);
	auto stored = items;
	const bool ok = transactional(db_, [&] {
		db_.faults.check("write:storage");
		assign_rows(db_, stored);
		if (stored.empty()) db_.storage.erase(account);
		else db_.storage[account] = stored;
	});
	if (!ok) throw RepositoryError("storage save failed");
}

} // namespace rathena::persistence
