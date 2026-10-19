# ruleset-prerenewal

Luau package `ruleset.prerenewal` for the rathena-engine (architecture sections 6.3 to 6.6,
milestone M3). Mutually exclusive with `ruleset.renewal`; depends on `rathena.base` (mechanism only).

Stack: Luau package (`mod.toml`, `scripts/`, `data/`, `types/*.d.luau`), checked in CI by
`luau-analyze`. Sources use the Lua/Luau common subset so they also run under plain Lua locally.

## Layout

- `mod.toml`: id, conflicts, ruleset sub-flags (all off), no permissions.
- `scripts/pipeline.luau`: PRE stage order for weapon, magic, misc (BR-RULESET-018, 044) and `register(engine)`: `engine.pipeline.define`
  for the three orders, then the weapon stage bodies (`pipeline.stage`; `init` and `final` protected). The engine, not this package,
  owns the stage loop (script-host `pipeline.run`); magic and misc are declared without bodies. Implemented.
- `scripts/manifest.luau`: manifest and battle-config defaults (BR-RULESET-118). Implemented.
- `scripts/domain.luau`: validators for status, skill, item definitions. Implemented.
- `scripts/rules/*.luau`: one `rNNN` function per assigned rule. Stubs raise `NOT_IMPLEMENTED`.
- `scripts/damage/`: the pre-renewal **basic weapon attack** (no skills), written against an explicit `ctx` interface
  (documented in the header of `weapon.luau`: injected `rng`, truncating `idiv` / `mul_div`, int64 accumulators). The 15 stage bodies
  are `weapon.STAGES.*` (registered with the engine by `weapon.register`); `calc_attack` builds the attack state, runs the prelude
  (lucky dodge, crit, hit) and calls `pipeline.run("weapon", S, ...)`. `rules/damage.luau` calls the pure stage functions.
- `scripts/status/`: the declarative **status layer** (stat aggregation, speed / ASPD, element / mode, states, scripted statuses, recalculation),
  validated against the real legacy `status_calc_*` functions (see "Status layer" below and `docs/STATUS_AGGREGATION.md`).
- `data/status_records.luau`: the 699 status records of `db/pre-re/status.yml` (generated from the migrated database, cross-checked against the
  real loader). `data/`: PROVISIONAL starter statuses, skills, items, resistance, tables. `rathena-migrate` overwrites them.
  `damage_tables.luau` (element matrix, size modifiers) is generated from `db/pre-re` and cross-checked against the legacy binary;
  `damage_cfg.luau` holds the `conf/battle` tunables the weapon pipeline reads.
- `conformance/`: cases keyed by BR-RULESET ID (`cases/*.luau`), `required.luau`, `pending.luau`, `golden/` (recorded legacy
  data), `golden_gen/` (the generator: patch, build script, case generator; see its README).
- `tests/`: harness, fake `ctx` (int64-truncating, integer-safe), scripted RNG with draw counting, golden runner, one acceptance
  test per rule, the golden conformance test, `pipeline_test` (register, cancel / replace / insert / move through the engine API),
  `quirks_test` (`legacy_quirks`), `bench_weapon`. `tests/support/fake_host.luau` is the Luau CLI's stand-in for script-host's
  `engine` table (pipeline, `num.f32`, `config`; byte-identical in both packages, checked against the real runtime by
  `script-host/tests/test_rulesets.cpp`), and `booted.luau` registers the package with it. Under script-host (mod loader, rathena-sim)
  `scripts/init.luau` registers the package with the real globals when it loads.
- `tools/`: `run_tests.py` (lupa or `--luau`), `build_golden.py`, `bundle_luau.py`.

## Status layer (`scripts/status/`)

