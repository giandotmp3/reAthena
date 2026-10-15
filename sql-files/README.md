# SQL files

rAthena stores its data in SQLite or PostgreSQL; see [doc/database.md](../doc/database.md).

### Layout
---
* `main.sql`, `logs.sql`, `web.sql`, `roulette_default_data.sql` -- the schema **source**, written
  in MySQL syntax. Edit these when the schema changes.
* `postgresql/` and `sqlite/` -- the same schema for each engine, **generated** from the source by
  `python3 tools/sql/mysql2portable.py`. Do not edit them by hand; CI fails when they are stale
  (`--check`).
* `upgrades/` -- historical MySQL upgrade scripts. Use them only to bring an old MySQL database up
  to date before moving it with `rathena-migrate import`.

### New install
---
Nothing to import: a server that finds an empty database creates the tables from
`<backend>/main.sql`, `logs.sql`, `web.sql` and `roulette_default_data.sql`
(`db_auto_create_schema: yes`, the default). To create them yourself instead:

    sqlite3 save/ragnarok.db < sql-files/sqlite/main.sql      # and logs.sql, web.sql, ...
    psql -d ragnarok -f sql-files/postgresql/main.sql         # and logs.sql, web.sql, ...

The log tables (`logs.sql`) may live in a separate database (`log_db_db` in
`conf/inter_athena.conf`).

### Notes
---
The `web-server` must be able to read the `login` and `guild` tables from the `login-server` and `char-server`, respectively.
