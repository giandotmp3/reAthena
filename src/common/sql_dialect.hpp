// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef SQL_DIALECT_HPP
#define SQL_DIALECT_HPP

// Translates the MySQL-flavoured SQL that the servers (and NPC scripts through query_sql) write
// into SQL that SQLite and PostgreSQL accept. rAthena no longer runs on MySQL
// (analysis/rathena/DATABASE_DECISION.md); this shim keeps the ~350 existing queries and custom
// scripts working without rewriting each one.
//
// Handled, always outside string literals:
//   `ident`                      -> "ident"
//   LIMIT a, b                   -> LIMIT b OFFSET a
//   BINARY expr                  -> CAST(expr AS text)          (PostgreSQL)
//                                   expr COLLATE BINARY         (SQLite)
//   START TRANSACTION            -> BEGIN IMMEDIATE             (SQLite)
//   REPLACE INTO t (cols) ...    -> INSERT ... ON CONFLICT (pk) DO UPDATE ...   (PostgreSQL;
//                                   SQLite supports REPLACE natively)
// String literals use standard SQL rules ('' escapes a quote); backslashes are literal.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rathena::sqldialect {

enum class Dialect { Sqlite, Postgres };

/// Returns the primary-key columns of a table (unquoted), or nullopt if unknown.
using PrimaryKeyLookup = std::function<std::optional<std::vector<std::string>>(const std::string& table)>;

struct Translation {
	std::string sql;
	std::string error; ///< non-empty when the statement cannot be translated
};

/// Translates one statement for `dialect`. `pk` is consulted only for REPLACE INTO on PostgreSQL.
Translation translate(std::string_view mysql_sql, Dialect dialect, const PrimaryKeyLookup& pk = {});

/// Number of '?' parameter markers outside string literals and quoted identifiers.
size_t count_placeholders(std::string_view sql);

/// Rewrites '?' markers as $1, $2, ... (PostgreSQL), outside literals.
std::string number_placeholders(std::string_view sql);

/// If the statement is an INSERT, returns its target table name (unquoted).
std::optional<std::string> insert_target(std::string_view sql);

/// Doubles single quotes (standard SQL). Writes at most 2*len bytes plus a terminator and
/// returns the escaped length. NUL bytes end the input (text columns cannot store them).
size_t escape_string(char* out, const char* from, size_t from_len);

} // namespace rathena::sqldialect

#endif /* SQL_DIALECT_HPP */
