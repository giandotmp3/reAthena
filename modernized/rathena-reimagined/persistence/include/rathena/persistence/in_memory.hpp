#pragma once
// In-memory fakes for tests (no live DB). Write operations are transactional: a write either
// fully applies or leaves the database untouched. FaultInjector lets tests fail a named
// table operation mid-transaction to prove atomicity (BR-LOGIN-037).
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "rathena/persistence/repositories.hpp"

namespace rathena::persistence {

/// Fails the next operation whose label matches (e.g. "delete:global_acc_reg_str").
class FaultInjector {
public:
	void fail_on(std::string label) { armed_.insert(std::move(label)); }
	void clear() { armed_.clear(); }
	/// Throws RepositoryError if `label` is armed (one-shot).
	void check(const std::string& label);

private:
	std::set<std::string> armed_;
};

struct InMemoryDatabase {
	std::map<AccountId, Account> accounts;
	std::map<std::tuple<AccountId, std::string, std::uint32_t>, std::int64_t> global_reg_num;
	std::map<std::tuple<AccountId, std::string, std::uint32_t>, std::string> global_reg_str;
	std::map<CharId, Character> characters;
	std::map<PartyId, Party> parties;
	std::map<GuildId, Guild> guilds;
	std::map<MailId, Mail> mails;
	std::uint32_t next_char_id = kStartCharNum;
	std::uint32_t next_party_id = 1;
	std::uint32_t next_guild_id = 1;
	std::uint64_t next_mail_id = 1;
	FaultInjector faults;
};

class InMemoryAccountRepository final : public AccountRepository {
public:
	explicit InMemoryAccountRepository(InMemoryDatabase& db) : db_(db) {}
	using AccountRepository::create;
	std::optional<Account> find(AccountId id) const override;
	std::optional<Account> find_by_userid(std::string_view userid, bool case_sensitive) const override;
	AccountIdAllocation create(Account& account, std::optional<std::uint32_t> requested) override;
	void save(const Account& account) override;
	bool remove(AccountId id) override;
	void set_global_reg(AccountId id, const GlobalRegNum& reg) override;
	void set_global_reg(AccountId id, const GlobalRegStr& reg) override;
	std::vector<GlobalRegNum> global_regs_num(AccountId id) const override;
	std::vector<GlobalRegStr> global_regs_str(AccountId id) const override;

private:
	InMemoryDatabase& db_;
};

class InMemoryCharacterRepository final : public CharacterRepository {
public:
	explicit InMemoryCharacterRepository(InMemoryDatabase& db) : db_(db) {}
	std::optional<Character> find(CharId id) const override;
	std::vector<Character> list_by_account(AccountId account) const override;
	std::optional<Character> find_by_name(std::string_view name) const override;
	CharId create(Character character) override;
	void save(const Character& character) override;
	/// PENDING BR-CHAR-013: throws NotImplemented until the char-server refactor (phase 3).
	bool remove(CharId id) override;

private:
	InMemoryDatabase& db_;
};

class InMemoryPartyRepository final : public PartyRepository {
public:
	explicit InMemoryPartyRepository(InMemoryDatabase& db) : db_(db) {}
	std::optional<Party> find(PartyId id) const override;
	PartyId create(Party party) override;
	void save(const Party& party) override;
	bool remove(PartyId id) override;

private:
	InMemoryDatabase& db_;
};

class InMemoryGuildRepository final : public GuildRepository {
public:
	explicit InMemoryGuildRepository(InMemoryDatabase& db) : db_(db) {}
	std::optional<Guild> find(GuildId id) const override;
	GuildId create(Guild guild) override;
	void save(const Guild& guild) override;
	bool remove(GuildId id) override;

private:
	InMemoryDatabase& db_;
};

class InMemoryMailRepository final : public MailRepository {
public:
	explicit InMemoryMailRepository(InMemoryDatabase& db) : db_(db) {}
	std::optional<Mail> find(MailId id) const override;
	std::vector<Mail> inbox(CharId dest, std::size_t limit) const override;
	MailId create(Mail mail) override;
	void save(const Mail& mail) override;
	bool remove(MailId id) override;

private:
	InMemoryDatabase& db_;
};

} // namespace rathena::persistence
