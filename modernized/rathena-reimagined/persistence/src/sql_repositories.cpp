#include "rathena/persistence/sql_repositories.hpp"

#include <span>
#include <type_traits>
#include <utility>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/item_mapping.hpp"

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

// ================================================================== character-side repositories

namespace {

/// Double-quoted identifier; the quoting is the same on every dialect (sql_connection.hpp).
std::string qid(std::string_view name) { return quote_identifier(name, SqlDialect::Sqlite); }

std::string joined(std::span<const std::string_view> cols, std::string_view suffix = "") {
	std::string out;
	for (const auto c : cols) out += (out.empty() ? "" : ", ") + qid(c) + std::string(suffix);
	return out;
}

std::string marks(std::size_t n) {
	std::string out;
	for (std::size_t i = 0; i < n; ++i) out += i == 0 ? "?" : ", ?";
	return out;
}

/// INSERT INTO "t" (cols) VALUES (?...) RETURNING "key": the generated id comes back with the row.
Statement insert_returning(const std::string& table, std::span<const std::string_view> cols, std::vector<SqlValue> values,
                           std::string_view key) {
	return {"INSERT INTO " + qid(table) + " (" + joined(cols) + ") VALUES (" + marks(cols.size()) + ") RETURNING " + qid(key),
	        std::move(values)};
}

/// UPDATE "t" SET "a" = ?, ... WHERE "key" = ? (the key value is the last parameter).
Statement update_where(const std::string& table, std::span<const std::string_view> cols, std::vector<SqlValue> values,
                       std::string_view key, SqlValue key_value) {
	values.push_back(std::move(key_value));
	return {"UPDATE " + qid(table) + " SET " + joined(cols, " = ?") + " WHERE " + qid(key) + " = ?", std::move(values)};
}

Statement select_where(const std::string& table, std::span<const std::string_view> cols, std::string_view key, SqlValue value,
                       std::string_view order_by) {
	return {"SELECT " + joined(cols) + " FROM " + qid(table) + " WHERE " + qid(key) + " = ? ORDER BY " + qid(order_by),
	        {std::move(value)}};
}

Statement delete_where(const std::string& table, std::string_view key, SqlValue value) {
	return {"DELETE FROM " + qid(table) + " WHERE " + qid(key) + " = ?", {std::move(value)}};
}

SqlValue u64(std::uint64_t v) { return SqlValue{v}; }

/// A "none" reference is NULL (the legacy schema's 0 sentinel is gone in the target schema).
template <class T>
SqlValue nullable(const std::optional<T>& v) {
	if (!v) return std::monostate{};
	if constexpr (std::is_integral_v<T>) return u64(*v);
	else return u64(v->value);
}

// Reads are checked: a value the domain type cannot hold is a MappingError, never a wrap-around.
const SqlValue& cell(const Row& r, std::string_view col) {
	const auto it = r.find(col);
	if (it == r.end()) throw MappingError("missing column " + std::string(col));
	return it->second;
}

std::optional<std::int64_t> nullable_int(const Row& r, std::string_view col) {
	const auto& v = cell(r, col);
	if (std::holds_alternative<std::monostate>(v)) return std::nullopt;
	if (const auto* i = std::get_if<std::int64_t>(&v)) return *i;
	if (const auto* u = std::get_if<std::uint64_t>(&v); u != nullptr && std::in_range<std::int64_t>(*u))
		return static_cast<std::int64_t>(*u);
	throw MappingError("column " + std::string(col) + " is not an integer");
}

template <class T>
std::optional<T> nullable_as(const Row& r, std::string_view col) {
	const auto v = nullable_int(r, col);
	if (!v) return std::nullopt;
	if (!std::in_range<T>(*v)) throw MappingError("column " + std::string(col) + " is out of range");
	return static_cast<T>(*v);
}

template <class T>
T int_as(const Row& r, std::string_view col) {
	const auto v = nullable_as<T>(r, col);
	if (!v) throw MappingError("column " + std::string(col) + " is NULL");
	return *v;
}

template <class Id>
std::optional<Id> nullable_id(const Row& r, std::string_view col) {
	const auto v = nullable_as<decltype(Id::value)>(r, col);
	if (!v) return std::nullopt;
	return Id{*v};
}

std::string text_of(const Row& r, std::string_view col) {
	if (const auto* s = std::get_if<std::string>(&cell(r, col))) return *s;
	throw MappingError("column " + std::string(col) + " is not text");
}

Zeny zeny_of(const Row& r, std::string_view col) {
	const auto z = Zeny::from_db(int_as<std::int64_t>(r, col));
	if (!z) throw MappingError("column " + std::string(col) + " is outside 0..MAX_ZENY (BR-MAP-045)");
	return *z;
}

/// The id a RETURNING clause produced.
template <class T>
T returned_id(const std::vector<Row>& rows, std::string_view key) {
	if (rows.size() != 1) throw RepositoryError("INSERT ... RETURNING produced no row");
	return int_as<T>(rows.front(), key);
}

/// The ONE item mapping refuses a field the table cannot hold; that is a repository error (and is
/// checked before a transaction starts, so nothing is written).
void check_items(const std::vector<StoredItem>& items, ItemTable t) {
	for (const auto& s : items) {
		try {
			(void)to_row(s.item, t);
		} catch (const MappingError& e) {
			throw RepositoryError(e.what());
		}
	}
}

std::vector<StoredItem> load_items(SqlConnection& conn, const TableNames& names, ItemTable t, std::uint64_t owner) {
	std::vector<StoredItem> out;
	for (const auto& r : conn.query(select_statement(names, t, owner))) out.push_back({int_as<std::uint64_t>(r, "id"), from_row(r, t)});
	return out;
}

/// Delete-then-insert of one owner's items; rows keep their ids (StoredItem::row_id).
void rewrite_items(SqlConnection& conn, const TableNames& names, ItemTable t, std::uint64_t owner,
                   const std::vector<StoredItem>& items) {
	conn.execute(delete_statement(names, t, owner));
	for (const auto& s : items) conn.execute(insert_statement(names, t, owner, s));
}

/// Columns of persistence::Character in `char`; kCharColumns[0] is the key, the rest are written.
constexpr std::string_view kCharColumns[] = {
    "char_id", "account_id", "char_num", "name",     "class",  "base_level", "zeny",     "party_id",    "guild_id",
    "clan_id", "partner_id", "father",   "mother",   "pet_id", "homun_id",   "elemental_id", "delete_date"};
constexpr std::span<const std::string_view> kCharWritten = std::span(kCharColumns).subspan(1);

std::vector<SqlValue> char_values(const Character& c) {
	return {u64(c.account.value),   u64(c.slot),       c.name,              u64(c.job_class),
	        u64(c.base_level),      std::int64_t{c.zeny.value()}, nullable(c.party), nullable(c.guild),
	        nullable(c.clan),       nullable(c.partner), nullable(c.father), nullable(c.mother),
	        nullable(c.pet_id),     nullable(c.homun_id), nullable(c.elemental_id), c.delete_date};
}

Character character_from_row(const Row& r) {
	Character c;
	c.id = CharId{int_as<std::uint32_t>(r, "char_id")};
	c.account = AccountId{int_as<std::uint32_t>(r, "account_id")};
	c.slot = int_as<std::uint8_t>(r, "char_num");
	c.name = text_of(r, "name");
	c.job_class = int_as<std::uint32_t>(r, "class");
	c.base_level = int_as<std::uint32_t>(r, "base_level");
	c.zeny = zeny_of(r, "zeny");
	c.party = nullable_id<PartyId>(r, "party_id");
	c.guild = nullable_id<GuildId>(r, "guild_id");
	c.clan = nullable_as<std::uint32_t>(r, "clan_id");
	c.partner = nullable_id<CharId>(r, "partner_id");
	c.father = nullable_id<CharId>(r, "father");
	c.mother = nullable_id<CharId>(r, "mother");
	c.pet_id = nullable_as<std::uint32_t>(r, "pet_id");
	c.homun_id = nullable_as<std::uint32_t>(r, "homun_id");
	c.elemental_id = nullable_as<std::uint32_t>(r, "elemental_id");
	c.delete_date = int_as<std::int64_t>(r, "delete_date");
	return c;
}

constexpr std::string_view kPartyColumns[] = {"party_id", "name", "exp", "leader_id", "leader_char"};
constexpr std::span<const std::string_view> kPartyWritten = std::span(kPartyColumns).subspan(1);

std::vector<SqlValue> party_values(const Party& p) {
	return {p.name, u64(p.exp_share ? 1 : 0), u64(p.leader_account.value), u64(p.leader_char.value)};
}

constexpr std::string_view kGuildColumns[] = {"guild_id", "name", "char_id", "master", "guild_lv"};
constexpr std::span<const std::string_view> kGuildWritten = std::span(kGuildColumns).subspan(1);
constexpr std::string_view kGuildMemberColumns[] = {"guild_id", "char_id", "position", "exp"};
constexpr std::string_view kGuildPositionColumns[] = {"guild_id", "position", "name", "mode", "exp_mode"};

std::vector<SqlValue> guild_values(const Guild& g) {
	return {g.name, u64(g.master.value), g.master_name, u64(g.guild_lv)};
}

constexpr std::string_view kMailColumns[] = {"id",      "send_name", "send_id", "dest_name", "dest_id",
                                             "title",   "message",   "time",    "status",    "zeny"};
constexpr std::span<const std::string_view> kMailWritten = std::span(kMailColumns).subspan(1);

std::vector<SqlValue> mail_values(const Mail& m) {
	return {m.send_name, nullable(m.sender), m.dest_name, u64(m.dest.value), m.title, m.message, m.time,
	        u64(static_cast<std::uint8_t>(m.status)), std::int64_t{m.zeny.value()}};
}

Mail mail_from_row(const Row& r) {
	Mail m;
	m.id = MailId{int_as<std::uint64_t>(r, "id")};
	m.send_name = text_of(r, "send_name");
	m.sender = nullable_id<CharId>(r, "send_id");
	m.dest_name = text_of(r, "dest_name");
	m.dest = CharId{int_as<std::uint32_t>(r, "dest_id")};
	m.title = text_of(r, "title");
	m.message = text_of(r, "message");
	m.time = int_as<std::int64_t>(r, "time");
	const auto status = int_as<std::uint8_t>(r, "status");
	if (status > static_cast<std::uint8_t>(MailStatus::Returned)) throw MappingError("mail.status out of range");
	m.status = static_cast<MailStatus>(status);
	m.zeny = zeny_of(r, "zeny");
	return m;
}

void check_mail(const Mail& m) {
	if (m.attachments.size() > kMailMaxItems) throw RepositoryError("too many mail attachments");
	for (const auto& item : m.attachments) {
		try {
			(void)to_row(item, ItemTable::MailAttachments);
		} catch (const MappingError& e) {
			throw RepositoryError(e.what());
		}
	}
}

/// Attachments are keyed (mail id, index); the index is the position in Mail::attachments.
void rewrite_attachments(SqlConnection& conn, const TableNames& names, const Mail& m) {
	conn.execute(delete_statement(names, ItemTable::MailAttachments, m.id.value));
	for (std::size_t i = 0; i < m.attachments.size(); ++i)
		conn.execute(insert_statement(names, ItemTable::MailAttachments, m.id.value, m.attachments[i], static_cast<std::uint32_t>(i)));
}

/// BR-CHAR-042: everything of the guild goes and its members' guild is cleared. The foreign keys
/// would cascade too; the explicit statements keep the order visible and work on any schema.
std::vector<Statement> break_guild(const TableNames& t, GuildId id) {
	const SqlValue g = u64(id.value);
	return {
	    {"UPDATE " + qid(t.character) + " SET " + qid("guild_id") + " = NULL WHERE " + qid("guild_id") + " = ?", {g}},
	    delete_where(t.guild_storage, "guild_id", g),
	    delete_where(t.guild_position, "guild_id", g),
	    delete_where(t.guild_member, "guild_id", g),
	    delete_where(t.guild, "guild_id", g),
	};
}

constexpr std::uint32_t kWeddingRingM = 2634; ///< WEDDING_RING_M (common/mmo.hpp)
constexpr std::uint32_t kWeddingRingF = 2635; ///< WEDDING_RING_F
constexpr std::uint32_t kSkillCallBaby = 410; ///< WE_CALLBABY, removed from the parents on de-adopt
constexpr std::uint32_t kCard0Pet = 256;      ///< CARD0_PET: card1 | card2 << 16 is the egg's pet_id

} // namespace

