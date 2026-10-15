#include "rathena/persistence/mysql.hpp"

#include "rathena/persistence/errors.hpp"

namespace rathena::persistence {

void MysqlConnection::connect() { throw NotImplemented("MysqlConnection::connect (" + endpoint_.redacted() + ")"); }
void MysqlConnection::execute(const Statement&) { throw NotImplemented("MysqlConnection::execute"); }
std::vector<Row> MysqlConnection::query(const Statement&) { throw NotImplemented("MysqlConnection::query"); }
bool MysqlConnection::execute_in_transaction(const std::vector<Statement>&) {
	throw NotImplemented("MysqlConnection::execute_in_transaction");
}

namespace sql {

Statement select_max_account_id(const TableNames& t) {
	return {"SELECT MAX(" + quote_identifier("account_id") + ") FROM " + quote_identifier(t.login), {}};
}

std::vector<Statement> remove_account(const TableNames& t, AccountId id) {
	const SqlValue p{std::uint64_t{id.value}};
	const std::string where = " WHERE " + quote_identifier("account_id") + " = ?";
	return {
	    {"DELETE FROM " + quote_identifier(t.login) + where, {p}},
	    {"DELETE FROM " + quote_identifier(t.global_acc_reg_num) + where, {p}},
	    {"DELETE FROM " + quote_identifier(t.global_acc_reg_str) + where, {p}},
	};
}

} // namespace sql

// ------------------------------------------------------------------ accounts
std::optional<Account> MysqlAccountRepository::find(AccountId) const { throw NotImplemented("MysqlAccountRepository::find"); }
std::optional<Account> MysqlAccountRepository::find_by_userid(std::string_view, bool) const {
	throw NotImplemented("MysqlAccountRepository::find_by_userid");
}
AccountIdAllocation MysqlAccountRepository::create(Account&, std::optional<std::uint32_t>) {
	throw NotImplemented("MysqlAccountRepository::create");
}
void MysqlAccountRepository::save(const Account&) { throw NotImplemented("MysqlAccountRepository::save"); }
bool MysqlAccountRepository::remove(AccountId id) {
	// BR-LOGIN-037: one transaction; ROLLBACK on any failure.
	return conn_.execute_in_transaction(sql::remove_account(names_, id));
}
void MysqlAccountRepository::set_global_reg(AccountId, const GlobalRegNum&) {
	throw NotImplemented("MysqlAccountRepository::set_global_reg");
}
void MysqlAccountRepository::set_global_reg(AccountId, const GlobalRegStr&) {
	throw NotImplemented("MysqlAccountRepository::set_global_reg");
}
std::vector<GlobalRegNum> MysqlAccountRepository::global_regs_num(AccountId) const {
	throw NotImplemented("MysqlAccountRepository::global_regs_num");
}
std::vector<GlobalRegStr> MysqlAccountRepository::global_regs_str(AccountId) const {
	throw NotImplemented("MysqlAccountRepository::global_regs_str");
}

// ------------------------------------------------------------------ characters
std::optional<Character> MysqlCharacterRepository::find(CharId) const { throw NotImplemented("MysqlCharacterRepository::find"); }
std::vector<Character> MysqlCharacterRepository::list_by_account(AccountId) const {
	throw NotImplemented("MysqlCharacterRepository::list_by_account");
}
std::optional<Character> MysqlCharacterRepository::find_by_name(std::string_view) const {
	throw NotImplemented("MysqlCharacterRepository::find_by_name");
}
CharId MysqlCharacterRepository::create(Character) { throw NotImplemented("MysqlCharacterRepository::create"); }
void MysqlCharacterRepository::save(const Character&) { throw NotImplemented("MysqlCharacterRepository::save"); }
bool MysqlCharacterRepository::remove(CharId) { throw NotImplemented("MysqlCharacterRepository::remove (BR-CHAR-013)"); }

// ------------------------------------------------------------------ parties
std::optional<Party> MysqlPartyRepository::find(PartyId) const { throw NotImplemented("MysqlPartyRepository::find"); }
PartyId MysqlPartyRepository::create(Party) { throw NotImplemented("MysqlPartyRepository::create"); }
void MysqlPartyRepository::save(const Party&) { throw NotImplemented("MysqlPartyRepository::save"); }
bool MysqlPartyRepository::remove(PartyId) { throw NotImplemented("MysqlPartyRepository::remove"); }

// ------------------------------------------------------------------ guilds
std::optional<Guild> MysqlGuildRepository::find(GuildId) const { throw NotImplemented("MysqlGuildRepository::find"); }
GuildId MysqlGuildRepository::create(Guild) { throw NotImplemented("MysqlGuildRepository::create"); }
void MysqlGuildRepository::save(const Guild&) { throw NotImplemented("MysqlGuildRepository::save"); }
bool MysqlGuildRepository::remove(GuildId) { throw NotImplemented("MysqlGuildRepository::remove"); }

// ------------------------------------------------------------------ mail
std::optional<Mail> MysqlMailRepository::find(MailId) const { throw NotImplemented("MysqlMailRepository::find"); }
std::vector<Mail> MysqlMailRepository::inbox(CharId, std::size_t) const { throw NotImplemented("MysqlMailRepository::inbox"); }
MailId MysqlMailRepository::create(Mail) { throw NotImplemented("MysqlMailRepository::create"); }
void MysqlMailRepository::save(const Mail&) { throw NotImplemented("MysqlMailRepository::save"); }
bool MysqlMailRepository::remove(MailId) { throw NotImplemented("MysqlMailRepository::remove"); }

} // namespace rathena::persistence
