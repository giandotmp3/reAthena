# ADR: Replace MySQL/MariaDB with PostgreSQL (servers) and SQLite (embedded)

**Status:** Accepted, 2026-10-15. Requested by the user ("move away from MariaDB/MySQL 8 into a better persistent database solution… and for local development that does not require a live server"). This amends `REIMAGINED_ARCHITECTURE.md` §4 (database row) and §5 (data migration).

**Amended 2026-10-15: MySQL fully retired.** The user then asked to "do whatever is needed to fully retire MySQL". The staged cutover below (stages A-C, MySQL as a transition backend) is superseded by [Amendment: full retirement](#amendment-full-retirement); the rest of the decision stands.

## Decision

| Use | Engine | Why |
|---|---|---|
| **Production servers** (any deployment, including multi-host and map-sharded) | **PostgreSQL 16+** | Transactional DDL, real FKs/CHECK/partial indexes, MVCC concurrency, JSONB, partitioning for the append-only log tables, a mature migration path from MySQL. |
| **Local development, tests, CI, single-host small servers** | **SQLite 3.45+**, vendored as the amalgamation and built in | No server process, no install on any OS: one file, or `:memory:` for tests. Transactional DDL, FKs, `STRICT` tables, WAL mode for several processes on one host. |
| ~~Transition only~~ **Retired** | MySQL/MariaDB | No server runs on it. Its only remaining use is as the *source* of the one-time `rathena-migrate import`. |

All three sit behind the existing repository interfaces in `persistence/`. Rules and services never see which engine is underneath. One portable SQL subset is used (below), and only DDL and a few functions differ per backend.

## Why MySQL/MariaDB is not the best fit

The faults below come from what we found in this codebase, not general preference:

1. **DDL is not transactional.** A migration step that fails part-way leaves the schema half-changed (`MysqlMigrationTarget::apply` has to say "every step is written to be re-runnable"). PostgreSQL and SQLite roll back DDL.
2. **The legacy schema depends on MyISAM.** Only one foreign key exists, and `ROLLBACK` is silently ignored. The live test `BR_LOGIN_037_WithoutM2LegacyMyIsamDeleteIsNotAtomic` proves a "transactional" delete leaves partial state. InnoDB fixes that, but the schema still carries the MyISAM-era design (no FKs, `0` meaning "none").
3. **Behaviour differs between servers.** MariaDB and MySQL 8 report column types differently (`int(11)` vs `int`); we just had to normalise this for M0. They also differ in `CHECK` enforcement history, JSON types, authentication plugins and default SQL modes.
4. **Every developer and every test needs a running server.** The MySQL tests only run where a server exists: one CI job with a service container. Windows and macOS build a `NotImplemented` stub instead.

## Options considered

| Criterion | MySQL/MariaDB (today) | **PostgreSQL** | **SQLite** | SQLite only (also prod) | libSQL / Turso | CockroachDB / YugabyteDB | Embedded KV (RocksDB/LMDB) | DuckDB |
|---|---|---|---|---|---|---|---|---|
| Needs a server process | yes | yes | **no** | no | optional | yes (cluster) | no | no |
| Multi-host (map shards on several machines) | yes | **yes** | no | **no** | via server mode | yes | no | no |
| Transactional DDL | no | **yes** | **yes** | yes | yes | partial | n/a | yes |
| FKs / CHECK / NULL semantics | InnoDB only; legacy is MyISAM | **full** | full (FK pragma on) | full | full | full | none | full |
| Concurrent writers | good | **excellent (MVCC)** | one writer at a time (WAL) | limiting under load | one writer | excellent | good | analytics-only |
| Zero-install on Windows/macOS/Linux | no | client lib only | **yes (one .c file)** | yes | needs its SDK | no | yes | yes |
| Keeps SQL for operators and tools | yes | yes | yes | yes | yes | yes (PG wire) | **no** | yes |
| Migration path from today's data | — | **mature tools (pgloader); our importer** | our importer | our importer | our importer | via PG | full rewrite | n/a |
| Maturity / C/C++ integration | high | **high (libpq)** | **highest** | highest | young | medium | high | high |
| Verdict | retire | **production** | **embedded/dev/test** | rejected: one host and one writer | rejected: young, extra SDK | rejected: overkill | rejected: loses SQL and rewrites all queries | rejected: OLAP engine |

Choosing PostgreSQL also keeps the door open to PostgreSQL-compatible distributed databases later, without changing code.

## Consequences

### Portable SQL subset (one dialect for both backends)
- Identifiers are double-quoted (`"login"`), never backticks.
- Upserts use `INSERT … ON CONFLICT (…) DO UPDATE` (PostgreSQL 9.5+, SQLite 3.24+). `REPLACE INTO` (24 legacy uses) is not used.
- `RETURNING` replaces `LAST_INSERT_ID()` (PostgreSQL, SQLite 3.35+).
- Times are stored as integer UNIX seconds (the legacy columns already are), so no `NOW()`/`UNIX_TIMESTAMP()`/`FROM_UNIXTIME()` in queries. The application clock supplies times, which also makes tests deterministic.
- Placeholders are written `?` in repository code. The PostgreSQL driver rewrites them to `$1…$n`.
- DDL is generated per backend by the migrator (types, `BIGINT GENERATED … AS IDENTITY` vs `INTEGER PRIMARY KEY`, `STRICT`).
- JSON for the web config tables: `JSONB` on PostgreSQL, `TEXT` with `json_valid` CHECK on SQLite. Merging stays in application code (BR-WEB-009/011).

### Data migration: import into a clean schema instead of altering MyISAM in place
Production cutover becomes **export from MySQL → transform → import into a freshly created, fully constrained schema**. The transform:
- converts `0`-as-none to NULL;
- quarantines orphans in `_orphans_*`;
- moves `user_pass` to `pass_hash`/`pass_scheme`;
- drops `auction` (archived).

Migration steps M2 (to InnoDB), M3a (0 → NULL), M3b (foreign keys) and M4 (type fixes) then never run against MySQL in production. They become part of the import, and the new schema is born with FKs and correct types. The M0 fingerprint check stays: the importer refuses an unknown legacy schema. `rathena-migrate` gains `export`/`import` subcommands, and `pgloader` is an alternative for very large databases.

### ~~Cutover is staged by table ownership~~ (superseded by the amendment below)
| Stage | When | What moves to PostgreSQL | Why it is safe then |
|---|---|---|---|
| A | now | nothing in production. New code is written against the portable subset; every repository test runs on SQLite on all CI platforms; PostgreSQL CI job added. | No behaviour change. |
| B | end of phase 2 (login and web rebuilt) | login-owned tables: `login`, `global_acc_reg_*`, `ipbanlist`, `loginlog` | Only the rebuilt login-server and web-server touch them. The legacy char-server never reads `login` (verified: no `login_table` use in `src/char`). Until then the web-server reads guild and char data from MySQL through read-only repositories. |
| C | M3a gate: no legacy binary writes the database | everything else, through the import | char and map persistence are behind repositories by then. The MySQL backend is retired. |

If the rebuilt login-server ships on MySQL before stage B, M2 must run on the login-owned tables, so BR-LOGIN-037 deletes are atomic. The live test above already proves this ordering.

### Ecosystem impact (needs operator communication)
- **FluxCP and other community tools** query the MySQL schema directly through PHP PDO. They will not work against the new schema without changes. Mitigation: publish the new schema and a compatibility view set; announce at stage C. Keeping a maintained MySQL backend for these users is possible but would mean three backends. Decision deferred to stage C.
- **`query_sql` / `query_logsql` script buildins** pass raw SQL from NPC scripts (8 uses in shipped `npc/custom/*`, plus operator customs). Under K2 they stay supported. Statements are passed to the active backend, so MySQL-only syntax (backticks, `REPLACE INTO`) in custom scripts must be ported. The shipped uses are ported in stage C, and a lint warns on MySQL-isms at script load.
- Log tables grow without bound. On PostgreSQL they are range-partitioned by month (cheap retention). On SQLite they go in a separate database file.

### Engine capacity notes
- SQLite in WAL mode allows one writer at a time across processes. The busy timeout is set by the persistence layer. The single-host profile (all four servers on one machine) fits small private servers, with autosave every 300 s per player and append-only logs. Above that, or with multiple hosts, PostgreSQL is required. The servers refuse a SQLite database path on a network filesystem mount.
- PostgreSQL connection counts are small and fixed (four kinds of process, a handful of connections each), so no pooler is needed.

## Implementation increments
1. **SQLite backend:** vendored amalgamation, `SqliteConnection`, account repository, and the migrator target with SQLite DDL. Repository tests run on all four CI toolchains with no server. *(Done first: it is what makes local development serverless.)*
2. **PostgreSQL backend:** libpq, `PgConnection`, the same repositories, and a CI job with a `postgres:16` service container.
3. Port the remaining repositories (character, party, guild, mail) to the portable subset, so each test runs on all backends.
4. `rathena-migrate export/import` (MySQL → PostgreSQL/SQLite), with the transform above and an M0 check on the source.
5. Stage B cutover; then stage C with the char/map persistence work.

## Amendment: full retirement

Instead of keeping MySQL until the strangler phase moves each table behind a repository, the legacy
servers were moved off MySQL directly:

- **`src/common/sql.cpp` runs on SQLite or PostgreSQL** behind the unchanged `Sql_*`/`SqlStmt` API, so
  none of the 348 query sites had to be rewritten wholesale. `db_backend: sqlite|postgresql` in
  `conf/inter_athena.conf` selects the engine; SQLite is the default and needs nothing installed.
- **A translation layer** (`src/common/sql_dialect.cpp`) adapts the MySQL spelling the servers and
  NPC scripts write: backticks, `LIMIT a,b`, `BINARY`, `START TRANSACTION`, and `REPLACE INTO` (an
  upsert on the primary key, looked up from the catalog, on PostgreSQL). `NOW()`,
  `UNIX_TIMESTAMP()` and `FROM_UNIXTIME()` are provided as functions on both engines. The few
  queries the layer cannot express (`INTERVAL` arithmetic, `UPDATE/DELETE ... LIMIT`,
  `INSERT DELAYED`) were rewritten at their call sites. The portable-subset rules above still
  apply to new code (repositories); the legacy queries keep their MySQL spelling.
- **The legacy schema is kept as-is**, generated per engine from the MySQL source files by
  `tools/sql/mysql2portable.py` (`sql-files/postgresql/`, `sql-files/sqlite/`). Case-insensitive
  names use `citext` (PostgreSQL) and `COLLATE NOCASE` (SQLite). PostgreSQL databases are
  `SQL_ASCII` with the `C` locale, so client bytes are stored unchanged, as in MySQL `latin1`.
  Servers create the schema in an empty database on first start.
- **Import keeps the legacy shape.** `rathena-migrate import` copies a MySQL database into the
  generated schema byte for byte (with an M0 check on the source). The transform listed under
  "Data migration" (0 → NULL, orphans, `pass_hash`, FKs) moves to the reimagined schema's own
  migration, together with the deferred D4/D5/D6 fixes; it no longer gates leaving MySQL.
- **Builds and CI:** autotools, CMake and MSBuild build the vendored SQLite and, when found, libpq.
  `build_servers_database.yml` runs the Sql/SqlStmt contract tests on both engines and a scripted
  login → char → map session (`tools/ci/smoke.sh`) on each. The MySQL-only tooling (`sql.sh`,
  `config.pl`, `update.pl`, `yaml2sql`, the `use_sql_db` item/mob tables) was removed.
- **`use_sql_db` is gone** (HITL #2): items and monsters come from the YAML databases only.
- **Ecosystem:** FluxCP-style tools that use the MySQL schema directly must switch to PostgreSQL
  (the table and column names are unchanged, so most queries port with identifier-quoting changes).
  Operator `query_sql` scripts keep working when they use the forms the translation layer handles;
  `doc/database.md` lists the forms that need porting.

User-facing documentation: `doc/database.md`.
