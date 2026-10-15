#include "rathena/persistence/mysql.hpp"

#include "rathena/persistence/errors.hpp"

namespace rathena::persistence {

namespace {

constexpr const char* kAccountColumns =
    "`account_id`, `userid`, `pass_hash`, `pass_scheme`, `sex`, `email`, `group_id`, `state`, `unban_time`, "
    "`expiration_time`, `logincount`, `character_slots`, `vip_time`, `old_group`";

std::string_view pass_scheme_name(PassScheme s) {
	switch (s) {
	case PassScheme::Plain: return "plain";
	case PassScheme::Md5: return "md5";
	case PassScheme::Argon2id: return "argon2id";
	}
	return "plain";
}

PassScheme pass_scheme_from(std::string_view s) {
	if (s == "argon2id") return PassScheme::Argon2id;
	if (s == "md5") return PassScheme::Md5;
	if (s == "plain") return PassScheme::Plain;
	throw RepositoryError("unknown pass_scheme '" + std::string(s) + "'");
}

/// Legacy servers still read `user_pass` during the strangler phase (architecture §5 M6): it is
/// kept in step for plain/md5 accounts and blanked once the account holds an Argon2id hash.
std::string legacy_user_pass(const Account& a) {
	return a.pass_scheme == PassScheme::Argon2id ? std::string{} : a.pass_hash;
}

std::vector<SqlValue> account_values(const Account& a) {
	return {
	    a.userid,
	    legacy_user_pass(a),
	    a.pass_hash,
	    std::string(pass_scheme_name(a.pass_scheme)),
	    std::string(1, static_cast<char>(a.sex)),
	    a.email,
	    std::int64_t{a.group_id},
	    std::uint64_t{a.state},
	    a.unban_time,
	    a.expiration_time,
	    std::uint64_t{a.logincount},
	    std::uint64_t{a.character_slots},
	    a.vip_time,
	    std::int64_t{a.old_group},
	};
}

template <class T>
T as_int(const Row& r, std::string_view col) {
	const auto it = r.find(col);
	if (it == r.end()) throw RepositoryError("missing column " + std::string(col));
	if (const auto* u = std::get_if<std::uint64_t>(&it->second)) return static_cast<T>(*u);
	if (const auto* i = std::get_if<std::int64_t>(&it->second)) return static_cast<T>(*i);
	if (std::holds_alternative<std::monostate>(it->second)) return T{};
	throw RepositoryError("column " + std::string(col) + " is not an integer");
}

std::string as_text(const Row& r, std::string_view col) {
	const auto it = r.find(col);
	if (it == r.end()) throw RepositoryError("missing column " + std::string(col));
	if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
	if (std::holds_alternative<std::monostate>(it->second)) return {};
	throw RepositoryError("column " + std::string(col) + " is not text");
}

Account account_from_row(const Row& r) {
	Account a;
	a.id = AccountId{as_int<std::uint32_t>(r, "account_id")};
	a.userid = as_text(r, "userid");
	a.pass_hash = as_text(r, "pass_hash");
	a.pass_scheme = pass_scheme_from(as_text(r, "pass_scheme"));
	const auto sex = as_text(r, "sex");
	a.sex = sex == "F" ? Sex::Female : sex == "S" ? Sex::Server : Sex::Male;
	a.email = as_text(r, "email");
	a.group_id = as_int<std::int32_t>(r, "group_id");
	a.state = as_int<std::uint32_t>(r, "state");
	a.unban_time = as_int<std::int64_t>(r, "unban_time");
	a.expiration_time = as_int<std::int64_t>(r, "expiration_time");
	a.logincount = as_int<std::uint32_t>(r, "logincount");
	a.character_slots = as_int<std::uint8_t>(r, "character_slots");
	a.vip_time = as_int<std::int64_t>(r, "vip_time");
	a.old_group = as_int<std::int32_t>(r, "old_group");
	return a;
}

} // namespace

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


Statement select_account_by_id(const TableNames& t, AccountId id) {
	return {std::string("SELECT ") + kAccountColumns + " FROM " + quote_identifier(t.login) + " WHERE `account_id` = ?",
	        {std::uint64_t{id.value}}};
}

Statement select_account_by_userid(const TableNames& t, std::string_view userid, bool case_sensitive) {
	return {std::string("SELECT ") + kAccountColumns + " FROM " + quote_identifier(t.login) + " WHERE " +
	            (case_sensitive ? "BINARY `userid` = ?" : "`userid` = ?") + " LIMIT 2",
	        {std::string(userid)}};
}

Statement insert_account(const TableNames& t, const Account& a) {
	auto params = account_values(a);
	params.insert(params.begin(), std::uint64_t{a.id.value});
	return {"INSERT INTO " + quote_identifier(t.login) +
	            " (`account_id`, `userid`, `user_pass`, `pass_hash`, `pass_scheme`, `sex`, `email`, `group_id`, `state`,"
	            " `unban_time`, `expiration_time`, `logincount`, `character_slots`, `vip_time`, `old_group`)"
	            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
	        std::move(params)};
}

Statement update_account(const TableNames& t, const Account& a) {
	auto params = account_values(a);
	params.emplace_back(std::uint64_t{a.id.value});
	return {"UPDATE " + quote_identifier(t.login) +
	            " SET `userid` = ?, `user_pass` = ?, `pass_hash` = ?, `pass_scheme` = ?, `sex` = ?, `email` = ?,"
	            " `group_id` = ?, `state` = ?, `unban_time` = ?, `expiration_time` = ?, `logincount` = ?,"
	            " `character_slots` = ?, `vip_time` = ?, `old_group` = ? WHERE `account_id` = ?",
	        std::move(params)};
}

} // namespace sql