// ------------------------------------------------------------------ characters
std::vector<Character> SqlCharacterRepository::load(const Statement& select) const {
	std::vector<Character> out;
	for (const auto& r : conn_.query(select)) {
		auto c = character_from_row(r);
		c.inventory = load_items(conn_, names_, ItemTable::Inventory, c.id.value);
		c.cart = load_items(conn_, names_, ItemTable::Cart, c.id.value);
		out.push_back(std::move(c));
	}
	return out;
}

std::optional<Character> SqlCharacterRepository::find(CharId id) const {
	auto found = load(select_where(names_.character, kCharColumns, "char_id", u64(id.value), "char_id"));
	if (found.empty()) return std::nullopt;
	return std::move(found.front());
}

std::vector<Character> SqlCharacterRepository::list_by_account(AccountId account) const {
	return load(select_where(names_.character, kCharColumns, "account_id", u64(account.value), "char_id"));
}

std::optional<Character> SqlCharacterRepository::find_by_name(std::string_view name) const {
	auto found = load(select_where(names_.character, kCharColumns, "name", std::string(name), "char_id"));
	if (found.empty()) return std::nullopt;
	return std::move(found.front());
}

void SqlCharacterRepository::write_items(const Character& character) {
	rewrite_items(conn_, names_, ItemTable::Inventory, character.id.value, character.inventory);
	rewrite_items(conn_, names_, ItemTable::Cart, character.id.value, character.cart);
}

