#!/usr/bin/env bash
# Checks a database produced by `rathena-migrate import` from sql-files/main.sql + logs.sql +
# import_seed.sql (CI job `importer` in .github/workflows/build_reimagined_units.yml).
#
#   check_import.sh postgresql          # connection from the standard PG* environment variables
#   check_import.sh sqlite <file.db>
set -euo pipefail

kind=${1:?usage: check_import.sh postgresql|sqlite [file.db]}
failures=0

q() {
	if [[ $kind == postgresql ]]; then
		psql -X -A -t -v ON_ERROR_STOP=1 -c "$1"
	else
		sqlite3 -batch "$db" "$1"
	fi
}

hex() { # column expression -> lower-case hex of its bytes
	if [[ $kind == postgresql ]]; then echo "encode(convert_to($1, 'SQL_ASCII'), 'hex')"; else echo "lower(hex($1))"; fi
}

expect() { # description, query, expected
	local got
	got=$(q "$2")
	if [[ $got == "$3" ]]; then
		echo "ok    $1"
	else
		echo "FAIL  $1: expected '$3', got '$got'"
		failures=$((failures + 1))
	fi
}

if [[ $kind == sqlite ]]; then
	db=${2:?sqlite needs the database file}
fi

expect "login rows" 'SELECT COUNT(*) FROM "login"' 4
expect "char rows" 'SELECT COUNT(*) FROM "char"' 2
expect "clan defaults copied from the source" 'SELECT COUNT(*) FROM "clan"' 4
expect "log table rows" 'SELECT COUNT(*) FROM "zenylog"' 1
expect "EUC-KR userid is byte exact" "SELECT $(hex '"userid"') FROM \"login\" WHERE \"account_id\" = 2000001" c7d1b1db
expect "EUC-KR char name is byte exact" "SELECT $(hex '"name"') FROM \"char\" WHERE \"char_id\" = 150001" c7d1b1dbb0fa
expect "CP1252 char name is byte exact" "SELECT $(hex '"name"') FROM \"char\" WHERE \"char_id\" = 150000" 4a6f73e9
expect "latin1 mail text is byte exact" "SELECT $(hex '"message"') FROM \"mail\" WHERE \"id\" = 1" e9e8
if [[ $kind == postgresql ]]; then
	expect "guild emblem blob" 'SELECT encode("emblem_data", '"'hex'"') FROM "guild" WHERE "guild_id" = 11' 00010280ff
else
	expect "guild emblem blob" 'SELECT lower(hex("emblem_data")) FROM "guild" WHERE "guild_id" = 11' 00010280ff
fi
expect "zero datetime -> NULL" 'SELECT COUNT(*) FROM "login" WHERE "account_id" = 2000000 AND "lastlogin" IS NULL AND "birthdate" IS NULL' 1
expect "zero datetime in NOT NULL column -> epoch" 'SELECT "time" FROM "zenylog"' "1970-01-01 00:00:00"
expect "invalid enum '' -> column default" 'SELECT "sex" FROM "login" WHERE "account_id" = 2000002' M
expect "case-insensitive name lookup" "SELECT \"account_id\" FROM \"login\" WHERE \"userid\" = 'HERO'" 2000000

# New ids continue where the source's AUTO_INCREMENT stood (150050), not at max + 1.
q 'INSERT INTO "char" ("account_id", "name") VALUES (2000000, '"'newbie'"')' >/dev/null
expect "char id counter continues from AUTO_INCREMENT" "SELECT \"char_id\" FROM \"char\" WHERE \"name\" = 'newbie'" 150050

if ((failures > 0)); then
	echo "$failures check(s) failed"
	exit 1
fi
echo "all checks passed"
