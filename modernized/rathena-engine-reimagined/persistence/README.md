# persistence (rathena-engine-reimagined)

Generic persistence service, C++20 + CMake + GoogleTest. No gameplay repositories: it stores opaque
`(owner, kind)` records and namespaced mod KV, with write-behind (copy-on-write batches), a bounded
on-disk WAL for DB outage, schema migrations, `id_map` with tombstones and row quarantine.
Backends: SQLite and PostgreSQL 16+ (MySQL is refused).

- Interface contract: `include/rathena/persist/service.hpp`; bodies are stubs in `src/stubs.cpp`.
- Implemented now: URL/env config and redaction (`db_config`), domain helpers, identifier quoting.
- Rules and test status: `rules/rule_manifest.tsv`. Test names embed the rule id (`ctest -R BR_CHAR_032`).
- Pending tests are real assertions gated by `PENDING_RULE(...)`; they skip with `PENDING <id>`.
  `RATHENA_PERSIST_RUN_PENDING=1 ctest` runs them (they fail until implemented).
- Config: `DATABASE_URL` (env only, no default), `RATHENA_WAL_DIR` (env only), `RATHENA_TEST_PG_URL`
  (disposable DB, tests only). Test fixtures use fake same-shape values only.

## Lift plan
Copy `SqliteConnection` and `PostgresConnection` (and the vendored SQLite amalgamation CMake block)
from `modernized/rathena-reimagined/persistence` into `src/`, rename the namespace to
`rathena::persist`, wire them into `open_connection()`, then implement `src/stubs.cpp` per rule.
Gameplay repositories (Account, Character, Guild, Mail...) are deliberately not lifted.