CharId SqlCharacterRepository::create(Character character) {
	check_items(character.inventory, ItemTable::Inventory);
	check_items(character.cart, ItemTable::Cart);
	// The id comes from the char_id identity (START_CHAR_NUM); the name is UNIQUE in the schema.
	conn_.transaction([&] {
		character.id = CharId{returned_id<std::uint32_t>(
		    conn_.query(insert_returning(names_.character, kCharWritten, char_values(character), "char_id")), "char_id")};
		write_items(character);
	});
	return character.id;
}

void SqlCharacterRepository::save(const Character& character) {
	check_items(character.inventory, ItemTable::Inventory);
	check_items(character.cart, ItemTable::Cart);
	conn_.transaction([&] {
		conn_.execute(update_where(names_.character, kCharWritten, char_values(character), "char_id", u64(character.id.value)));
		if (conn_.affected_rows() == 0) throw RepositoryError("save of unknown character");
		write_items(character);
	});
}

bool SqlCharacterRepository::remove(CharId id) {
	const auto character = find(id);
	if (!character) return false;
	try {
		conn_.transaction([&] {
			for (const auto& step : character_delete_plan()) cleanup(step, *character);
		});
	} catch (const RepositoryError&) {
		return false;
	}
	return true;
}

/// One BR-CHAR-013 step (character_delete_plan() order), inside remove()'s transaction.
void SqlCharacterRepository::cleanup(const CleanupStepInfo& step, const Character& c) {
	const auto& t = names_;
	const SqlValue id = u64(c.id.value);
	auto run = [&](std::string sql, std::vector<SqlValue> params) { conn_.execute({std::move(sql), std::move(params)}); };
	auto by_char = [&](const std::string& table) { conn_.execute(delete_where(table, "char_id", id)); };
	const std::string chr = qid(t.character);
	switch (step.step) {
	case CleanupStep::Divorce:
		if (c.partner) {
			run("UPDATE " + chr + " SET " + qid("partner_id") + " = NULL WHERE " + qid("char_id") + " IN (?, ?)", {id, nullable(c.partner)});
			// The partner's wedding ring goes too (legacy removes at most one ring each).
			run("DELETE FROM " + qid(t.inventory) + " WHERE " + qid("id") + " IN (SELECT " + qid("id") + " FROM " + qid(t.inventory) +
			        " WHERE " + qid("char_id") + " = ? AND " + qid("nameid") + " IN (?, ?) ORDER BY " + qid("id") + " LIMIT 1)",
			    {nullable(c.partner), u64(kWeddingRingM), u64(kWeddingRingF)});
		}
		break;
	case CleanupStep::DeAdopt:
		if (c.father || c.mother) {
			run("UPDATE " + chr + " SET " + qid("child") + " = NULL WHERE " + qid("char_id") + " IN (?, ?)",
			    {nullable(c.father), nullable(c.mother)});
			run("DELETE FROM " + qid(t.skill) + " WHERE " + qid("id") + " = ? AND " + qid("char_id") + " IN (?, ?)",
			    {u64(kSkillCallBaby), nullable(c.father), nullable(c.mother)});
		}
		break;
	case CleanupStep::LeaveParty:
		if (c.party) {
			const SqlValue p = nullable(c.party);
			const auto leader = conn_.query({"SELECT " + qid("leader_char") + " FROM " + qid(t.party) + " WHERE " + qid("party_id") + " = ?", {p}});
			run("UPDATE " + chr + " SET " + qid("party_id") + " = NULL WHERE " + qid("char_id") + " = ?", {id});
			const bool was_leader = !leader.empty() && int_as<std::uint32_t>(leader.front(), "leader_char") == c.id.value;
			const bool empty = conn_.query({"SELECT 1 FROM " + chr + " WHERE " + qid("party_id") + " = ? LIMIT 1", {p}}).empty();
			if (was_leader || empty) { // BR-CHAR-035 (leader left) / BR-CHAR-036 (no member left): dissolve
				run("UPDATE " + chr + " SET " + qid("party_id") + " = NULL WHERE " + qid("party_id") + " = ?", {p});
				conn_.execute(delete_where(t.party, "party_id", p));
			}
		}
		break;
	case CleanupStep::DeleteHatchedPet:
		run("DELETE FROM " + qid(t.pet) + " WHERE " + qid("char_id") + " = ? AND " + qid("incubate") + " = 0", {id});
		break;
	case CleanupStep::DeleteEggPets:
		for (const auto* items : {&t.inventory, &t.cart_inventory})
			run("DELETE FROM " + qid(t.pet) + " WHERE " + qid("pet_id") + " IN (SELECT (" + qid("card1") + " | (" + qid("card2") +
			        " << 16)) FROM " + qid(*items) + " WHERE " + qid("char_id") + " = ? AND " + qid("card0") + " = ?)",
			    {id, u64(kCard0Pet)});
		break;
	case CleanupStep::DeleteHomunculus:
		run("DELETE FROM " + qid(t.homunculus) + " WHERE " + qid("char_id") + " = ? OR " + qid("homun_id") + " = ?",
		    {id, nullable(c.homun_id)});
		break;
	case CleanupStep::DeleteElemental: by_char(t.elemental); break;
	case CleanupStep::DeleteMercenaryOwner: by_char(t.mercenary_owner); break;
	case CleanupStep::DeleteOwnFriends: by_char(t.friends); break;
	case CleanupStep::DeleteFromOthersFriends: conn_.execute(delete_where(t.friends, "friend_id", id)); break;
	case CleanupStep::DeleteHotkeys: by_char(t.hotkey); break;
	case CleanupStep::DeleteInventory: conn_.execute(delete_statement(t, ItemTable::Inventory, c.id.value)); break;
	case CleanupStep::DeleteCart: conn_.execute(delete_statement(t, ItemTable::Cart, c.id.value)); break;
	case CleanupStep::DeleteMemo: by_char(t.memo); break;
	case CleanupStep::DeleteCharRegStr: by_char(t.char_reg_str); break;
	case CleanupStep::DeleteCharRegNum: by_char(t.char_reg_num); break;
	case CleanupStep::DeleteSkills: by_char(t.skill); break;
	case CleanupStep::DeleteReceivedMailAttachments:
		run("DELETE FROM " + qid(t.mail_attachments) + " WHERE " + qid("id") + " IN (SELECT " + qid("id") + " FROM " + qid(t.mail) +
		        " WHERE " + qid("dest_id") + " = ?)",
		    {id});
		break;
	case CleanupStep::DeleteReceivedMail: conn_.execute(delete_where(t.mail, "dest_id", id)); break;
	case CleanupStep::AnonymizeSentMail:
		run("UPDATE " + qid(t.mail) + " SET " + qid("send_id") + " = NULL WHERE " + qid("send_id") + " = ?", {id});
		break;
	case CleanupStep::DeleteStatusChanges: by_char(t.sc_data); break;
	case CleanupStep::DeleteBonusScripts: by_char(t.bonus_script); break;
	case CleanupStep::DeleteQuests: by_char(t.quest); break;
	case CleanupStep::DeleteAchievements: by_char(t.achievement); break;
	case CleanupStep::GuildBreakOrLeave:
		// Break the guild this character is master of (its RESTRICT key would block the row
		// delete), otherwise leave: the member row goes, the guild stays.
		for (const auto& r : conn_.query({"SELECT " + qid("guild_id") + " FROM " + qid(t.guild) + " WHERE " + qid("char_id") + " = ?", {id}}))
			for (const auto& st : break_guild(t, GuildId{int_as<std::uint32_t>(r, "guild_id")})) conn_.execute(st);
		by_char(t.guild_member);
		break;
	case CleanupStep::DeleteCharacterRow: by_char(t.character); break;
	}
}

