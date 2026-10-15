#include "rathena/persistence/migrate/import.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>

#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/migrate/schema.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence::migrate {
namespace {

/// The schema files, in load order. roulette_default_data.sql seeds rows (idempotently).
constexpr std::string_view kSchemaFiles[] = {"main.sql", "logs.sql", "web.sql", "roulette_default_data.sql"};
/// Row errors kept per table for the summary.
constexpr std::size_t kMaxErrors = 5;
/// Bound values per INSERT; below SQLite's 32766 and PostgreSQL's 65535 parameter limits.
constexpr std::size_t kMaxParamsPerStatement = 30000;
constexpr std::size_t kMaxRowsPerStatement = 500;

std::string lower(std::string_view s) {
	std::string out(s);
	for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

bool iequals(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::uint64_t to_count(const SqlValue& v) {
	if (const auto* u = std::get_if<std::uint64_t>(&v)) return *u;
	if (const auto* i = std::get_if<std::int64_t>(&v)) return *i < 0 ? 0 : static_cast<std::uint64_t>(*i);
	if (const auto* s = std::get_if<std::string>(&v)) {
		std::uint64_t n = 0;
		std::from_chars(s->data(), s->data() + s->size(), n);
		return n;
	}
	return 0;
}

std::string text(const Row& r, std::string_view col) {
	const auto it = r.find(col);
	if (it == r.end()) throw RepositoryError("missing column " + std::string(col));
	if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
	if (std::holds_alternative<std::monostate>(it->second)) return {};
	return std::to_string(to_count(it->second));
}

const SqlValue& value(const Row& r, std::string_view col) {
	const auto it = r.find(col);
	if (it == r.end()) throw RepositoryError("missing column " + std::string(col));
	return it->second;
}

bool is_null(const Row& r, std::string_view col) { return std::holds_alternative<std::monostate>(value(r, col)); }

std::string q(std::string_view id, SqlDialect d) { return quote_identifier(id, d); }

std::uint64_t count_rows(SqlConnection& conn, std::string_view table) {
	const auto rows = conn.query({"SELECT COUNT(*) AS n FROM " + q(table, conn.dialect()), {}});
	return rows.empty() ? 0 : to_count(value(rows.front(), "n"));
}

/// Printable form of a (possibly binary) value for the report, capped at 16 bytes.
std::string printable(std::string_view v) {
	std::string out;
	for (const char c : v.substr(0, 16)) {
		const auto u = static_cast<unsigned char>(c);
		if (u >= 0x20 && u < 0x7f && c != '\'') {
			out += c;
		} else {
			static constexpr char kHex[] = "0123456789abcdef";
			out += "\\x";
			out += kHex[u >> 4];
			out += kHex[u & 0xf];
		}
	}
	if (v.size() > 16) out += "...";
	return out;
}

bool is_date_type(std::string_view data_type) {
	return data_type == "datetime" || data_type == "timestamp" || data_type == "date";
}

SqlValue zero_value(TargetType t) {
	switch (t) {
	case TargetType::Integer: return std::int64_t{0};
	case TargetType::Real: return std::string("0");
	case TargetType::Blob: return Blob{};
	case TargetType::Timestamp: return std::string("1970-01-01 00:00:00");
	case TargetType::Date: return std::string("1970-01-01");
	case TargetType::Text: break;
	}
	return std::string{};
}

// ------------------------------------------------------------------ target inspection

TargetSchema inspect_postgres(SqlConnection& conn) {
	TargetSchema s;
	const auto enc = conn.query({"SELECT pg_encoding_to_char(encoding) AS enc FROM pg_database WHERE datname = current_database()", {}});
	if (!enc.empty()) s.encoding = text(enc.front(), "enc");

	std::map<std::string, TargetTable> tables;
	for (const auto& r : conn.query({"SELECT table_name AS name FROM information_schema.tables"
	                                 " WHERE table_schema = current_schema() AND table_type = 'BASE TABLE'",
	                                 {}})) {
		const auto name = text(r, "name");
		tables[name].name = name;
	}
	for (const auto& r : conn.query({"SELECT table_name AS tbl, column_name AS col, data_type AS type,"
	                                 " is_nullable AS nullable, column_default AS dflt, is_identity AS ident,"
	                                 " character_maximum_length AS maxlen FROM information_schema.columns"
	                                 " WHERE table_schema = current_schema() ORDER BY table_name, ordinal_position",
	                                 {}})) {
		const auto it = tables.find(text(r, "tbl"));
		if (it == tables.end()) continue;
		TargetColumn c;
		c.name = text(r, "col");
		c.type = target_type(text(r, "type"), SqlDialect::Postgres);
		c.nullable = text(r, "nullable") == "YES";
		if (!is_null(r, "dflt")) {
			const auto lits = quoted_literals(text(r, "dflt"));
			if (!lits.empty()) c.default_literal = lits.front();
		}
		c.identity = text(r, "ident") == "YES";
		if (!is_null(r, "maxlen") && text(r, "type") == "character varying")
			c.max_length = static_cast<std::size_t>(to_count(value(r, "maxlen")));
		it->second.columns.push_back(std::move(c));
	}
	// Single-column CHECK (col IN (...)) constraints, as PostgreSQL prints them:
	//   CHECK (((sex)::text = ANY ((ARRAY['F'::character varying, 'M'::character varying])::text[])))
	for (const auto& r : conn.query({"SELECT c.relname AS tbl, a.attname AS col, pg_get_constraintdef(k.oid) AS def"
	                                 " FROM pg_constraint k JOIN pg_class c ON c.oid = k.conrelid"
	                                 " JOIN pg_namespace n ON n.oid = c.relnamespace"
	                                 " JOIN pg_attribute a ON a.attrelid = k.conrelid AND a.attnum = k.conkey[1]"
	                                 " WHERE k.contype = 'c' AND n.nspname = current_schema()"
	                                 " AND array_length(k.conkey, 1) = 1",
	                                 {}})) {
		const auto def = text(r, "def");
		if (def.find("= ANY") == std::string::npos && def.find(" IN (") == std::string::npos) continue;
		const auto it = tables.find(text(r, "tbl"));
		if (it == tables.end()) continue;
		for (auto& c : it->second.columns)
			if (c.name == text(r, "col")) c.allowed = quoted_literals(def);
	}
	for (const auto& r : conn.query({"SELECT c.relname AS child, p.relname AS parent FROM pg_constraint k"
	                                 " JOIN pg_class c ON c.oid = k.conrelid JOIN pg_class p ON p.oid = k.confrelid"
	                                 " JOIN pg_namespace n ON n.oid = c.relnamespace"
	                                 " WHERE k.contype = 'f' AND n.nspname = current_schema()",
	                                 {}}))
		s.foreign_keys.emplace_back(text(r, "child"), text(r, "parent"));
	for (auto& [name, t] : tables) s.tables.push_back(std::move(t));
	return s;
}

TargetSchema inspect_sqlite(SqlConnection& conn) {
	TargetSchema s;
	for (const auto& r : conn.query({"SELECT name, sql FROM sqlite_master WHERE type = 'table'"
	                                 " AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY name",
	                                 {}})) {
		TargetTable t;
		t.name = text(r, "name");
		const std::string create = text(r, "sql");
		const bool autoincrement = lower(create).find("autoincrement") != std::string::npos;
		const auto cols = conn.query({"SELECT name, type, \"notnull\" AS nn, dflt_value AS dflt, pk"
		                              " FROM pragma_table_info(?) ORDER BY cid",
		                              {t.name}});
		const auto pk_columns = std::count_if(cols.begin(), cols.end(), [](const Row& c) { return to_count(value(c, "pk")) > 0; });
		for (const auto& cr : cols) {
			TargetColumn c;
			c.name = text(cr, "name");
			c.type = target_type(text(cr, "type"), SqlDialect::Sqlite);
			c.nullable = to_count(value(cr, "nn")) == 0;
			if (!is_null(cr, "dflt")) {
				const auto lits = quoted_literals(text(cr, "dflt"));
				if (!lits.empty()) c.default_literal = lits.front();
			}
			c.allowed = check_in_values(create, c.name);
			c.identity = autoincrement && pk_columns == 1 && to_count(value(cr, "pk")) == 1 && c.type == TargetType::Integer;
			t.columns.push_back(std::move(c));
		}
		for (const auto& fk : conn.query({"SELECT \"table\" AS parent FROM pragma_foreign_key_list(?)", {t.name}}))
			s.foreign_keys.emplace_back(t.name, text(fk, "parent"));
		s.tables.push_back(std::move(t));
	}
	return s;
}

// ------------------------------------------------------------------ copying

class TableCopier {
public:
	TableCopier(SqlConnection& conn, const TargetTable& target, std::vector<std::string> columns, TableReport& report)
	    : conn_(conn), report_(report), columns_(std::move(columns)) {
		const auto d = conn.dialect();
		prefix_ = "INSERT INTO " + q(target.name, d) + " (";
		for (std::size_t i = 0; i < columns_.size(); ++i) prefix_ += (i ? ", " : "") + q(columns_[i], d);
		prefix_ += ") VALUES ";
		row_marks_ = "(";
		for (std::size_t i = 0; i < columns_.size(); ++i) row_marks_ += i ? ", ?" : "?";
		row_marks_ += ")";
		rows_per_statement_ = std::clamp<std::size_t>(kMaxParamsPerStatement / std::max<std::size_t>(columns_.size(), 1), 1,
		                                              kMaxRowsPerStatement);
	}

	/// Writes `rows` in one transaction; if that fails, row by row so only the bad rows are lost.
	void write(std::vector<std::vector<SqlValue>>& rows) {
		std::vector<Statement> statements;
		for (std::size_t i = 0; i < rows.size(); i += rows_per_statement_) {
			const auto end = std::min(rows.size(), i + rows_per_statement_);
			statements.push_back(statement(rows, i, end));
		}
		try {
			conn_.execute({"BEGIN", {}});
			for (const auto& st : statements) conn_.execute(st);
			conn_.execute({"COMMIT", {}});
			return;
		} catch (const RepositoryError&) {
			rollback();
		}
		for (std::size_t i = 0; i < rows.size(); ++i) {
			try {
				conn_.execute(statement(rows, i, i + 1));
			} catch (const RepositoryError& e) {
				++report_.failed_rows;
				if (report_.errors.size() < kMaxErrors) report_.errors.emplace_back(e.what());
			}
		}
	}

private:
	Statement statement(std::vector<std::vector<SqlValue>>& rows, std::size_t begin, std::size_t end) const {
		Statement st;
		st.sql = prefix_;
		st.params.reserve((end - begin) * columns_.size());
		for (std::size_t r = begin; r < end; ++r) {
			st.sql += (r == begin ? "" : ", ") + row_marks_;
			for (const auto& v : rows[r]) st.params.push_back(v);
		}
		return st;
	}

	void rollback() {
		try {
			conn_.execute({"ROLLBACK", {}});
		} catch (const RepositoryError&) {
			// no transaction was open (BEGIN itself failed)
		}
	}

	SqlConnection& conn_;
	TableReport& report_;
	std::vector<std::string> columns_;
	std::string prefix_;
	std::string row_marks_;
	std::size_t rows_per_statement_ = 1;
};

/// Moves the target's id counter to `next` (the id the source would have handed out next).
void reset_identity(SqlConnection& conn, const TargetTable& table, const TargetColumn& column, std::uint64_t next) {
	const auto d = conn.dialect();
	if (next < 1) return;
	if (d == SqlDialect::Postgres) {
		conn.query({"SELECT setval(pg_get_serial_sequence(?, ?), ?, false)",
		            {q(table.name, d), column.name, next}});
		return;
	}
	const std::uint64_t seq = next - 1;
	conn.execute({"UPDATE sqlite_sequence SET seq = ? WHERE name = ?", {seq, table.name}});
	if (conn.affected_rows() == 0) conn.execute({"INSERT INTO sqlite_sequence (name, seq) VALUES (?, ?)", {table.name, seq}});
}

std::optional<std::string> read_file(const std::filesystem::path& p) {
	std::ifstream in(p, std::ios::binary);
	if (!in) return std::nullopt;
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} // namespace

// ------------------------------------------------------------------ pure parts

const TargetColumn* TargetTable::find(std::string_view column) const noexcept {
	for (const auto& c : columns)
		if (iequals(c.name, column)) return &c;
	return nullptr;
}

const TargetTable* TargetSchema::find(std::string_view table) const noexcept {
	for (const auto& t : tables)
		if (iequals(t.name, table)) return &t;
	return nullptr;
}

TargetType target_type(std::string_view declared, SqlDialect dialect) {
	const std::string t = lower(declared);
	if (dialect == SqlDialect::Postgres) {
		if (t == "smallint" || t == "integer" || t == "bigint" || t == "boolean") return TargetType::Integer;
		if (t == "real" || t == "double precision" || t == "numeric") return TargetType::Real;
		if (t == "bytea") return TargetType::Blob;
		if (t.starts_with("timestamp")) return TargetType::Timestamp;
		if (t == "date") return TargetType::Date;
		return TargetType::Text; // character varying, text, USER-DEFINED (citext), ...
	}
	// SQLite type affinity rules (datatype3.html §3.1); dates are TEXT there.
	if (t.find("int") != std::string::npos) return TargetType::Integer;
	if (t.find("char") != std::string::npos || t.find("clob") != std::string::npos || t.find("text") != std::string::npos)
		return TargetType::Text;
	if (t.find("blob") != std::string::npos) return TargetType::Blob;
	if (t.find("real") != std::string::npos || t.find("floa") != std::string::npos || t.find("doub") != std::string::npos)
		return TargetType::Real;
	return TargetType::Text;
}

std::vector<std::string> quoted_literals(std::string_view sql) {
	std::vector<std::string> out;
	for (std::size_t i = 0; i < sql.size(); ++i) {
		if (sql[i] == '"') { // skip identifiers
			const auto end = sql.find('"', i + 1);
			if (end == std::string_view::npos) break;
			i = end;
			continue;
		}
		if (sql[i] != '\'') continue;
		std::string lit;
		std::size_t j = i + 1;
		for (; j < sql.size(); ++j) {
			if (sql[j] == '\'') {
				if (j + 1 < sql.size() && sql[j + 1] == '\'') {
					lit += '\'';
					++j;
					continue;
				}
				break;
			}
			lit += sql[j];
		}
		out.push_back(std::move(lit));
		i = j;
	}
	return out;
}

std::vector<std::string> check_in_values(std::string_view create_sql, std::string_view column) {
	const std::string lc = lower(create_sql);
	const std::string needle = "\"" + lower(column) + "\" in (";
	for (auto pos = lc.find("check"); pos != std::string::npos; pos = lc.find("check", pos + 1)) {
		const auto open = lc.find('(', pos);
		if (open == std::string::npos) break;
		std::size_t k = open + 1;
		while (k < lc.size() && lc[k] == ' ') ++k;
		if (lc.compare(k, needle.size(), needle) != 0) continue;
		// Up to the parenthesis closing the IN list, skipping parentheses inside literals.
		const auto list = k + needle.size();
		std::size_t end = list;
		bool in_lit = false;
		for (; end < create_sql.size(); ++end) {
			if (create_sql[end] == '\'') in_lit = !in_lit;
			else if (!in_lit && create_sql[end] == ')') break;
		}
		return quoted_literals(create_sql.substr(list, end - list));
	}
	return {};
}

std::set<std::string> schema_file_tables(std::string_view schema_sql) {
	std::set<std::string> out;
	constexpr std::string_view kCreate = "CREATE TABLE IF NOT EXISTS \"";
	for (auto pos = schema_sql.find(kCreate); pos != std::string_view::npos; pos = schema_sql.find(kCreate, pos + 1)) {
		const auto start = pos + kCreate.size();
		const auto end = schema_sql.find('"', start);
		if (end == std::string_view::npos) break;
		out.emplace(schema_sql.substr(start, end - start));
	}
	return out;
}

std::map<std::string, std::uint64_t> schema_file_seed_rows(std::string_view schema_sql) {
	std::map<std::string, std::uint64_t> out;
	std::size_t line_start = 0;
	while (line_start < schema_sql.size()) {
		auto line_end = schema_sql.find('\n', line_start);
		if (line_end == std::string_view::npos) line_end = schema_sql.size();
		const auto line = schema_sql.substr(line_start, line_end - line_start);
		line_start = line_end + 1;
		if (!line.starts_with("INSERT")) continue;
		const auto into = line.find("INTO \"");
		if (into == std::string_view::npos) continue; // e.g. INSERT INTO sqlite_sequence
		const auto start = into + 6;
		const auto end = line.find('"', start);
		if (end == std::string_view::npos) continue;
		++out[std::string(line.substr(start, end - start))];
	}
	return out;
}

bool is_zero_date(std::string_view v) noexcept {
	// YYYY-MM-DD[ HH:MM:SS]: any of year, month or day all zeros.
	if (v.size() < 10 || v[4] != '-' || v[7] != '-') return false;
	const auto zeros = [](std::string_view s) { return std::all_of(s.begin(), s.end(), [](char c) { return c == '0'; }); };
	return zeros(v.substr(0, 4)) || zeros(v.substr(5, 2)) || zeros(v.substr(8, 2));
}

ColumnPlan plan_columns(const SourceTable& source, const TargetTable& target) {
	ColumnPlan plan;
	for (std::size_t t = 0; t < target.columns.size(); ++t) {
		const auto it = std::find_if(source.columns.begin(), source.columns.end(),
		                             [&](const SourceColumn& s) { return iequals(s.name, target.columns[t].name); });
		if (it == source.columns.end()) plan.target_only.push_back(target.columns[t].name);
		else plan.copy.emplace_back(static_cast<std::size_t>(it - source.columns.begin()), t);
	}
	for (const auto& s : source.columns)
		if (target.find(s.name) == nullptr) plan.source_only.push_back(s.name);
	return plan;
}

std::vector<std::string> copy_order(std::vector<std::string> tables,
                                    const std::vector<std::pair<std::string, std::string>>& foreign_keys) {
	std::sort(tables.begin(), tables.end());
	std::vector<std::string> out;
	std::set<std::string> placed;
	auto ready = [&](const std::string& t) {
		for (const auto& [child, parent] : foreign_keys) {
			if (!iequals(child, t) || iequals(parent, t)) continue;
			const bool parent_pending = std::any_of(tables.begin(), tables.end(), [&](const std::string& o) {
				return iequals(o, parent) && !placed.contains(o);
			});
			if (parent_pending) return false;
		}
		return true;
	};
	while (out.size() < tables.size()) {
		bool progressed = false;
		for (const auto& t : tables) {
			if (placed.contains(t) || !ready(t)) continue;
			out.push_back(t);
			placed.insert(t);
			progressed = true;
		}
		if (!progressed) { // cycle: keep the rest alphabetical
			for (const auto& t : tables)
				if (!placed.contains(t)) {
					out.push_back(t);
					placed.insert(t);
				}
		}
	}
	return out;
}

Converted convert_value(const SourceColumn& source, const TargetColumn& target, const RawValue& raw) {
	if (!raw) {
		if (target.nullable) return {std::monostate{}, {}};
		return {zero_value(target.type), "NULL in a NOT NULL column -> zero value"};
	}
	std::string v = *raw;
	std::string adjustment;

	if (is_date_type(source.data_type) && is_zero_date(v)) {
		if (target.nullable) return {std::monostate{}, "zero date -> NULL"};
		const bool date_only = source.data_type == "date" || target.type == TargetType::Date;
		v = date_only ? "1970-01-01" : "1970-01-01 00:00:00";
		adjustment = "zero date -> " + v;
	}

	switch (target.type) {
	case TargetType::Integer: {
		std::int64_t n = 0;
		const auto* end = v.data() + v.size();
		const auto r = std::from_chars(v.data(), end, n);
		if (r.ec != std::errc{} || r.ptr != end) {
			if (target.nullable) return {std::monostate{}, "'" + printable(v) + "' is not an integer -> NULL"};
			return {std::int64_t{0}, "'" + printable(v) + "' is not an integer -> 0"};
		}
		return {n, adjustment};
	}
	case TargetType::Blob: return {Blob{std::move(v)}, adjustment};
	case TargetType::Real:
	case TargetType::Timestamp:
	case TargetType::Date: return {std::move(v), adjustment};
	case TargetType::Text: break;
	}

	if (const auto nul = v.find('\0'); nul != std::string::npos) {
		v.resize(nul);
		adjustment = "cut at a NUL byte";
	}
	if (!target.allowed.empty() && std::find(target.allowed.begin(), target.allowed.end(), v) == target.allowed.end()) {
		const std::string was = printable(v);
		if (target.default_literal &&
		    std::find(target.allowed.begin(), target.allowed.end(), *target.default_literal) != target.allowed.end()) {
			v = *target.default_literal;
		} else if (target.nullable) {
			return {std::monostate{}, "'" + was + "' not allowed -> NULL"};
		} else {
			v = target.allowed.front();
		}
		std::string what = "'" + was + "' not allowed -> '" + printable(v) + "'";
		return {std::move(v), std::move(what)};
	}
	if (target.max_length > 0 && v.size() > target.max_length) {
		v.resize(target.max_length);
		adjustment = "longer than " + std::to_string(target.max_length) + " bytes -> cut";
	}
	return {std::move(v), adjustment};
}

TargetSchema inspect_target(SqlConnection& conn) {
	TargetSchema s = conn.dialect() == SqlDialect::Postgres ? inspect_postgres(conn) : inspect_sqlite(conn);
	for (auto& t : s.tables) t.rows = count_rows(conn, t.name);
	return s;
}

bool ImportReport::ok() const noexcept {
	if (refused) return false;
	return std::all_of(tables.begin(), tables.end(), [](const TableReport& t) {
		return t.status != TableReport::Status::Mismatch && t.failed_rows == 0;
	});
}

std::uint64_t ImportReport::rows_copied() const noexcept {
	std::uint64_t n = 0;
	for (const auto& t : tables)
		if (t.status == TableReport::Status::Ok || t.status == TableReport::Status::Mismatch) n += t.target_rows;
	return n;
}

// ------------------------------------------------------------------ run_import

ImportReport run_import(LegacySource& source, SqlConnection& target, const ImportOptions& options, std::ostream& progress) {
	ImportReport report;
	auto refuse = [&](std::string why) {
		report.refused = true;
		report.refusal = std::move(why);
		return report;
	};
	const auto d = target.dialect();

	// 1. The source must be a known legacy schema.
	const auto source_tables = source.tables();
	SchemaSnapshot snap;
	bool has_log_tables = false;
	for (const auto& t : source_tables) {
		if (excluded_from_fingerprint(t.name)) {
			const auto logs = log_tables();
			has_log_tables = has_log_tables || std::find(logs.begin(), logs.end(), lower(t.name)) != logs.end();
			continue;
		}
		TableSchema ts{t.name, {}};
		for (const auto& c : t.columns) ts.columns.push_back({c.name, c.column_type, c.nullable});
		snap.tables.push_back(std::move(ts));
	}
	if (snap.tables.empty()) {
		if (!has_log_tables) return refuse("the source holds no rAthena tables");
		report.fingerprint = "(log tables only)";
	} else {
		report.fingerprint = schema_fingerprint(snap);
		if (!options.known_baselines.contains(report.fingerprint))
			return refuse("unknown source schema " + report.fingerprint +
			              ": bring the database to the current rAthena schema with sql-files/upgrades/ first, or, "
			              "after checking it, accept it with --baseline=" + report.fingerprint);
	}
	progress << "source schema " << report.fingerprint << "\n";

	// 2. The target: empty (create the schema) or holding the rAthena schema.
	const std::string dialect_dir = d == SqlDialect::Postgres ? "postgresql" : "sqlite";
	std::vector<std::string> scripts;
	for (const auto f : kSchemaFiles) {
		const auto path = options.schema_dir / dialect_dir / f;
		auto content = read_file(path);
		if (!content)
			return refuse("schema file " + path.string() + " not found (run from the rAthena directory or pass --schema-dir)");
		scripts.push_back(std::move(*content));
	}
	TargetSchema tschema = inspect_target(target);
	if (d == SqlDialect::Postgres && tschema.encoding != "SQL_ASCII")
		return refuse("the target database has encoding " + tschema.encoding +
		              "; rAthena needs CREATE DATABASE ... ENCODING 'SQL_ASCII' LC_COLLATE 'C' LC_CTYPE 'C' "
		              "TEMPLATE template0 so client text is stored byte for byte");
	if (tschema.tables.empty()) {
		progress << "creating the " << to_string(d) << " schema from " << (options.schema_dir / dialect_dir).string() << "\n";
		for (const auto& sql : scripts) target.execute_script(sql);
		report.created_schema = true;
		tschema = inspect_target(target);
	} else {
		std::string missing;
		for (const auto& t : schema_file_tables(scripts.front()))
			if (tschema.find(t) == nullptr) missing += (missing.empty() ? "" : ", ") + t;
		if (!missing.empty())
			return refuse("the target is neither empty nor an rAthena database (missing tables: " + missing + ")");
	}

	// 3. Tables in both; every one of them must still be empty (seeded defaults aside).
	std::vector<std::string> common;
	for (const auto& s : source_tables) {
		if (tschema.find(s.name) == nullptr) report.source_only.push_back(s.name);
		else common.push_back(s.name);
	}
	for (const auto& t : tschema.tables)
		if (std::none_of(source_tables.begin(), source_tables.end(), [&](const SourceTable& s) { return iequals(s.name, t.name); }))
			report.target_only.push_back(t.name);
	std::sort(report.source_only.begin(), report.source_only.end());
	std::sort(report.target_only.begin(), report.target_only.end());
	std::map<std::string, std::uint64_t> seed_rows;
	for (const auto& sql : scripts)
		for (const auto& [t, n] : schema_file_seed_rows(sql)) seed_rows[lower(t)] += n;
	auto seeded = [&](const TargetTable& t) {
		const auto it = seed_rows.find(lower(t.name));
		return it != seed_rows.end() && t.rows <= it->second;
	};
	std::string not_empty;
	for (const auto& name : common) {
		const auto* t = tschema.find(name);
		if (t->rows > 0 && !seeded(*t)) not_empty += (not_empty.empty() ? "" : ", ") + t->name;
	}
	if (!not_empty.empty())
		return refuse("target tables already hold rows (" + not_empty + "); import into an empty database");

	// 4. Copy, parents before children.
	for (const auto& name : copy_order(common, tschema.foreign_keys)) {
		const auto& src = *std::find_if(source_tables.begin(), source_tables.end(), [&](const SourceTable& s) { return s.name == name; });
		const auto& dst = *tschema.find(name);
		TableReport tr;
		tr.table = dst.name;
		const auto plan = plan_columns(src, dst);
		for (const auto& c : plan.source_only) tr.notes.push_back("column " + c + " is not in the target: dropped");
		for (const auto& c : plan.target_only) tr.notes.push_back("column " + c + " is not in the source: target default");
		tr.source_rows = source.row_count(src);
		if (plan.copy.empty()) {
			tr.status = TableReport::Status::NoCommonColumns;
			tr.target_rows = count_rows(target, dst.name);
			report.tables.push_back(std::move(tr));
			continue;
		}
		if (dst.rows > 0) { // only default rows (checked above)
			if (tr.source_rows == 0) {
				tr.status = TableReport::Status::KeptDefaults;
				tr.target_rows = dst.rows;
				report.tables.push_back(std::move(tr));
				continue;
			}
			target.execute({"DELETE FROM " + q(dst.name, d), {}}); // the source's rows replace the defaults
		}

		if (tr.source_rows > 0) progress << "copying " << dst.name << " (" << tr.source_rows << " rows)\n" << std::flush;
		std::vector<const SourceColumn*> read_cols;
		std::vector<std::string> write_cols;
		for (const auto& [si, ti] : plan.copy) {
			read_cols.push_back(&src.columns[si]);
			write_cols.push_back(dst.columns[ti].name);
		}
		TableCopier copier(target, dst, write_cols, tr);
		if (tr.source_rows > 0) {
			source.read_rows(src, read_cols, options.utf8_as_latin1, std::max<std::size_t>(options.batch_size, 1),
			                 [&](std::vector<RawRow>& rows) {
				                 std::vector<std::vector<SqlValue>> values;
				                 values.reserve(rows.size());
				                 for (auto& raw : rows) {
					                 std::vector<SqlValue> row;
					                 row.reserve(plan.copy.size());
					                 for (std::size_t i = 0; i < plan.copy.size(); ++i) {
						                 auto conv = convert_value(src.columns[plan.copy[i].first],
						                                           dst.columns[plan.copy[i].second], raw[i]);
						                 if (!conv.adjustment.empty())
							                 ++tr.adjustments[dst.columns[plan.copy[i].second].name + ": " + conv.adjustment];
						                 row.push_back(std::move(conv.value));
					                 }
					                 values.push_back(std::move(row));
				                 }
				                 copier.write(values);
			                 });
		}

		// 5. Identity counters continue where the source stood.
		for (const auto& [si, ti] : plan.copy) {
			const auto& tc = dst.columns[ti];
			if (!tc.identity) continue;
			std::uint64_t next = src.columns[si].auto_increment ? src.auto_increment : 0;
			const auto mx = target.query({"SELECT MAX(" + q(tc.name, d) + ") AS m FROM " + q(dst.name, d), {}});
			if (!mx.empty() && !is_null(mx.front(), "m")) next = std::max(next, to_count(value(mx.front(), "m")) + 1);
			reset_identity(target, dst, tc, next);
		}

		// 6. Verify.
		tr.target_rows = count_rows(target, dst.name);
		tr.status = tr.target_rows == tr.source_rows ? TableReport::Status::Ok : TableReport::Status::Mismatch;
		report.tables.push_back(std::move(tr));
	}
	return report;
}

void print_summary(const ImportReport& report, std::ostream& out) {
	if (report.refused) {
		out << "import refused: " << report.refusal << "\n";
		return;
	}
	out << "\n" << std::left << std::setw(28) << "table" << std::right << std::setw(12) << "source" << std::setw(12)
	    << "target" << "  status\n";
	std::size_t adjusted = 0;
	for (const auto& t : report.tables) {
		std::string status;
		switch (t.status) {
		case TableReport::Status::Ok: status = "ok"; break;
		case TableReport::Status::Mismatch: status = "MISMATCH"; break;
		case TableReport::Status::KeptDefaults: status = "kept default rows (source empty)"; break;
		case TableReport::Status::NoCommonColumns: status = "skipped: no common columns"; break;
		}
		if (t.failed_rows > 0) status += ", " + std::to_string(t.failed_rows) + " row(s) FAILED";
		out << std::left << std::setw(28) << t.table << std::right << std::setw(12) << t.source_rows << std::setw(12)
		    << t.target_rows << "  " << status << "\n";
		adjusted += t.adjustments.size() + t.notes.size() + t.errors.size();
	}
	if (adjusted > 0) {
		out << "\nadjustments and notes:\n";
		for (const auto& t : report.tables) {
			for (const auto& n : t.notes) out << "  " << t.table << ": " << n << "\n";
			for (const auto& [what, n] : t.adjustments) out << "  " << t.table << "." << what << " (" << n << " value(s))\n";
			for (const auto& e : t.errors) out << "  " << t.table << ": row failed: " << e << "\n";
		}
	}
	if (!report.source_only.empty()) {
		out << "\nskipped, not in the target schema:";
		for (const auto& t : report.source_only) out << " " << t;
		out << "\n";
	}
	if (!report.target_only.empty()) {
		out << "not in the source (left as created):";
		for (const auto& t : report.target_only) out << " " << t;
		out << "\n";
	}
	out << "\n" << (report.ok() ? "import OK: " : "import FAILED: ") << report.tables.size() << " tables, "
	    << report.rows_copied() << " rows" << (report.created_schema ? " (schema created)" : "") << "\n";
	if (!report.ok())
		out << "the target now holds a partial copy; recreate it empty before running the import again\n";
}

} // namespace rathena::persistence::migrate
