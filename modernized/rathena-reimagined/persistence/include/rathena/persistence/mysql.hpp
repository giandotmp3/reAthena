#pragma once
// Concrete MySQL/MariaDB repositories. In this scaffold the connection and query execution are
// stubs (NotImplemented); the parameterized statement builders are real and unit-tested, so the
// SQL each operation will issue is pinned before the client library is wired in.
#include <memory>
#include <vector>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/repositories.hpp"
#include "rathena/persistence/row.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence {

/// One connection to ${DATABASE_URL} (or ${LOG_DB_URL}). Not implemented in the scaffold.
class MysqlConnection {
public:
	explicit MysqlConnection(DbEndpoint endpoint) : endpoint_(std::move(endpoint)) {}
	void connect();                                        ///< throws NotImplemented
	void execute(const Statement& st);                     ///< throws NotImplemented
	std::vector<Row> query(const Statement& st);           ///< throws NotImplemented
	/// Runs all statements in one START TRANSACTION ... COMMIT; ROLLBACK on any failure.
	bool execute_in_transaction(const std::vector<Statement>& statements); ///< NotImplemented
	[[nodiscard]] const DbEndpoint& endpoint() const noexcept { return endpoint_; }

private:
	DbEndpoint endpoint_;
};

namespace sql {
/// BR-LOGIN-008: `SELECT MAX(account_id) FROM login` (+1 and range checks happen in C++).
Statement select_max_account_id(const TableNames& t);
/// BR-LOGIN-037: the statements run inside ONE transaction, in this order.
std::vector<Statement> remove_account(const TableNames& t, AccountId id);
} // namespace sql

class MysqlAccountRepository final : public AccountRepository {
public:
	MysqlAccountRepository(MysqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
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
	MysqlConnection& conn_;
	TableNames names_;
};

class MysqlCharacterRepository final : public CharacterRepository {
public:
	MysqlCharacterRepository(MysqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::optional<Character> find(CharId id) const override;
	std::vector<Character> list_by_account(AccountId account) const override;
	std::optional<Character> find_by_name(std::string_view name) const override;
	CharId create(Character character) override;
	void save(const Character& character) override;
	bool remove(CharId id) override;

private:
	[[maybe_unused]] MysqlConnection& conn_; // used once the MySQL client is wired in
	TableNames names_;
};

class MysqlPartyRepository final : public PartyRepository {
public:
	MysqlPartyRepository(MysqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::optional<Party> find(PartyId id) const override;
	PartyId create(Party party) override;
	void save(const Party& party) override;
	bool remove(PartyId id) override;

private:
	[[maybe_unused]] MysqlConnection& conn_; // used once the MySQL client is wired in
	TableNames names_;
};

class MysqlGuildRepository final : public GuildRepository {
public:
	MysqlGuildRepository(MysqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::optional<Guild> find(GuildId id) const override;
	GuildId create(Guild guild) override;
	void save(const Guild& guild) override;
	bool remove(GuildId id) override;

private:
	[[maybe_unused]] MysqlConnection& conn_; // used once the MySQL client is wired in
	TableNames names_;
};

class MysqlMailRepository final : public MailRepository {
public:
	MysqlMailRepository(MysqlConnection& conn, TableNames names) : conn_(conn), names_(std::move(names)) {}
	std::optional<Mail> find(MailId id) const override;
	std::vector<Mail> inbox(CharId dest, std::size_t limit) const override;
	MailId create(Mail mail) override;
	void save(const Mail& mail) override;
	bool remove(MailId id) override;

private:
	[[maybe_unused]] MysqlConnection& conn_; // used once the MySQL client is wired in
	TableNames names_;
};

} // namespace rathena::persistence