// ------------------------------------------------------------------ accounts
std::optional<Account> MysqlAccountRepository::find(AccountId id) const {
	const auto rows = conn_.query(sql::select_account_by_id(names_, id));
	if (rows.empty()) return std::nullopt;
	return account_from_row(rows.front());
}

std::optional<Account> MysqlAccountRepository::find_by_userid(std::string_view userid, bool case_sensitive) const {
	const auto rows = conn_.query(sql::select_account_by_userid(names_, userid, case_sensitive));
	if (rows.size() != 1) return std::nullopt; // none, or BR-LOGIN-038 ambiguity -> deny
	return account_from_row(rows.front());
}

AccountIdAllocation MysqlAccountRepository::create(Account& account, std::optional<std::uint32_t> requested) {
	std::optional<std::uint32_t> current_max;
	if (!requested) {
		const auto rows = conn_.query(sql::select_max_account_id(names_));
		if (!rows.empty() && !rows.front().empty()) {
			const auto& v = rows.front().begin()->second;
			if (const auto* u = std::get_if<std::uint64_t>(&v)) current_max = static_cast<std::uint32_t>(*u);
			else if (const auto* i = std::get_if<std::int64_t>(&v)) current_max = static_cast<std::uint32_t>(*i);
		}
	}
	const auto alloc = allocate_account_id(requested, current_max);
	if (!alloc.ok()) return alloc;
	Account row = account;
	row.id = *alloc.id;
	conn_.execute(sql::insert_account(names_, row)); // duplicate id -> RepositoryError
	account.id = row.id;
	return alloc;
}

void MysqlAccountRepository::save(const Account& account) {
	conn_.execute(sql::update_account(names_, account));
	if (conn_.affected_rows() == 0 && !find(account.id)) throw RepositoryError("save of unknown account");
}

bool MysqlAccountRepository::remove(AccountId id) {
	if (!find(id)) return false;
	return conn_.execute_in_transaction(sql::remove_account(names_, id));
}

void MysqlAccountRepository::set_global_reg(AccountId id, const GlobalRegNum& reg) {
	conn_.execute({"REPLACE INTO " + quote_identifier(names_.global_acc_reg_num) +
	                   " (`account_id`, `key`, `index`, `value`) VALUES (?, ?, ?, ?)",
	               {std::uint64_t{id.value}, reg.key, std::uint64_t{reg.index}, reg.value}});
}

void MysqlAccountRepository::set_global_reg(AccountId id, const GlobalRegStr& reg) {
	conn_.execute({"REPLACE INTO " + quote_identifier(names_.global_acc_reg_str) +
	                   " (`account_id`, `key`, `index`, `value`) VALUES (?, ?, ?, ?)",
	               {std::uint64_t{id.value}, reg.key, std::uint64_t{reg.index}, reg.value}});
}

std::vector<GlobalRegNum> MysqlAccountRepository::global_regs_num(AccountId id) const {
	std::vector<GlobalRegNum> out;
	for (const auto& r : conn_.query({"SELECT `key`, `index`, `value` FROM " + quote_identifier(names_.global_acc_reg_num) +
	                                      " WHERE `account_id` = ? ORDER BY `key`, `index`",
	                                  {std::uint64_t{id.value}}}))
		out.push_back({as_text(r, "key"), as_int<std::uint32_t>(r, "index"), as_int<std::int64_t>(r, "value")});
	return out;
}

std::vector<GlobalRegStr> MysqlAccountRepository::global_regs_str(AccountId id) const {
	std::vector<GlobalRegStr> out;
	for (const auto& r : conn_.query({"SELECT `key`, `index`, `value` FROM " + quote_identifier(names_.global_acc_reg_str) +
	                                      " WHERE `account_id` = ? ORDER BY `key`, `index`",
	                                  {std::uint64_t{id.value}}}))
		out.push_back({as_text(r, "key"), as_int<std::uint32_t>(r, "index"), as_text(r, "value")});
	return out;
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
