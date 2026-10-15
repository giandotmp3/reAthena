#pragma once
// SQL repositories over any SqlConnection (SQLite or PostgreSQL), written in the portable SQL
// subset of DATABASE_DECISION.md: double-quoted identifiers, ON CONFLICT upserts, '?'
// placeholders (the PostgreSQL backend rewrites them to $n), integer UNIX times.
#include <memory>
#include <vector>

#include "rathena/persistence/character_delete.hpp"
#include "rathena/persistence/repositories.hpp"
#include "rathena/persistence/row.hpp"
#include "rathena/persistence/sql_connection.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence {

namespace sql {
/// BR-LOGIN-008: `SELECT MAX(account_id) FROM login` (+1 and range checks happen in C++).
Statement select_max_account_id(const TableNames& t, SqlDialect d);
/// Columns read into persistence::Account (login + pass_hash/pass_scheme).
Statement select_account_by_id(const TableNames& t, AccountId id, SqlDialect d);
/// BR-LOGIN-038: LIMIT 2 so a duplicate is detected without scanning further.
Statement select_account_by_userid(const TableNames& t, std::string_view userid, bool case_sensitive,
                                   SqlDialect d);
Statement insert_account(const TableNames& t, const Account& a, SqlDialect d);
Statement update_account(const TableNames& t, const Account& a, SqlDialect d);
/// BR-LOGIN-037: the statements run inside ONE transaction, in this order.
std::vector<Statement> remove_account(const TableNames& t, AccountId id, SqlDialect d);
} // namespace sql

/// AccountRepository over any SqlConnection, on the clean target schema (target_schema.hpp).
class SqlAccountRepository final : public AccountRepository {
public:
	SqlAccountRepository(SqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
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
	SqlConnection& conn_;
	TableNames names_;
};

// The character-side repositories (DATABASE_DECISION.md increment 3), also on the target schema.
// Generated ids come back through RETURNING inside the transaction that writes the child rows;
// item rows of every table go through the ONE Item mapping (item_mapping.hpp). "None" references
// are NULL (std::nullopt), never 0.

class SqlCharacterRepository final : public CharacterRepository {
public:
	SqlCharacterRepository(SqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::optional<Character> find(CharId id) const override;
	std::vector<Character> list_by_account(AccountId account) const override;
	std::optional<Character> find_by_name(std::string_view name) const override;
	CharId create(Character character) override;
	void save(const Character& character) override;
	bool remove(CharId id) override;

private:
	std::vector<Character> load(const Statement& select) const;
	void write_items(const Character& character);
	void cleanup(const CleanupStepInfo& step, const Character& character);
	SqlConnection& conn_;
	TableNames names_;
};

class SqlPartyRepository final : public PartyRepository {
public:
	SqlPartyRepository(SqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::optional<Party> find(PartyId id) const override;
	PartyId create(Party party) override;
	void save(const Party& party) override;
	bool remove(PartyId id) override;
	std::vector<CharId> members(PartyId id) const override;

private:
	SqlConnection& conn_;
	TableNames names_;
};

class SqlGuildRepository final : public GuildRepository {
public:
	SqlGuildRepository(SqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::optional<Guild> find(GuildId id) const override;
	GuildId create(Guild guild) override;
	void save(const Guild& guild) override;
	bool remove(GuildId id) override;

private:
	void write_children(const Guild& guild);
	SqlConnection& conn_;
	TableNames names_;
};

class SqlMailRepository final : public MailRepository {
public:
	SqlMailRepository(SqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::optional<Mail> find(MailId id) const override;
	std::vector<Mail> inbox(CharId dest, std::size_t limit) const override;
	MailId create(Mail mail) override;
	void save(const Mail& mail) override;
	bool remove(MailId id) override;

private:
	std::vector<Mail> load(const Statement& select) const;
	SqlConnection& conn_;
	TableNames names_;
};

class SqlStorageRepository final : public StorageRepository {
public:
	SqlStorageRepository(SqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::vector<StoredItem> load(AccountId account) const override;
	void save(AccountId account, const std::vector<StoredItem>& items) override;

private:
	SqlConnection& conn_;
	TableNames names_;
};

} // namespace rathena::persistence