// ------------------------------------------------------------------ parties
std::optional<Party> SqlPartyRepository::find(PartyId id) const {
	const auto rows = conn_.query(select_where(names_.party, kPartyColumns, "party_id", u64(id.value), "party_id"));
	if (rows.empty()) return std::nullopt;
	const auto& r = rows.front();
	Party p;
	p.id = PartyId{int_as<std::uint32_t>(r, "party_id")};
	p.name = text_of(r, "name");
	p.exp_share = int_as<std::int64_t>(r, "exp") != 0;
	p.leader_account = AccountId{int_as<std::uint32_t>(r, "leader_id")};
	p.leader_char = CharId{int_as<std::uint32_t>(r, "leader_char")};
	return p;
}

PartyId SqlPartyRepository::create(Party party) {
	// One statement; a duplicate name violates the UNIQUE key (BR-CHAR-033) -> RepositoryError.
	return PartyId{returned_id<std::uint32_t>(conn_.query(insert_returning(names_.party, kPartyWritten, party_values(party), "party_id")),
	                                          "party_id")};
}

void SqlPartyRepository::save(const Party& party) {
	conn_.execute(update_where(names_.party, kPartyWritten, party_values(party), "party_id", u64(party.id.value)));
	if (conn_.affected_rows() == 0) throw RepositoryError("save of unknown party");
}

