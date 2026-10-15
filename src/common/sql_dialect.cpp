// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "sql_dialect.hpp"

#include <cctype>
#include <regex>

namespace rathena::sqldialect {

namespace {

constexpr char kMark = '\x01'; // stands in for a masked string literal

/// Replaces every '...' literal by kMark<index>kMark so the remaining text can be pattern-matched
/// safely, converting `backticks` to "double quotes" on the way.
struct Masked {
	std::string text;
	std::vector<std::string> literals;
};

Masked mask(std::string_view sql) {
	Masked m;
	m.text.reserve(sql.size());
	size_t i = 0;
	while (i < sql.size()) {
		const char c = sql[i];
		if (c == '\'') {
			size_t j = i + 1;
			for (; j < sql.size(); ++j) {
				if (sql[j] == '\'') {
					if (j + 1 < sql.size() && sql[j + 1] == '\'') {
						++j; // '' escape
						continue;
					}
					break;
				}
			}
			const size_t end = j < sql.size() ? j + 1 : sql.size();
			m.text += kMark;
			m.text += std::to_string(m.literals.size());
			m.text += kMark;
			m.literals.emplace_back(sql.substr(i, end - i));
			i = end;
		} else if (c == '"') {
			const size_t j = sql.find('"', i + 1);
			const size_t end = j == std::string_view::npos ? sql.size() : j + 1;
			m.text.append(sql.substr(i, end - i));
			i = end;
		} else if (c == '`') {
			m.text += '"';
			++i;
		} else {
			m.text += c;
			++i;
		}
	}
	return m;
}

std::string unmask(const std::string& text, const std::vector<std::string>& literals) {
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == kMark) {
			const size_t end = text.find(kMark, i + 1);
			out += literals[std::stoul(text.substr(i + 1, end - i - 1))];
			i = end;
		} else {
			out += text[i];
		}
	}
	return out;
}

std::string unquote(std::string s) {
	if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
	return s;
}

std::vector<std::string> split_columns(const std::string& list) {
	std::vector<std::string> cols;
	std::string cur;
	for (char ch : list) {
		if (ch == ',') {
			cols.push_back(cur);
			cur.clear();
		} else {
			cur += ch;
		}
	}
	cols.push_back(cur);
	for (auto& c : cols) {
		const auto b = c.find_first_not_of(" \t\r\n");
		const auto e = c.find_last_not_of(" \t\r\n");
		c = b == std::string::npos ? std::string{} : unquote(c.substr(b, e - b + 1));
	}
	return cols;
}

const std::regex kLimitComma(R"(\bLIMIT\s+(\d+)\s*,\s*(\d+))", std::regex::icase);
// BINARY followed by a (possibly qualified, possibly quoted) column reference.
const std::regex kBinary(R"(\bBINARY\s+((?:"[^"]+"|[A-Za-z_][A-Za-z0-9_]*)(?:\s*\.\s*(?:"[^"]+"|[A-Za-z_][A-Za-z0-9_]*))?))",
                         std::regex::icase);
const std::regex kStartTransaction(R"(^\s*START\s+TRANSACTION\s*;?\s*$)", std::regex::icase);
const std::regex kReplace(R"(^\s*REPLACE\s+INTO\s+("[^"]+"|[A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(VALUES[\s\S]*?)\s*;?\s*$)",
                          std::regex::icase);
const std::regex kInsertTarget(R"(^\s*INSERT\s+(?:OR\s+\w+\s+)?INTO\s+("[^"]+"|`[^`]+`|[A-Za-z_][A-Za-z0-9_]*))",
                               std::regex::icase);

} // namespace

Translation translate(std::string_view mysql_sql, Dialect dialect, const PrimaryKeyLookup& pk) {
	Masked m = mask(mysql_sql);
	std::string t = std::regex_replace(m.text, kLimitComma, "LIMIT $2 OFFSET $1");

	if (dialect == Dialect::Postgres) {
		t = std::regex_replace(t, kBinary, "CAST($1 AS text)");
	} else {
		t = std::regex_replace(t, kBinary, "$1 COLLATE BINARY");
		if (std::regex_match(t, kStartTransaction)) t = "BEGIN IMMEDIATE";
	}

	if (dialect == Dialect::Postgres) {
		std::smatch r;
		if (std::regex_match(t, r, kReplace)) {
			const std::string table = unquote(r[1].str());
			const auto cols = split_columns(r[2].str());
			const auto keys = pk ? pk(table) : std::nullopt;
			if (!keys || keys->empty())
				return {{}, "REPLACE INTO " + table + ": primary key unknown, cannot translate"};
			std::string conflict, updates;
			for (const auto& k : *keys) conflict += (conflict.empty() ? "" : ", ") + ("\"" + k + "\"");
			for (const auto& c : cols) {
				bool is_key = false;
				for (const auto& k : *keys) is_key = is_key || k == c;
				if (!is_key) updates += (updates.empty() ? "" : ", ") + ("\"" + c + "\" = EXCLUDED.\"" + c + "\"");
			}
			std::string col_list;
			for (const auto& c : cols) col_list += (col_list.empty() ? "" : ", ") + ("\"" + c + "\"");
			t = "INSERT INTO " + r[1].str() + " (" + col_list + ") " + r[3].str() + " ON CONFLICT (" + conflict + ") " +
			    (updates.empty() ? "DO NOTHING" : "DO UPDATE SET " + updates);
		}
	}
	return {unmask(t, m.literals), {}};
}

size_t count_placeholders(std::string_view sql) {
	const Masked m = mask(sql);
	size_t n = 0;
	bool in_ident = false;
	for (char c : m.text) {
		if (c == '"') in_ident = !in_ident;
		else if (c == '?' && !in_ident) ++n;
	}
	return n;
}

std::string number_placeholders(std::string_view sql) {
	Masked m = mask(sql);
	std::string out;
	size_t n = 0;
	bool in_ident = false;
	for (char c : m.text) {
		if (c == '"') in_ident = !in_ident;
		if (c == '?' && !in_ident) out += "$" + std::to_string(++n);
		else out += c;
	}
	return unmask(out, m.literals);
}

std::optional<std::string> insert_target(std::string_view sql) {
	std::string s(sql);
	std::smatch r;
	if (!std::regex_search(s, r, kInsertTarget)) return std::nullopt;
	std::string name = r[1].str();
	if (name.size() >= 2 && (name.front() == '"' || name.front() == '`')) name = name.substr(1, name.size() - 2);
	return name;
}

size_t escape_string(char* out, const char* from, size_t from_len) {
	size_t o = 0;
	for (size_t i = 0; i < from_len && from[i] != '\0'; ++i) {
		if (from[i] == '\'') out[o++] = '\'';
		out[o++] = from[i];
	}
	out[o] = '\0';
	return o;
}

} // namespace rathena::sqldialect
