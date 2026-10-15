#include "rathena/persistence/sql_repositories.hpp"

#include "rathena/persistence/errors.hpp"

namespace rathena::persistence {

namespace {

constexpr std::string_view kAccountColumnNames[] = {
    "account_id", "userid", "pass_hash", "pass_scheme", "sex", "email", "group_id", "state", "unban_time",
    "expiration_time", "logincount", "character_slots", "vip_time", "old_group"};

/// Columns written on insert/update, in account_values() order (without the id).
constexpr std::string_view kWrittenColumns[] = {
    "userid", "pass_hash", "pass_scheme", "sex", "email", "group_id", "state", "unban_time",
    "expiration_time", "logincount", "character_slots", "vip_time", "old_group"};

std::string column_list(SqlDialect d) {
	std::string out;
	for (const auto c : kAccountColumnNames) out += (out.empty() ? "" : ", ") + quote_identifier(c, d);
	return out;
}

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

std::vector<SqlValue> account_values(const Account& a) {
	std::vector<SqlValue> v = {a.userid};
	v.insert(v.end(), {
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
	                  });
	return v;
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

/// `##` registry upsert: the portable INSERT ... ON CONFLICT DO UPDATE.
Statement upsert_reg(const std::string& table, SqlDialect d, std::vector<SqlValue> params) {
	auto q = [d](std::string_view n) { return quote_identifier(n, d); };
	const std::string cols = "(" + q("account_id") + ", " + q("key") + ", " + q("index") + ", " + q("value") + ")";
	return {"INSERT INTO " + q(table) + " " + cols + " VALUES (?, ?, ?, ?) ON CONFLICT (" + q("account_id") + ", " +
	            q("key") + ", " + q("index") + ") DO UPDATE SET " + q("value") + " = excluded." + q("value"),
	        std::move(params)};
}

} // namespace

namespace sql {

Statement select_max_account_id(const TableNames& t, SqlDialect d) {
	return {"SELECT MAX(" + quote_identifier("account_id", d) + ") FROM " + quote_identifier(t.login, d), {}};
}

std::vector<Statement> remove_account(const TableNames& t, AccountId id, SqlDialect d) {
	const SqlValue p{std::uint64_t{id.value}};
	const std::string where = " WHERE " + quote_identifier("account_id", d) + " = ?";
	return {
	    {"DELETE FROM " + quote_identifier(t.global_acc_reg_num, d) + where, {p}},
	    {"DELETE FROM " + quote_identifier(t.global_acc_reg_str, d) + where, {p}},
	    {"DELETE FROM " + quote_identifier(t.login, d) + where, {p}},
	};
}

Statement select_account_by_id(const TableNames& t, AccountId id, SqlDialect d) {
	return {"SELECT " + column_list(d) + " FROM " + quote_identifier(t.login, d) + " WHERE " +
	            quote_identifier("account_id", d) + " = ?",
	        {std::uint64_t{id.value}}};
}

Statement select_account_by_userid(const TableNames& t, std::string_view userid, bool case_sensitive, SqlDialect d) {
	const auto col = quote_identifier("userid", d);
	const std::string where = case_sensitive ? col + " = ?" : "lower(" + col + ") = lower(?)";
	return {"SELECT " + column_list(d) + " FROM " + quote_identifier(t.login, d) + " WHERE " + where + " LIMIT 2",
	        {std::string(userid)}};
}

Statement insert_account(const TableNames& t, const Account& a, SqlDialect d) {
	auto params = account_values(a);
	params.insert(params.begin(), std::uint64_t{a.id.value});
	std::string cols = quote_identifier("account_id", d);
	std::string marks = "?";
	for (const auto c : kWrittenColumns) {
		cols += ", " + quote_identifier(c, d);
		marks += ", ?";
	}
	return {"INSERT INTO " + quote_identifier(t.login, d) + " (" + cols + ") VALUES (" + marks + ")", std::move(params)};
}

Statement update_account(const TableNames& t, const Account& a, SqlDialect d) {
	auto params = account_values(a);
	params.emplace_back(std::uint64_t{a.id.value});
	std::string sets;
	for (const auto c : kWrittenColumns) sets += (sets.empty() ? "" : ", ") + quote_identifier(c, d) + " = ?";
	return {"UPDATE " + quote_identifier(t.login, d) + " SET " + sets + " WHERE " + quote_identifier("account_id", d) + " = ?",
	        std::move(params)};
}

} // namespace sql

// ------------------------------------------------------------------ accounts
std::optional<Account> SqlAccountRepository::find(AccountId id) const {
	const auto rows = conn_.query(sql::select_account_by_id(names_, id, conn_.dialect()));
	if (rows.empty()) return std::nullopt;
	return account_from_row(rows.front());
}

std::optional<Account> SqlAccountRepository::find_by_userid(std::string_view userid, bool case_sensitive) const {
	const auto rows = conn_.query(sql::select_account_by_userid(names_, userid, case_sensitive, conn_.dialect()));
	if (rows.size() != 1) return std::nullopt; // none, or BR-LOGIN-038 ambiguity -> deny
	return account_from_row(rows.front());
}

AccountIdAllocation SqlAccountRepository::create(Account& account, std::optional<std::uint32_t> requested) {
	std::optional<std::uint32_t> current_max;
	if (!requested) {
		const auto rows = conn_.query(sql::select_max_account_id(names_, conn_.dialect()));
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
	conn_.execute(sql::insert_account(names_, row, conn_.dialect())); // duplicate id -> RepositoryError
	account.id = row.id;
	return alloc;
}

void SqlAccountRepository::save(const Account& account) {
	conn_.execute(sql::update_account(names_, account, conn_.dialect()));
	// SQLite and PostgreSQL report matched rows; the find() keeps this safe for engines that
	// report only changed rows.
	if (conn_.affected_rows() == 0 && !find(account.id)) throw RepositoryError("save of unknown account");
}

bool SqlAccountRepository::remove(AccountId id) {
	if (!find(id)) return false;
	return conn_.execute_in_transaction(sql::remove_account(names_, id, conn_.dialect()));
}

void SqlAccountRepository::set_global_reg(AccountId id, const GlobalRegNum& reg) {
	conn_.execute(upsert_reg(names_.global_acc_reg_num, conn_.dialect(),
	                         {std::uint64_t{id.value}, reg.key, std::uint64_t{reg.index}, reg.value}));
}

void SqlAccountRepository::set_global_reg(AccountId id, const GlobalRegStr& reg) {
	conn_.execute(upsert_reg(names_.global_acc_reg_str, conn_.dialect(),
	                         {std::uint64_t{id.value}, reg.key, std::uint64_t{reg.index}, reg.value}));
}

std::vector<GlobalRegNum> SqlAccountRepository::global_regs_num(AccountId id) const {
	const auto d = conn_.dialect();
	auto q = [d](std::string_view n) { return quote_identifier(n, d); };
	std::vector<GlobalRegNum> out;
	for (const auto& r : conn_.query({"SELECT " + q("key") + ", " + q("index") + ", " + q("value") + " FROM " +
	                                      q(names_.global_acc_reg_num) + " WHERE " + q("account_id") + " = ? ORDER BY " +
	                                      q("key") + ", " + q("index"),
	                                  {std::uint64_t{id.value}}}))
		out.push_back({as_text(r, "key"), as_int<std::uint32_t>(r, "index"), as_int<std::int64_t>(r, "value")});
	return out;
}

std::vector<GlobalRegStr> SqlAccountRepository::global_regs_str(AccountId id) const {
	const auto d = conn_.dialect();
	auto q = [d](std::string_view n) { return quote_identifier(n, d); };
	std::vector<GlobalRegStr> out;
	for (const auto& r : conn_.query({"SELECT " + q("key") + ", " + q("index") + ", " + q("value") + " FROM " +
	                                      q(names_.global_acc_reg_str) + " WHERE " + q("account_id") + " = ? ORDER BY " +
	                                      q("key") + ", " + q("index"),
	                                  {std::uint64_t{id.value}}}))
		out.push_back({as_text(r, "key"), as_int<std::uint32_t>(r, "index"), as_text(r, "value")});
	return out;
}

// ------------------------------------------------------------------ characters
std::optional<Character> SqlCharacterRepository::find(CharId) const { throw NotImplemented("SqlCharacterRepository::find"); }
std::vector<Character> SqlCharacterRepository::list_by_account(AccountId) const {
	throw NotImplemented("SqlCharacterRepository::list_by_account");
}
std::optional<Character> SqlCharacterRepository::find_by_name(std::string_view) const {
	throw NotImplemented("SqlCharacterRepository::find_by_name");
}
CharId SqlCharacterRepository::create(Character) { throw NotImplemented("SqlCharacterRepository::create"); }
void SqlCharacterRepository::save(const Character&) { throw NotImplemented("SqlCharacterRepository::save"); }
bool SqlCharacterRepository::remove(CharId) { throw NotImplemented("SqlCharacterRepository::remove (BR-CHAR-013)"); }

// ------------------------------------------------------------------ parties
std::optional<Party> SqlPartyRepository::find(PartyId) const { throw NotImplemented("SqlPartyRepository::find"); }
PartyId SqlPartyRepository::create(Party) { throw NotImplemented("SqlPartyRepository::create"); }
void SqlPartyRepository::save(const Party&) { throw NotImplemented("SqlPartyRepository::save"); }
bool SqlPartyRepository::remove(PartyId) { throw NotImplemented("SqlPartyRepository::remove"); }

// ------------------------------------------------------------------ guilds
std::optional<Guild> SqlGuildRepository::find(GuildId) const { throw NotImplemented("SqlGuildRepository::find"); }
GuildId SqlGuildRepository::create(Guild) { throw NotImplemented("SqlGuildRepository::create"); }
void SqlGuildRepository::save(const Guild&) { throw NotImplemented("SqlGuildRepository::save"); }
bool SqlGuildRepository::remove(GuildId) { throw NotImplemented("SqlGuildRepository::remove"); }

// ------------------------------------------------------------------ mail
std::optional<Mail> SqlMailRepository::find(MailId) const { throw NotImplemented("SqlMailRepository::find"); }
std::vector<Mail> SqlMailRepository::inbox(CharId, std::size_t) const { throw NotImplemented("SqlMailRepository::inbox"); }
MailId SqlMailRepository::create(Mail) { throw NotImplemented("SqlMailRepository::create"); }
void SqlMailRepository::save(const Mail&) { throw NotImplemented("SqlMailRepository::save"); }
bool SqlMailRepository::remove(MailId) { throw NotImplemented("SqlMailRepository::remove"); }

} // namespace rathena::persistence