bool SqlPartyRepository::remove(PartyId id) {
	if (!find(id)) return false;
	const SqlValue p = u64(id.value);
	return conn_.execute_in_transaction({
	    delete_where(names_.party, "party_id", p),
	    {"UPDATE " + qid(names_.character) + " SET " + qid("party_id") + " = NULL WHERE " + qid("party_id") + " = ?", {p}},
	});
}

std::vector<CharId> SqlPartyRepository::members(PartyId id) const {
	constexpr std::string_view cols[] = {"char_id"};
	std::vector<CharId> out;
	for (const auto& r : conn_.query(select_where(names_.character, cols, "party_id", u64(id.value), "char_id")))
		out.push_back(CharId{int_as<std::uint32_t>(r, "char_id")});
	return out;
}

// ------------------------------------------------------------------ guilds
std::optional<Guild> SqlGuildRepository::find(GuildId id) const {
	const SqlValue gid = u64(id.value);
	const auto rows = conn_.query(select_where(names_.guild, kGuildColumns, "guild_id", gid, "guild_id"));
	if (rows.empty()) return std::nullopt;
	const auto& r = rows.front();
	Guild g;
	g.id = GuildId{int_as<std::uint32_t>(r, "guild_id")};
	g.name = text_of(r, "name");
	g.master = CharId{int_as<std::uint32_t>(r, "char_id")};
	g.master_name = text_of(r, "master");
	g.guild_lv = int_as<std::uint32_t>(r, "guild_lv");
	for (const auto& m : conn_.query(select_where(names_.guild_member, kGuildMemberColumns, "guild_id", gid, "char_id")))
		g.members.push_back({CharId{int_as<std::uint32_t>(m, "char_id")}, int_as<std::uint32_t>(m, "position"),
		                     int_as<std::uint64_t>(m, "exp")});
	for (const auto& p : conn_.query(select_where(names_.guild_position, kGuildPositionColumns, "guild_id", gid, "position")))
		g.positions.push_back({int_as<std::uint32_t>(p, "position"), text_of(p, "name"), int_as<std::uint32_t>(p, "mode"),
		                       int_as<std::uint32_t>(p, "exp_mode")});
	g.storage = load_items(conn_, names_, ItemTable::GuildStorage, id.value);
	return g;
}

