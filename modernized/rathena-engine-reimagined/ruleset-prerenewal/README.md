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
- `data/`: PROVISIONAL starter statuses, skills, items, resistance, tables. `rathena-migrate` overwrites them.
- `conformance/`: cases keyed by BR-RULESET ID (`cases/*.luau`), `required.luau`, `pending.luau`, `golden/`.
- `tests/`: harness, fake engine host, smoke tests, one acceptance test per rule.

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
