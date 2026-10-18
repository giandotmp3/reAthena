# data/

Starter definitions, hand-authored and PROVISIONAL. The real tables are emitted by
`rathena-migrate` from `db/pre-re` (+ `db/import*`) and overwrite these files; the
migrator diff report decides what, if anything, is shared with `ruleset.renewal`.
Client IDs below follow the legacy numbering for the few starter entries.
Numeric values must be re-checked against migrator output before use in conformance.

## Generated damage data

`damage_tables.luau` (element matrix `db/pre-re/attr_fix.yml`, size modifiers `db/size_fix.yml` + `db/pre-re/size_fix.yml`) is written by
`conformance/golden_gen/gen_tables.py`, which also compares every cell with the tables the legacy binary loaded (400 element cells,
24 weapon rows). `damage_cfg.luau` is hand-kept: the `conf/battle` tunables the weapon pipeline reads, verified against the same binary
by the golden hook (`RA_GOLDEN_DUMP`). `rathena-migrate` is expected to take over both.
