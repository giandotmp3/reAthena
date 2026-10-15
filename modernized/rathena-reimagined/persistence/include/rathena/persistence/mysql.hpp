#pragma once
// Concrete MySQL/MariaDB repositories. The connection uses the MySQL C client API (server-side
// prepared statements, so parameters are never spliced into SQL text) when the build finds a
// client library (RATHENA_PERSISTENCE_HAS_MYSQL_CLIENT); otherwise it throws NotImplemented.
// The parameterized statement builders are real and unit-tested either way.
#include <memory>
#include <vector>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/repositories.hpp"
#include "rathena/persistence/row.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence {

/// True when this build links a MySQL/MariaDB client library.
bool mysql_client_available() noexcept;

/// One connection to ${DATABASE_URL} (or ${LOG_DB_URL}).
/// Result values: integer columns come back as int64/uint64 (by the column's UNSIGNED flag),
/// NULL as monostate, everything else as string.
class MysqlConnection {
public:
	explicit MysqlConnection(DbEndpoint endpoint);
	~MysqlConnection();
	MysqlConnection(const MysqlConnection&) = delete;
	MysqlConnection& operator=(const MysqlConnection&) = delete;

	void connect();                                        ///< throws RepositoryError (or NotImplemented without a client)
	void execute(const Statement& st);                     ///< throws RepositoryError on failure
	std::vector<Row> query(const Statement& st);           ///< throws RepositoryError on failure
	/// Rows changed by the last execute().
	[[nodiscard]] std::uint64_t affected_rows() const noexcept { return affected_rows_; }
	/// Runs all statements in one START TRANSACTION ... COMMIT; ROLLBACK on any failure.
	/// Atomic only on transactional engines: the legacy MyISAM tables need migration M2 first.
	bool execute_in_transaction(const std::vector<Statement>& statements);
	[[nodiscard]] const DbEndpoint& endpoint() const noexcept { return endpoint_; }

private:
	struct Impl;
	DbEndpoint endpoint_;
	std::unique_ptr<Impl> impl_;
	std::uint64_t affected_rows_ = 0;
};

namespace sql {
/// BR-LOGIN-008: `SELECT MAX(account_id) FROM login` (+1 and range checks happen in C++).
Statement select_max_account_id(const TableNames& t);
/// Columns read into persistence::Account (login + M6 pass_hash/pass_scheme).
Statement select_account_by_id(const TableNames& t, AccountId id);
/// BR-LOGIN-038: LIMIT 2 so a duplicate is detected without scanning further.
Statement select_account_by_userid(const TableNames& t, std::string_view userid, bool case_sensitive);
Statement insert_account(const TableNames& t, const Account& a);
Statement update_account(const TableNames& t, const Account& a);
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