void SqlGuildRepository::write_children(const Guild& guild) {
	const SqlValue gid = u64(guild.id.value);
	conn_.execute(delete_where(names_.guild_member, "guild_id", gid));
	for (const auto& m : guild.members)
		conn_.execute({"INSERT INTO " + qid(names_.guild_member) + " (" + joined(kGuildMemberColumns) + ") VALUES (?, ?, ?, ?)",
		               {gid, u64(m.char_id.value), u64(m.position), u64(m.exp)}});
	conn_.execute(delete_where(names_.guild_position, "guild_id", gid));
	for (const auto& p : guild.positions)
		conn_.execute({"INSERT INTO " + qid(names_.guild_position) + " (" + joined(kGuildPositionColumns) + ") VALUES (?, ?, ?, ?, ?)",
		               {gid, u64(p.index), p.name, u64(p.mode), u64(p.exp_mode)}});
	rewrite_items(conn_, names_, ItemTable::GuildStorage, guild.id.value, guild.storage);
}

GuildId SqlGuildRepository::create(Guild guild) {
	check_items(guild.storage, ItemTable::GuildStorage);
	conn_.transaction([&] {
		guild.id = GuildId{returned_id<std::uint32_t>(
		    conn_.query(insert_returning(names_.guild, kGuildWritten, guild_values(guild), "guild_id")), "guild_id")};
		write_children(guild);
	});
	return guild.id;
}