`status.define(name, { fields = {val1 = "lv", val2 = "agi_inc"}, contributes = { agi = { {at = 6882, op = "add", v = fn} } }, ... })` is the front
end; the aggregator (`aggregate.luau`) builds the ordered contribution list of an attribute from the active set and folds it with the attribute's
stage list (`attrs.luau`). 319 statuses are defined (`defs/*.luau` first draft generated from `status.cpp` by `conformance/golden_gen/st_gen_defs.py`, then
maintained by hand together with `defs_hand.luau`); every contribution carries `at = status.cpp line`, the citation and the evaluation order.
`speed.luau`, `aspd.luau`, `mode.luau`, `maxhp.luau`, `states.luau`, `scripted.luau`, `chain.luau` (`status_calc_bl_main`), `recalc.luau`,
`engine_model.luau` (the same value from engine-core `Contribution{source, tag, value}` records), `ops.luau`, `consts.luau` (generated from the
legacy build). `docs/STATUS_FIELDS.md` answers Q-F (names of `val1..val4`). Golden: `conformance/golden/status_pre.jsonl.gz` (13,100 cases recorded from
the real code, strict `PRERE` build; `conformance/golden_gen/gen_status_cases.py`), replayed by `tests/acceptance/status_golden_test.luau`
(`python3 -I tools/run_tests.py --luau X --status-report` prints the match rate by function, category and status, and the contribution coverage).
`tools/mutation_status.py --luau X` breaks the Luau in 45 ways; every mutant must fail the replay.

## Assigned rules (109)

BR-RULESET rules marked PRE or B in 001-110, minus RE-only 009, 010, 011, 019, 025, 034, 054,
plus ruleset-side contract rules 111, 112, 116, 117, 118, 119. Rules 113 (mob AI), 114 (ground
units) and 115 (numeric helpers) belong to native-defaults, engine-core and script-host.
Worked examples E-1, E-3, E-4, E-4b, E-5, E-6, E-7, E-8, E-10 (PRE branch) are encoded as cases.

## Test status

Implemented and passing: the basic attack rules (see the header of `conformance/pending.luau`), 118, 119 and, with the status layer, BR-RULESET-051, 065, 066, 067, 068, 069,
071, 073, 075, 077. The rest are expected-failure, tagged with the rule ID in `conformance/pending.luau`. `python3 -I tools/run_tests.py` (plain Lua through lupa) and `--luau`
run the main suite (**106 pass / 76 expected-failure**; it was 96 / 86 before the ten status rules landed) and the extra suite (`tests/extra.luau`: pipeline API,
`legacy_quirks`, the status golden replay and quirks: 92 tests, all pass). An expected-failure test that passes (XPASS) fails the run,
so remove the ID from `pending.luau` when the rule lands. Cases with `expected = nil` still need a
golden-trace recording.

    pip install lupa && python3 -I tools/run_tests.py

## Notes

No credentials or database access are used. If runtime config is ever needed, read it from the
environment (`${DATABASE_URL}`); nothing in this package does. Legacy defects (Burnt, `rnd%(max-min)`)
are reproduced as-is behind `legacy_quirks` (default on), per the HITL #2 outcome.

`legacy_quirks` is real: script-host exposes the manifest's `[ruleset] legacy_quirks` as `config.legacy_quirks`, `scripts/init.luau`
makes it the default of `cfg.legacy_quirks` (`data/damage_cfg.luau`), and the weapon pipeline reads it. The one defect of this
package's paths it governs is `weapon_damage_rate[16]` indexed by weapon types up to 23 (`pc.hpp:577`): true reproduces the
aliasing (guns read `arrow_addele[type - 16]`), false reads the weapon's own slot (`bonus.wdr_r` / `wdr_l`). The step itself
(`battle_add_weapon_damage` after the over-refine roll) was missing here before; no PRE golden case sets a weapon_damage_rate bonus or
`arrow_addele` for a gun, so its expectations in `tests/acceptance/quirks_test.luau` are derived from `battle.cpp:2411-2429`, not
recorded from a PRE build. The doubled monster crit multiplier (x14/10 in the base damage AND x1.4 after DEF) is `#ifdef RENEWAL`
in the legacy code and does not exist here.
