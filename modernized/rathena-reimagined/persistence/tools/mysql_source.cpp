// MysqlSource: the MySQL/MariaDB C API, text protocol, streamed (mysql_use_result) so a large
// table never has to fit in memory. Only identifiers checked by is_safe_identifier() reach the SQL
// text; the queries take no values.
#include "mysql_source.hpp"

#include <mysql.h>

#include <cctype>
#include <cstdlib>
#include <string>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::migrate_tool {

using persistence::RepositoryError;
using persistence::migrate::RawRow;
using persistence::migrate::SourceColumn;
using persistence::migrate::SourceTable;

struct MysqlSource::Impl {
	MYSQL* handle = nullptr;
	~Impl() {
		if (handle != nullptr) mysql_close(handle);
	}
};

namespace {

std::string bq(std::string_view id) {
	if (!persistence::is_safe_identifier(id)) throw RepositoryError("unsupported table or column name in the source");
	return "`" + std::string(id) + "`";
}

struct ResultGuard {
	MYSQL_RES* res;
	~ResultGuard() {
		if (res != nullptr) mysql_free_result(res);
	}
};

[[noreturn]] void fail(MYSQL* db, const std::string& what) {
	throw RepositoryError(what + ": " + mysql_error(db) + " (errno " + std::to_string(mysql_errno(db)) + ")");
}

void run(MYSQL* db, const std::string& sql) {
	if (mysql_real_query(db, sql.data(), static_cast<unsigned long>(sql.size())) != 0) fail(db, "query failed");
}

/// Runs a query and returns every row as optional strings (small metadata queries only).
std::vector<RawRow> fetch_all(MYSQL* db, const std::string& sql) {
	run(db, sql);
	ResultGuard g{mysql_store_result(db)};
	if (g.res == nullptr) fail(db, "store_result failed");
	const unsigned int n = mysql_num_fields(g.res);
	std::vector<RawRow> out;
	while (MYSQL_ROW row = mysql_fetch_row(g.res)) {
		const unsigned long* len = mysql_fetch_lengths(g.res);
		RawRow r(n);
		for (unsigned int i = 0; i < n; ++i)
			if (row[i] != nullptr) r[i] = std::string(row[i], len[i]);
		out.push_back(std::move(r));
	}
	return out;
}

std::string lower(std::string s) {
	for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

} // namespace

MysqlSource::MysqlSource(persistence::DbEndpoint endpoint) : endpoint_(std::move(endpoint)), impl_(std::make_unique<Impl>()) {}
MysqlSource::~MysqlSource() = default;

void MysqlSource::connect() {
	if (impl_->handle != nullptr) return;
	MYSQL* db = mysql_init(nullptr);
	if (db == nullptr) throw RepositoryError("mysql_init failed (out of memory)");
	const unsigned int timeout_s = 10;
	mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_s);
	// `binary`: the server sends every string exactly as stored, without charset conversion.
	mysql_options(db, MYSQL_SET_CHARSET_NAME, "binary");
	if (mysql_real_connect(db, endpoint_.host.c_str(), endpoint_.user.c_str(), endpoint_.password.c_str(),
	                       endpoint_.database.c_str(), endpoint_.port, nullptr, 0) == nullptr) {
		const std::string err = mysql_error(db);
		mysql_close(db);
		throw RepositoryError("connect to " + endpoint_.redacted() + " failed: " + err);
	}
	impl_->handle = db;
	// MySQL 8 caches information_schema.TABLES.AUTO_INCREMENT; MariaDB has no such variable.
	(void)mysql_query(db, "SET SESSION information_schema_stats_expiry = 0");
	run(db, "SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ");
	run(db, "START TRANSACTION WITH CONSISTENT SNAPSHOT");
}

std::vector<SourceTable> MysqlSource::tables() {
	connect();
	std::vector<SourceTable> out;
	for (const auto& r : fetch_all(impl_->handle, "SELECT TABLE_NAME, AUTO_INCREMENT FROM information_schema.TABLES"
	                                              " WHERE TABLE_SCHEMA = DATABASE() AND TABLE_TYPE = 'BASE TABLE'"
	                                              " ORDER BY TABLE_NAME")) {
		SourceTable t;
		t.name = r[0].value_or("");
		t.auto_increment = r[1] ? std::strtoull(r[1]->c_str(), nullptr, 10) : 0;
		out.push_back(std::move(t));
	}
	for (const auto& r : fetch_all(impl_->handle,
	                               "SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE, COLUMN_TYPE, IS_NULLABLE, EXTRA,"
	                               " CHARACTER_SET_NAME FROM information_schema.COLUMNS"
	                               " WHERE TABLE_SCHEMA = DATABASE() ORDER BY TABLE_NAME, ORDINAL_POSITION")) {
		for (auto& t : out) {
			if (t.name != r[0].value_or("")) continue;
			SourceColumn c;
			c.name = r[1].value_or("");
			c.data_type = lower(r[2].value_or(""));
			c.column_type = r[3].value_or("");
			c.nullable = r[4].value_or("") == "YES";
			c.auto_increment = lower(r[5].value_or("")).find("auto_increment") != std::string::npos;
			c.charset = lower(r[6].value_or(""));
			t.columns.push_back(std::move(c));
			break;
		}
	}
	return out;
}

std::uint64_t MysqlSource::row_count(const SourceTable& table) {
	connect();
	const auto rows = fetch_all(impl_->handle, "SELECT COUNT(*) FROM " + bq(table.name));
	return rows.empty() || !rows[0][0] ? 0 : std::strtoull(rows[0][0]->c_str(), nullptr, 10);
}

void MysqlSource::read_rows(const SourceTable& table, const std::vector<const SourceColumn*>& columns,
                            bool utf8_as_latin1, std::size_t batch_size,
                            const std::function<void(std::vector<RawRow>&)>& sink) {
	connect();
	std::string sql = "SELECT ";
	for (std::size_t i = 0; i < columns.size(); ++i) {
		const auto& c = *columns[i];
		const bool recode = utf8_as_latin1 && c.charset.starts_with("utf8");
		sql += (i ? ", " : "") + (recode ? "CONVERT(" + bq(c.name) + " USING latin1)" : bq(c.name));
	}
	sql += " FROM " + bq(table.name);
	run(impl_->handle, sql);
	ResultGuard g{mysql_use_result(impl_->handle)};
	if (g.res == nullptr) fail(impl_->handle, "use_result failed");
	const unsigned int n = mysql_num_fields(g.res);
	std::vector<RawRow> batch;
	batch.reserve(batch_size);
	while (MYSQL_ROW row = mysql_fetch_row(g.res)) {
		const unsigned long* len = mysql_fetch_lengths(g.res);
		RawRow r(n);
		for (unsigned int i = 0; i < n; ++i)
			if (row[i] != nullptr) r[i] = std::string(row[i], len[i]);
		batch.push_back(std::move(r));
		if (batch.size() >= batch_size) {
			sink(batch);
			batch.clear();
		}
	}
	if (mysql_errno(impl_->handle) != 0) fail(impl_->handle, "reading " + table.name + " failed");
	if (!batch.empty()) sink(batch);
}

} // namespace rathena::migrate_tool