void SqlGuildRepository::save(const Guild& guild) {
	check_items(guild.storage, ItemTable::GuildStorage);
	conn_.transaction([&] {
		conn_.execute(update_where(names_.guild, kGuildWritten, guild_values(guild), "guild_id", u64(guild.id.value)));
		if (conn_.affected_rows() == 0) throw RepositoryError("save of unknown guild");
		write_children(guild);
	});
}

bool SqlGuildRepository::remove(GuildId id) {
	constexpr std::string_view cols[] = {"guild_id"};
	if (conn_.query(select_where(names_.guild, cols, "guild_id", u64(id.value), "guild_id")).empty()) return false;
	return conn_.execute_in_transaction(break_guild(names_, id));
}

// ------------------------------------------------------------------ mail
std::vector<Mail> SqlMailRepository::load(const Statement& select) const {
	std::vector<Mail> out;
	for (const auto& r : conn_.query(select)) {
		auto m = mail_from_row(r);
		for (const auto& a : conn_.query(select_statement(names_, ItemTable::MailAttachments, m.id.value)))
			m.attachments.push_back(from_row(a, ItemTable::MailAttachments));
		out.push_back(std::move(m));
	}
	return out;
}

std::optional<Mail> SqlMailRepository::find(MailId id) const {
	auto found = load(select_where(names_.mail, kMailColumns, "id", u64(id.value), "id"));
	if (found.empty()) return std::nullopt;
	return std::move(found.front());
}

std::vector<Mail> SqlMailRepository::inbox(CharId dest, std::size_t limit) const {
	return load({"SELECT " + joined(kMailColumns) + " FROM " + qid(names_.mail) + " WHERE " + qid("dest_id") + " = ? ORDER BY " +
	                 qid("id") + " DESC LIMIT ?",
	             {u64(dest.value), u64(limit)}});
}

MailId SqlMailRepository::create(Mail mail) {
	check_mail(mail);
	conn_.transaction([&] {
		mail.id = MailId{returned_id<std::uint64_t>(conn_.query(insert_returning(names_.mail, kMailWritten, mail_values(mail), "id")), "id")};
		rewrite_attachments(conn_, names_, mail);
	});
	return mail.id;
}

void SqlMailRepository::save(const Mail& mail) {
	check_mail(mail);
	conn_.transaction([&] {
		conn_.execute(update_where(names_.mail, kMailWritten, mail_values(mail), "id", u64(mail.id.value)));
		if (conn_.affected_rows() == 0) throw RepositoryError("save of unknown mail");
		rewrite_attachments(conn_, names_, mail);
	});
}

bool SqlMailRepository::remove(MailId id) {
	constexpr std::string_view cols[] = {"id"};
	if (conn_.query(select_where(names_.mail, cols, "id", u64(id.value), "id")).empty()) return false;
	return conn_.execute_in_transaction({delete_statement(names_, ItemTable::MailAttachments, id.value),
	                                     delete_where(names_.mail, "id", u64(id.value))});
}

// ------------------------------------------------------------------ account storage
std::vector<StoredItem> SqlStorageRepository::load(AccountId account) const {
	return load_items(conn_, names_, ItemTable::Storage, account.value);
}

void SqlStorageRepository::save(AccountId account, const std::vector<StoredItem>& items) {
	check_items(items, ItemTable::Storage);
	conn_.transaction([&] { rewrite_items(conn_, names_, ItemTable::Storage, account.value, items); });
}

} // namespace rathena::persistence
