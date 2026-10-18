# ruleset-prerenewal

Luau package `ruleset.prerenewal` for the rathena-engine (architecture sections 6.3 to 6.6,
milestone M3). Mutually exclusive with `ruleset.renewal`; depends on `rathena.base` (mechanism only).

Stack: Luau package (`mod.toml`, `scripts/`, `data/`, `types/*.d.luau`), checked in CI by
`luau-analyze`. Sources use the Lua/Luau common subset so they also run under plain Lua locally.

## Layout

- `mod.toml`: id, conflicts, ruleset sub-flags (all off), no permissions.
- `scripts/pipeline.luau`: PRE stage order for weapon, magic, misc (BR-RULESET-018, 044). Implemented.
- `scripts/manifest.luau`: manifest and battle-config defaults (BR-RULESET-118). Implemented.
- `scripts/domain.luau`: validators for status, skill, item definitions. Implemented.
- `scripts/rules/*.luau`: one `rNNN` function per assigned rule. Stubs raise `NOT_IMPLEMENTED`.
- `scripts/damage/`: the pre-renewal **basic weapon attack** (no skills), written against an explicit `ctx` interface
  (documented in the header of `weapon.luau`: injected `rng`, truncating `idiv` / `mul_div`, int64 accumulators). `rules/damage.luau`
  calls its stage functions.
- `data/`: PROVISIONAL starter statuses, skills, items, resistance, tables. `rathena-migrate` overwrites them.
  `damage_tables.luau` (element matrix, size modifiers) is generated from `db/pre-re` and cross-checked against the legacy binary;
  `damage_cfg.luau` holds the `conf/battle` tunables the weapon pipeline reads.
- `conformance/`: cases keyed by BR-RULESET ID (`cases/*.luau`), `required.luau`, `pending.luau`, `golden/` (recorded legacy
  data), `golden_gen/` (the generator: patch, build script, case generator; see its README).
- `tests/`: harness, fake `ctx` (int64-truncating, integer-safe), scripted RNG with draw counting, golden runner, one acceptance
  test per rule, the golden conformance test, `bench_weapon`.
- `tools/`: `run_tests.py` (lupa or `--luau`), `build_golden.py`, `bundle_luau.py`.

## Assigned rules (109)

BR-RULESET rules marked PRE or B in 001-110, minus RE-only 009, 010, 011, 019, 025, 034, 054,
plus ruleset-side contract rules 111, 112, 116, 117, 118, 119. Rules 113 (mob AI), 114 (ground
units) and 115 (numeric helpers) belong to native-defaults, engine-core and script-host.
Worked examples E-1, E-3, E-4, E-4b, E-5, E-6, E-7, E-8, E-10 (PRE branch) are encoded as cases.

## Test status

Implemented and passing: BR-RULESET-018, 118, 119. The other 106 are expected-failure, tagged with
the rule ID in `conformance/pending.luau`. An expected-failure test that passes (XPASS) fails the run,
so remove the ID from `pending.luau` when the rule lands. Cases with `expected = nil` still need a
golden-trace recording.

    pip install lupa && python3 -I tools/run_tests.py

## Notes

No credentials or database access are used. If runtime config is ever needed, read it from the
environment (`${DATABASE_URL}`); nothing in this package does. Legacy defects (Burnt, `rnd%(max-min)`)
are reproduced as-is behind `legacy_quirks` (default on), per the HITL #2 outcome.
