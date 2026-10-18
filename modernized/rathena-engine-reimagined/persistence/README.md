# persistence (rathena-engine-reimagined)

Generic persistence service, C++20 + CMake + GoogleTest. No gameplay repositories: it stores opaque
`(owner, kind)` records and namespaced mod KV, with write-behind (copy-on-write batches), a bounded
on-disk WAL for DB outage, schema migrations, `id_map` with tombstones and row quarantine.
Backends: SQLite (system `libsqlite3`, linked) and PostgreSQL 16+ (optional, needs `libpq-fe.h`;
MySQL is refused).

## Layout

| Piece | Header | What it does |
|---|---|---|
| Connections | `sql_connection.hpp`, `sqlite_connection.hpp`, `connection_wrappers.hpp` | `open_connection(url)`; SQLite lifted from the earlier scaffold (statement cache, typed `RowRejectedError` for poison rows); `GuardedConnection` (share one connection between threads), `FaultInjectingConnection` (outage, N-th statement, poison parameter, statement log) |
| Config | `db_config.hpp`, `load_wal_config_from_env` | `DATABASE_URL` and `RATHENA_WAL_DIR` from the environment only, no default, URL redacted in every error |
| Schema | `service.hpp` `MigrationRunner`, `builtin_migrations()` | Ordered, checksummed, one transaction per migration, idempotent; built-in schema owns versions 1..999, an application runner uses `MigrationOptions{1000}` |
| Stores | `service.hpp` | `RecordStore` (admission, revisions, collections), `KvStore` (typed values, row versions + migration on read, per-mod quota ledger, raw layer), `IdMap` (ranges, explicit legacy numbers, tombstones), `Quarantine` |
| Write-behind | `service.hpp` `DirtyTracker`, `WriteBehindFlusher`; `writer.hpp` `WriteBehindService` | Producer marks and hands off an immutable batch pointer in O(1); one writer thread persists in order; save->auth `barrier(owner)`; `shutdown()` drains |
| WAL | `service.hpp` `Wal` | One CRC-protected segment file per batch, temp+rename, fsync policy `Never/File/FileAndDir`, byte limit -> `WalFullError`, exactly-once replay via a database progress marker |
| script-host | `script_host_adapter.hpp` | `KvStorageBackend : script_host::StorageBackend`, write-through or write-behind with a read-through cache |

Rules and test status: `rules/rule_manifest.tsv`. Test names embed the rule id (`ctest -R BR_CHAR_032`).

## Build and test

```sh
cmake -S . -B build -G Ninja -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<googletest checkout>   # offline
cmake --build build && ctest --test-dir build -j4
# sanitizers (warnings are errors in every configuration):
cmake -S . -B build-asan -DRATHENA_PERSIST_SANITIZE=address ...   # ASan + UBSan
cmake -S . -B build-tsan -DRATHENA_PERSIST_SANITIZE=thread ...
```

* `RATHENA_PERSIST_WITH_POSTGRESQL=AUTO|ON|OFF`: PostgreSQL is compiled only when `libpq-fe.h` and
  libpq are found. `PERSIST_BACKEND_002` runs only when `RATHENA_TEST_PG_URL` names a DISPOSABLE
  database and the backend is compiled; otherwise it skips with the reason. `src/postgres_connection.cpp`
  has only been syntax-checked against a mock header (no libpq headers or server where it was written).
* `RATHENA_PERSIST_SCRIPT_HOST_DIR` (default `../script-host`): the adapter library compiles
  script-host's `src/storage.cpp` read-only from there; the storage contract tests need no Luau.
* Config: `DATABASE_URL`, `RATHENA_WAL_DIR` (env only), `RATHENA_TEST_PG_URL` (tests only). Fixtures
  use fake same-shape values only.
* Mass-save numbers are printed by `ctest -R PERSIST_MASS_001 -V`.

## Operating notes

* One process owns the database and the WAL directory (`flock` on `<dir>/LOCK`).
* Threading: stores are single-threaded over one `SqlConnection`; share the connection between the
  sim thread and the writer through `GuardedConnection`, or give the writer its own file connection.
* While the backend is down the writer keeps spilling batches to the WAL; at the WAL limit it holds
  the head batch, `mark()` throws `WalFullError`, and it retries until the backend returns. A
  `shutdown()` with a held batch reports it (`unpersisted_batches`) instead of dropping it.
* Poison rows (constraint/type errors, not outages) are isolated, written to `persist_quarantine`
  with their raw bytes (reason text never contains payload bytes) and the rest of the batch is applied.
