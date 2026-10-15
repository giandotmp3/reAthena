# Databases

rAthena stores its data in **SQLite** or **PostgreSQL**. MySQL and MariaDB are no longer
supported (see `analysis/rathena/DATABASE_DECISION.md` for why); an existing MySQL database is
moved over once with `rathena-migrate import` (see [Moving from MySQL](#moving-from-mysql)).

| | SQLite | PostgreSQL |
|---|---|---|
| What it is | Embedded engine, compiled into the servers | Database server, 16 or newer |
| Install | Nothing | A PostgreSQL server; libpq to build |
| Data | Files under `save/` (`save/ragnarok.db`) | The server's data directory |
| Good for | Local development, tests, servers on one machine | Production, servers on several machines |

## Choosing the backend

`conf/inter_athena.conf` (override it in `conf/import/inter_conf.txt`):

```
db_backend: sqlite          // or: postgresql
sqlite_directory: save      // SQLite: where the database files go
db_auto_create_schema: yes  // create the tables in an empty database
db_schema_directory: sql-files
```

With SQLite each `*_db` setting (`login_server_db`, `char_server_db`, `map_server_db`,
`web_server_db`, `log_db_db`) names a file: `ragnarok` becomes `save/ragnarok.db`. The host, port,
user and password settings are ignored. All servers on the machine share the same files; SQLite's
write-ahead log lets them read and write at the same time.

With PostgreSQL the `*_ip`, `*_port` (5432), `*_id`, `*_pw` and `*_db` settings are the connection
settings, as they were for MySQL.

On first start, a server that finds an empty database creates the schema from
`sql-files/<backend>/main.sql`, `logs.sql`, `web.sql` and `roulette_default_data.sql`. Servers that
start together wait for each other, so the schema is created once. Set
`db_auto_create_schema: no` to manage the schema yourself.

## Local development (SQLite)

Build and start the servers; nothing else is needed:

```
./configure && make server        # or: cmake -B build && cmake --build build
./login-server & ./char-server & ./map-server &
```

The data is in `save/ragnarok.db`. Inspect it with any SQLite tool, e.g. `sqlite3 save/ragnarok.db`.
Delete the file (with the servers stopped) to start over.

## Production (PostgreSQL)

Create a role and a database. The database **must** use the `SQL_ASCII` encoding and the `C`
locale. The game client sends text in its own code page (EUC-KR, CP1252, GBK, ...) and rAthena
stores those bytes unchanged, as the old MySQL `latin1` tables did; any other encoding would
reject or alter them.

```sql
CREATE ROLE ragnarok LOGIN PASSWORD '<choose one>';
CREATE DATABASE ragnarok OWNER ragnarok
  ENCODING 'SQL_ASCII' LC_COLLATE 'C' LC_CTYPE 'C' TEMPLATE template0;
```

Then, in `conf/import/inter_conf.txt`:

```
db_backend: postgresql
login_server_ip: 127.0.0.1
login_server_id: ragnarok
login_server_pw: <the password>
login_server_db: ragnarok
// ... the same for ipban_db_*, char_server_*, map_server_*, web_server_* and log_db_*
```

The first server to start creates the tables (it needs the `citext` extension, which the database
owner may create; it ships with PostgreSQL). Names (`login.userid`, `char.name`, `guild.name`,
`party.name`) compare case-insensitively, as they did on MySQL.

Building with PostgreSQL support needs libpq (`libpq-dev` on Debian/Ubuntu, `libpq` on Homebrew).
`./configure` and CMake use it when they find it; `--with-postgresql=yes` / `-DWITH_POSTGRESQL=ON`
make it required, `no` / `OFF` build SQLite only. The Visual Studio solution builds SQLite only;
use CMake on Windows for PostgreSQL.

To try PostgreSQL without installing it, `tools/docker/docker-compose.yml` runs one.

## Moving from MySQL

1. Bring the MySQL database up to the current schema with the scripts in `sql-files/upgrades/`
   (as before). Back it up.
2. Create the target: an empty PostgreSQL database as above, or nothing for SQLite.
3. Copy the data:
   ```
   rathena-migrate import --from mysql://user:password@host:3306/ragnarok \
                          --to postgresql://ragnarok:password@127.0.0.1:5432/ragnarok
   # or --to sqlite:save/ragnarok.db
   ```
   The tool creates the schema, copies every table byte for byte, resets the id counters and
   checks the row counts. Tables that no longer exist (the `item_db*`, `mob_db*` and
   `mob_skill_db*` tables of `use_sql_db`) are skipped.
4. Point `conf/import/inter_conf.txt` at the new database and start the servers.

`rathena-migrate` is built from `modernized/rathena-reimagined` and is the only part of rAthena
that still talks to MySQL; it needs a MySQL or MariaDB client library to build its importer.

## Writing queries

Server code and NPC scripts (`query_sql`) keep writing the SQL they always have. A translation
layer (`src/common/sql_dialect.cpp`) adapts it per engine: backtick identifiers, `LIMIT a,b`,
`BINARY` comparisons, `START TRANSACTION` and `REPLACE INTO` (on PostgreSQL an upsert on the
primary key). `NOW()`, `UNIX_TIMESTAMP()` and `FROM_UNIXTIME()` work on both engines.

What does not work and must be written portably:

- `UPDATE ... LIMIT n` / `DELETE ... LIMIT n`: filter on the key, or use
  `WHERE id IN (SELECT id ... LIMIT n)`.
- `INTERVAL` arithmetic, `DATE_ADD`, `DATE_SUB`: compute the cut-off time in C++
  (`Sql_DateTime( time( nullptr ) - seconds )`).
- `INSERT IGNORE` and `ON DUPLICATE KEY UPDATE`: check for the row first, or use `REPLACE INTO`
  with a column list when replacing the whole row is what you want.
- `IF()` and `IFNULL()`: use `CASE WHEN ... END` and `COALESCE()`, which both engines have.
- `REPLACE INTO` without a column list.

`tools/ci/smoke.sh` runs a full login/char/map session on either engine and fails on any SQL error
in the server logs; `sql-test` (`cmake -DBUILD_TESTING=ON`, then `ctest -R sql_test`) checks the
Sql/SqlStmt behaviour on SQLite and, with `RATHENA_TEST_PG=host:port:user:password:db`, on
PostgreSQL.

## Changing the schema

Edit the MySQL-syntax source files `sql-files/main.sql`, `logs.sql`, `web.sql` or
`roulette_default_data.sql`, then regenerate the engine schemas:

```
python3 tools/sql/mysql2portable.py          # writes sql-files/postgresql/ and sql-files/sqlite/
python3 tools/sql/mysql2portable.py --check  # CI: fails when they are out of date
```

Existing databases do not pick up schema changes automatically; ship an `ALTER TABLE` for each
engine with the change.
