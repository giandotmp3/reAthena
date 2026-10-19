# Architecture proof: is renewal "a data diff plus a few stage overrides"?

Exit criterion of milestone M5 (architecture 12, CLAUDE.md "Decisions in force"): `ruleset.renewal` must be mostly a data diff plus a few stage overrides over the shared
mechanism, and must need **no** change to engine-core, script-host, mods or `rathena.base`. This file reports what was measured when the renewal **basic weapon attack** was
implemented in Luau and checked against the real legacy renewal code. Every number below is produced by a script (`tools/diff_report.py` -> `PIPELINE_DIFF.md`,
`pipeline_diff.json`; `tools/mutation_check.py`; the golden replay) or by a test; the exact tables are in `PIPELINE_DIFF.md`.

> **Status update (pipeline runtime work, section 12).** Findings 1 to 3 of section 6 (no stage bodies / no `move`, no `num.f32`, `legacy_quirks` read by nothing) are
> closed: script-host now has the pipeline runtime, `num.f32` and `config.legacy_quirks`, and both rulesets run on them. The sentences below that say the package
> "calls exactly one engine API" and that nothing in script-host changed describe the first proof and are kept as the record; section 12 lists what changed and
> the numbers that moved (for example the shared-line figure rose from 34 % to 43 % only because the pre-renewal package now has the same `STAGES` / `register` /
> `calc_attack` skeleton; the stage classes are unchanged).

## 1. Verdict

| Claim | Result |
|---|---|
| The renewal weapon pipeline reproduces the real legacy renewal code | **Yes.** 8,858 / 8,858 golden cases match (damage, damage2, type, div, flag, dmg_lv, the number, kind and bounds of all 16,957 recorded RNG draws, and 34 kinds of intermediate stage probes), plus 153,708 / 153,708 fresh-seed fuzz cases. 28 mutations of the Luau code are all caught |
| No change to engine-core, script-host, mods or `rathena.base` was needed | **Yes.** The package calls exactly one engine API, `engine.pipeline.define`, and needs no new ctx method. `mods_tool` (the real mods manifest/resolver/base-purity code) accepts the manifest and passes the `rathena.base` purity gate with this package present |
| Renewal is **mostly a data diff** plus a few stage overrides | **No, for the weapon pipeline.** 24 stages in the renewal order: 3 identical to pre-renewal, 9 with different logic, 10 that pre-renewal does not have, 2 empty for plain attacks. 34 % of the renewal pipeline code lines (284 of 824) are lines the pre-renewal package also has. Data differences are real but small (184 of 400 element cells, 2 of 24 size rows, 1 battle.conf key, a handful of constants) |
| The stage ORDER is a ruleset property (architecture 6.4) | **Yes, and larger than assumed.** 8 pairs of common stages are in the opposite order; the order cannot be derived from the pre-renewal one with the mod-facing edit calls without 23 operations (3 cancel, 12 insert_after, 8 replace) versus one `pipeline.define` |

The mechanism holds; the "mostly data" expectation does not. What is genuinely shared is the infrastructure (test support 77 % shared lines, tools 78 %, golden generator 60 %),
a layer of 19 small identical helper functions (96 lines) and the tail of the pipeline (`div_hands`, `map_rates`, `final`). The renewal ATK model (status / weapon / equip / mastery / percent
parts, P.ATK, RES, a float32 weapon roll, element per part, cards per part, crit after element) is new logic, not a parameter set.

## 2. What was built

* `scripts/damage/weapon.luau` (710 code lines): stage bodies in `STAGES`, one per stage name, run by `calc_attack` in the order of `scripts/pipeline.luau`; the prelude (lucky dodge,
  crit roll, hit roll) is the same structure as pre-renewal. Same `ctx` interface as the pre-renewal package (`rng.rnd/value`, `idiv`, `mul_div`, `acc` lanes), so engine and base need nothing.
* `scripts/damage/fp.luau` (28 lines): IEEE helpers for the places where the legacy code computes in `float` (weapon ATK variance, P.ATK and crit rate conversions, RES narrowing, the crit multiplier).
* `scripts/formulas.luau`: the entry points `res_reduction`, `crit_damage`, `weapon_atk_range` (backed by the same code as the stages); the formulas without golden data still raise `NotImplemented`.
* `conformance/golden_gen/` + `conformance/golden/weapon_re.jsonl.gz`: the renewal golden generator and its 8,858 recorded cases (README there).
* `tests/`: Luau suite (111 tests), Python wrapper (50 tests), `tools/`: bundler, lint, `diff_report.py`, `mutation_check.py`, `trace_case.sh`.

## 3. Validation

| Check | Result |
|---|---|
| Golden replay | 8,858 / 8,858 (65 categories). Mismatch classes (output, draw count, draw sequence, error): 0 / 0 / 0 / 0 |
| Intermediate stage probes (34 probe names) | identical for every case: a regression localises to a stage, and the order is proven by the real code |
| Fresh-seed fuzz (`--seed N --scale 3`, 6 seeds, never committed) | 6 x 25,618 = 153,708 cases, 0 mismatches |
| Reproducible generator | `build.sh` from scratch + `gen_cases.py` produced a byte-identical `weapon_re.jsonl.gz` |
| Mutation check (`tools/mutation_check.py`) | 28 single-expression mutants, all killed (from 1 to 6,681 cases differ); the float32 evaluation of the weapon ATK range is an *equivalent* mutant (verified: float32 and double truncate identically for every weapon ATK 1..400, refine 0..60, level 1..4, stat 1..130) |
| Mutation survivors found on the way | four non-equivalent mutants survived the first 8,308 cases (stage order crit-vs-element and ratio-vs-crit-rate, the ATK% 65535 cap, the element step of the mastery card fix); 550 interaction cases were added (`rates_with_ratio`, `element_mob_crit`, `element_pc_crit`, `status_atk_cap`, `cards_mastery_status`) and the mutants now die |
| Worked examples of BR_RULESET section 7 (renewal branches) | E-1 RE 1650, E-2 840 / 912, E-3 1380, E-9 [120, 160], crit 1400 / 1770: reproduced by the **legacy code** and by the Luau pipeline |
| Speed | replay of the 8,858 cases about 0.4 s (about 45 us per case including the comparison); about 25 us per attack in the pure-Luau fake ctx |

## 4. (a) How much is identical to pre-renewal

From `PIPELINE_DIFF.md` (difflib matching lines of non-blank, non-comment lines):

| group | files | PRE lines | RE lines | shared | share of RE |
|---|---|---|---|---|---|
| pipeline code (`weapon`, `consts`, `unit`, `pipeline`, `init`, `fp`, `formulas`) | 7 | 556 | 824 | 284 | 34 % |
| of which `scripts/damage/weapon.luau` | 1 | 486 | 710 | 240 | 34 % |
| data (`damage_cfg`, `damage_tables`) | 2 | 91 | 93 | 50 | 54 % (98 % if numbers are masked: the structure is identical, the numbers differ) |
| test support | 7 | 412 | 422 | 327 | 77 % (4 files byte-identical) |
| tools | 4 | 163 | 147 | 105 | 71 % |
| golden generator | 6 | 808 | 1000 | 595 | 60 % |

Functions of `weapon.luau` (same-name comparison): 19 identical (96 lines), 11 with different logic (204 lines), 35 only in renewal (394 lines, of which 24 functions / 238 lines are the stage bodies that
pre-renewal keeps inline in `calc_attack`, a 96-line function), 11 only in pre-renewal (140 lines). About 50 of the 394 renewal-only lines are features that also exist in the legacy *pre-renewal* code path but
that the pre-renewal package does not implement (mastery ATK, `weapon_damage_rate`, monster ranged flag, Weaponry Research hit bonus, `arrow_ele`, `damagetaken`): the renewal package covers a wider
input space, so its sharing percentage is slightly pessimistic; adding those to pre-renewal would move it to about 40 %, not to "mostly".

## 5. (b) Stage by stage: data-only, override, order

24 renewal stages (table with the functions in `PIPELINE_DIFF.md` section 3):

| Class | Stages |
|---|---|
| **identical** code (3) | `div_hands`, `map_rates`, `final` |
| **data-only** (0 whole stages) | none. The data-only differences are *inside* overridden stages: element table 184 of 400 cells, size table 2 of 24 weapon rows, `max_res_mres_ignored` (new key), base hit rate 80 -> 0, Berserk +100 -> +200, DEF storage width int8 -> int16 |
| **override**: same stage name, different logic (9) | `init` (hit rate: Vulture's Eye, Weaponry Research; monster ranged flag; arrow element), `base_attack` (ATK parts vs one rolled number), `card_attacker` (no short/long/katar steps), `skill_ratio` (ATK% seeds the ratio), `defense` (`ATK*(4000+DEF)/(4000+10*DEF) - softDEF`, no RNG), `post_defense`, `element` (players already applied it per part; monsters skip it under `attack_attr_none`), `card_target` (only non-player attackers), `target_modifiers` (Assumptio, Steel Body, Kaite, Raid) |
| **new** in renewal (10) | `card_target_parts`, `sc_bonus`, `sum`, `p_atk`, `mastery`, `crit_rate`, `adv_katar_mastery` (empty for plain attacks), `res`, `crit_multiplier`, `flat_additions` (empty for plain attacks) |
| empty in both for plain attacks (2) | `const_add`, `skill_atk_bonus` |
| pre-renewal only (1) | `element_bonus` (star crumb / EDP after the table) |

**Different ORDER** (8 inverted pairs among the 14 common stages): in pre-renewal `skill_ratio`, `const_add`, `skill_atk_bonus`, `defense`, `post_defense`, `element` come *before* `card_attacker`;
in renewal `card_attacker` is the second stage and everything else follows the sum of the parts. Verified from the real code: the golden probes of every case occur in the renewal order
(`tests/acceptance/weapon_golden_test.luau`), and the pre-renewal order is the one `ruleset-prerenewal` registers. `skill_atk_bonus` (before DEF in pre-renewal, after post-DEF in renewal) and
`card_attacker` are the two *moved* stages.

## 6. (c) Changes to engine-core, script-host, mods or rathena.base

**None were needed.** Evidence: the package calls `engine.pipeline.define` only (`test_register_calls_only_pipeline_define`; the Luau test gives `register()` an engine that offers nothing else and
fails on any other access); nothing under `scripts/` or `data/` refers to the pre-renewal package or imports outside `scripts/` and `data/` (`test_no_runtime_dependency_on_the_other_ruleset`);
`git status` shows no change under `engine-core`, `script-host`, `native-defaults`, `mods/base`, `ruleset-prerenewal` made by this work; the real `mods_tool` accepts the manifest
(`mods_tool manifest`), resolves it with `rathena.base`, refuses it together with `ruleset.prerenewal` (declared conflict), and the base purity gate passes with this package's identifiers
derived as forbidden names (`mods_tool check-base --ruleset ruleset-renewal --ruleset ruleset-prerenewal`: 38 forbidden names, PASS).

**Design findings that did not need a change but will need a decision** (stated plainly):

1. **The pipeline API has no way to register stage BODIES.** `define(kind, order)` carries names; `use_native` binds a native id; `replace(kind, stage, id)` refers to an `id` that nothing registers.
   Both rulesets therefore keep the bodies package-locally and run them with their own loop (`calc_attack`). A real engine-driven pipeline needs `define(kind, order, bodies)` or a `stage(kind, name, fn)` call
   (the replace/insert/cancel semantics are exercised against this package's own runner in `tests/acceptance/architecture_test.luau`).
2. **There is no `move`.** Reordering with the edit calls costs a `cancel` plus an `insert_after` under a new id; only `define` re-orders cheaply. That is fine for a ruleset, bad for a mod that wants to move a stage.
3. **float32 is not in the ctx.** The host has the lane method `f32_step(factor)` (one `(int64)((float)v * (float)factor)` step). Renewal needs seven other float32 operations (variance, base-stat bonus,
   `(float)` of a divided integer for P.ATK and crit rate, the RES narrowing to float, the float32 multiplier itself). They are written in 28 lines in `scripts/damage/fp.luau` with pure arithmetic.
   `fp.f32` (round to binary32) is exactly the primitive a `num.f32(x)` in script-host / rathena.base would provide.
4. **`legacy_quirks` is declared in the manifest but no code reads it.** Two reproduced defects ship unconditionally: the doubled monster crit multiplier (x14/10 in the base damage, x1.4 after DEF; `battle.cpp:2635`, `5679-5683`) and the
   `weapon_damage_rate[16]` array indexed by weapon type up to 23 (`pc.hpp:577`: gun types read `arrow_addele[type-16]`). The ruleset needs the flag in its `cfg`.
5. **Shared code is not a base-package question yet.** See section 8.

## 7. (d) pipeline.define / insert / replace / cancel usage

* What the package uses: `pipeline.define("weapon", ORDER.weapon)` and `pipeline.define("magic", ORDER.magic)`: 2 calls. Nothing else.
* What it would take to express renewal as a *delta over the pre-renewal stage names* with the mod-facing edit calls (simulated by `FakePipeline` in `tools/diff_report.py`, which checks the result equals the
  renewal order): **3 `cancel` + 12 `insert_after` + 8 `replace` = 23 operations** per pipeline kind (list in `PIPELINE_DIFF.md` section 4), against 1 `define`. The moved stages come back under new ids
  (`ruleset.renewal:card_attacker`, `ruleset.renewal:skill_atk_bonus`), 8 same-named stages need replaced bodies. This is the quantified reason why each ruleset registers its own order.
* The hook surface of BR-RULESET-117 (`base`, `ratio`, `element`, `defense`, `final`) maps onto renewal stages `base_attack`, `skill_ratio`, `element`, `defense`, `final`; the new renewal stages
  (`sum`, `p_atk`, `res`, `crit_multiplier`) are additional hook points mods may want (`res` and `crit_multiplier` in particular).

## 8. Helper code that wanted to be shared (listed, not created)

The architecture allows `rathena.base` mechanism only (no game numbers or formulas). Identical in both packages:

* **mechanism-level** (candidates for `rathena.base` or script-host `num`): `bit_set`, `wrap16`, `bl_type`, `sc_of`, `at`, `bonus_of`, `probe` (stage trace), the lane macros `atk_rate`, `atk_add_rate`, `min_damage`, `step`
  (38 lines), `fp.f32` rounding (28 lines in `fp.luau`, new), the stage runner loop (`calc_attack`, ~25 lines), a stage-order -> body table convention;
* **byte-identical test support** (candidates for a shared `ruleset-testkit`): `fake_ctx.luau`, `scripted_rng.luau`, `fake_host.luau`, `bench_weapon.luau`, `harness.luau` (1 line differs), `golden_runner.luau`
  (1 line differs), `bundle_luau.py`, `build_golden.py`, `lint_numeric.py`, `golden_stage.hpp`, `golden_hook.inc` (261 of 303 lines), `apply_edits.py` (random-draw log and map hook edits), `gen_tables.py`;
* **game-rule helpers identical in both rulesets** (they contain formulas, so base may not host them): `atk_percent`, `crowd_penalty`, `dual_wield`, `finish`, `gvg_damage`, `lucky_dodge`, `apply_cardfix`, `left_handed`
  (58 lines). If the project wants no duplication, a third `ruleset-common` package (not in the architecture) would be the home; the migrator's diff report is the arbiter (architecture 6.3).

## 9. Corrections to BR_RULESET.md and latent gaps found

See `BR_RULESET_CORRECTIONS.md` (corrected text for 15 rules, not applied to the source file).

## 10. Not covered

Skills (ratio, constants, `div` > 1, skill masteries), magic and misc attacks (the magic order in `pipeline.luau` is from BR-RULESET-044 and unverified), plants, katar, left-hand-only fists, Soul Attack,
Mild Wind and `SC_SUB_WEAPONPROPERTY`, `SC_WATK_ELEMENT`, statuses outside the lists in `conformance/golden_gen/README.md`, the `status_calc_*` derivation of every input (refine P.ATK, armor RES, DEF2, ASPD),
reflect / absorb / devotion, PK and battleground rates, `ksprotection`, item scripts, non-player attackers other than monsters, riding.

## 11. Reproduce

    conformance/golden_gen/build.sh $SCRATCH/golden-re                    # ~5 min, renewal build of a copy of the tree
    python3 -I conformance/golden_gen/gen_cases.py --server $SCRATCH/golden-re/rA --dump $SCRATCH/dump.txt
    LUAU=<luau> python3 -I tools/run_tests.py [--report | --bench]       # Luau suite, 111 tests
    LUAU=<luau> python3 -m unittest discover -s tests                     # Python wrapper, 50 tests
    LUAU=<luau> python3 -I tools/mutation_check.py                        # 28 mutants
    python3 -I tools/diff_report.py                                       # regenerates PIPELINE_DIFF.md / pipeline_diff.json

## 12. Follow-up: closing the pipeline API gaps (findings 1 to 4 of section 6)

**What was built (script-host, `include/script_host/pipeline.hpp`, `src/pipeline.cpp`, bindings in `sandbox.cpp`).**
`pipeline.define / stage / run` (the ruleset), `insert_before / insert_after / replace / cancel / move` (mods) with priority, precedence, protected stages
(`Permission::ProtectedStages`), observe / modify / replace modes, a transactional effect queue, per-hook RNG streams, `strict_parity`, `describe(name)` (effective list, source
mod of every edit) and `order(name)`; the registry is rebuilt and validated with the event bus on every reload and committed atomically with it. `num.f32` (`f32_round`) in the numeric API.
`config.legacy_quirks` (read-only table filled from `ModEnvironmentSpec::config`, default true). `api/engine.yaml` gained the types, the `pipeline`, `num` and `config` namespaces and
an `Engine` type; `engine.d.luau` is regenerated by the existing generator, which learned `values:` and the `engine:` list.

**What the packages do now.** Both register stage bodies (`weapon.STAGES.*`, `init` and `final` protected) and call `pipeline.run` instead of their private loops. The pre-renewal
`calc_attack`, a 96-line monolith, was split into its 15 stage bodies with the same probes in the same order. `fp.luau` shrank from 28 code lines of rounding to thin wrappers over `engine.num.f32`.
Under script-host a package registers itself when `scripts/init` loads (script-host defines the globals `pipeline`, `num`, `config`; it does not define `engine` by default because the mods bundler
would then call `register(engine)` on every entry module and `rathena.base` has a module with an unrelated `register(name, handler)`).

**legacy_quirks, corrected semantics.** Renewal, `false`: a monster crit is multiplied once by 1.4f in the `crit_multiplier` stage (golden case 2520: 1009 -> 766 -> 1072 under the quirk, 721 -> 529 -> 740 corrected);
`weapon_damage_rate` is read from the weapon's own slot (`wdr_r` / `wdr_l`) for every type instead of `arrow_addele[type - 16]` for types 16 to 23 (rifle ATK 105 with +20 % in `arrow_addele[2]`
and -10 % in `wdr_r`: 126 under the quirk, 95 corrected). Pre-renewal: only the `weapon_damage_rate` defect exists in its code path (the doubled crit is `#ifdef RENEWAL`); the step itself was missing in the
package and was added, unverified by PRE golden data (no PRE golden case exercises it). With `true` (default) every golden case is unchanged; with `false` nothing else changes (compared over all 8,858 / 5,424 cases).

**Validation.** See the report that accompanied this change for exact before / after counts. `script-host/tests/test_rulesets.cpp` replays 8,858 and 5,424 golden cases through the real runtime, inserts a stage
into the renewal pipeline, applies the 23 operations of `PIPELINE_DIFF.md` (read from `pipeline_diff.json`) to the pre-renewal order and checks the result equals the renewal order with the source mod of each edit, does
the same with `move` (21 operations, original ids kept), proves the packages share no code, and compares the CLI suites' fake host with the real runtime.

**Cost of the engine-driven loop** (us per golden case, min / median of 9 CLI runs and 7 host runs on a shared 4-core machine, noisy; Release build; calc_attack alone, fake ctx):

| | before (private loop) | after (pipeline.run) |
|---|---|---|
| pre-renewal, Luau CLI + fake host (pure-Luau pipeline double) | 10.1 / 11.6 | 13.2 / 14.4 |
| renewal, Luau CLI + fake host | 24.9 / 27.6 | 27.6 / 29.0 |
| pre-renewal, script-host sandbox, real runtime | 12.6 / 14.4 | 17.0 / 19.7 |
| renewal, script-host sandbox, real runtime | 30.9 / 33.3 | 29.6 / 33.4 |
| replay incl. comparison, pre-renewal / renewal, sandbox | 26.9 / 48.2 (min) | 32.6 / 53.0 (min) |

Renewal is within noise; pre-renewal pays about +4 us per attack (15 stage calls, 15 state-field accesses where the monolith used locals).

**Not done / limits.** The mods unit does not yet copy `manifest.ruleset.legacy_quirks` into `ModEnvironmentSpec::config` (so a manifest value of `false` is not honoured by the full loader) and has no permission
kind for protected stages. A foreign-state stage sees the lane and a copied `view`, not live entity handles. `use_native`, the magic and misc bodies, and the three latent PRE gaps other than `weapon_damage_rate` (monster ranged flag,
Weaponry Research hit bonus) are not implemented. The 153,708 / 76,660 fresh-seed fuzz runs need the legacy builds and were not repeated; the golden replays and 28 / 28 mutants were.

## 13. Part 2: the status layer (status effects and stat aggregation)

Question: after the weapon pipeline, is the renewal STATUS layer (what every status does to every stat, BR-RULESET-051, 065 to 077) a data diff over a shared mechanism, and did it need anything from
engine-core or script-host? Built: `scripts/status/` (declarative `status.define`, a pure-Luau aggregator with an ordered stage list per attribute that is expressible in `engine-core/include/engine/attrs/attrs.hpp`),
checked against the REAL `status_calc_*` functions of the legacy renewal binary (`conformance/golden_gen/golden_status_hook.inc`). Numbers below are produced by `tools/run_tests.py --status-report`,
`tools/mutation_status.py`, `tools/status_overlap.py` (`STATUS_OVERLAP.md`) and the tests.

### 13.1 Verdict

| Claim | Result |
|---|---|
| The declarative layer reproduces the real code | **Yes.** 31,470 / 31,470 cases (85,178 recorded outputs of 41 functions, and the `sc->cant` restrictions of all 1,025 sc_type values) match with nothing tolerated. 317 statuses are read by the renewal `status_calc_*` functions (parsed from `status.cpp`, `STATUS_ENUMERATION.md`); 321 are defined; all 658 contribution sites are applied by some case and every conditional site is also skipped by some case. 51 mutants of `scripts/status`, 51 killed (48 by the golden replay, 3 only by the unit tests; section 13.5) |
| No change to engine-core, script-host, mods or `rathena.base` | **Yes.** Nothing outside `ruleset-renewal/` was written (git status). The status layer also runs, unchanged, in script-host's REAL mod sandbox with the mods loader's module system: 31,470 / 31,470 matched, 2.3 s including the load of the golden data (`tools/host_status/`). script-host has no `attrs` namespace (`api/engine.yaml`), so the fold runs in Luau; see 13.4 |
| Renewal status behaviour is **mostly data** over a shared mechanism | **Yes for the contributions, no for the stage lists.** 532 of the 533 (function, status) sites that exist in both modes have the identical statement text; 239 of the 296 statuses the pre-renewal functions reference (81 %; 294 of them also exist in renewal, 2 are pre-renewal only: Gravitation and Longing) are identical by structure (same statements, same relative order); the other 55 differ because renewal ADDS lines to them. Renewal adds 111 sites that pre-renewal lacks: 85 in the 13 renewal-only functions (40 for the new ASPD model, 12 for the six trait stats, 33 for the six derived stats) and 26 extra lines inside functions both modes have; 23 renewal statuses are new. The skeleton (stage list) is identical for 23 of the 27 functions that exist in both modes |

### 13.2 What is data and what is logic

* **Status records (data, BR-051).** 1,018 renewal records against 699 pre-renewal ones (`rathena-migrate`): 697 in both, 462 of them identical in states, calc flags, flags, options, min rate / duration, Fail / EndOnStart /
  EndOnEnd / EndReturn lists and script (66 %); the 235 that differ differ mostly in `flags` (152) and the Fail list (64). 697 renewal records have no behaviour in any `status_calc_*` function: they are data only
  (`data/status_records.luau`, generated, 287 KB). 321 have a hand-written definition: 1,540 code lines for 658 contributions, one `status.cpp` line each.
* **Contributions (data in a declarative form).** `status.define(name, { fields, contributes = { attr = { op, value expression, when, at } } })`: an op name from the vocabulary of the attribute, an
  expression in a 140-line integer language, a condition, the legacy line as the order key. A renewal-only effect is another contribution line of an existing status or a new status; nothing else changes.
* **Logic that is the ruleset's (1,253 code lines).** The stage list of each of the 48 attributes (`attrs_re.luau`, 468 lines: plain chains (ordered ops, then `cap_value`) for 34 attributes; custom stage functions for
  speed (9 stages) and its rate (6), the fixed ASPD group (6) and the ASPD rate (6), the maximum HP / SP / AP (uint64 wrap-around), flee's map penalty), the aggregator, the expression language, `states.luau`, `recalc.luau`, `bonus_scripts.luau`. The
  skeleton comparison (`STATUS_OVERLAP.md` section 3) says which of these differ from pre-renewal: ASPD (renewal-only), the six trait stats and six derived stats (renewal-only, trivial chains), `watk` (the
  pre-renewal Nibelungen weapon-level branch), `def2` (the renewal Angelus/VIT term), `mdef2` and `maxsp` (renewal branches in the clamp / the SP rate).
* **Not factored into `rathena.base`:** as with the pipeline, 81 % identical contributions do not make a shared definition worthwhile: the PRE package would still need its own stage lists, its own
  records and the 55 non-identical statuses, and the two packages stay independent (section 8).

### 13.3 What the mechanism needed

1. **engine-core `AttributeSystem` is sufficient as a contract.** A ruleset-supplied stage list per attribute folding an insertion-ordered list of `(source, tag, value)` contributions expressed everything:
   the legacy functions are ordered statement lists, so the order key had to travel with the contribution (it is packed into the opaque `tag`: `stage|at|op|arg`; `tests/acceptance/status_model_test.luau` feeds the
   aggregator's contributions into a Luau transcription of the C++ class, `scripts/status/engine_model.luau`, and compares 8,000+ outputs with the legacy numbers). Three things had to be done ABOVE the
   engine class: (a) facts of the unit (skills, mounts, items, map, `sc == nullptr`) enter as contributions (`fact|0|<name>`), because a stage function sees nothing else; (b) derived attributes
   (`speed_rate` feeding `speed`; fix and rate feeding the maximum HP / SP / AP) are materialised by the caller as contributions of the parent: the engine class has no dependency between attributes;
   (c) early `return`s of the legacy functions are decided by every later stage looking for the same contribution again (stages are pure and only carry the accumulator).
2. **script-host exposes no `attrs` API** (`api/engine.yaml` lists events, storage, dialogue, timers, definitions, rng, num, pipeline, config). The aggregation therefore lives in the mod as pure Luau. To
   move the fold into the engine (cached per entity and attribute, invalidated by `add_contribution`) a binding of `AggregationStage` to a Luau function would be needed: a new API, not a change of
   existing behaviour. `definitions.declare` (shape of a definition) exists and was not needed.
3. **Cost.** In the Luau CLI (interpreter): 14 us for one attribute with 10 active statuses, 22 us on average over 31 attributes (0.69 ms for a full recalculation of one unit), 62 us per golden case including the
   comparison. A full recalculation per status change for an area buff on 50 units would cost ~35 ms in this implementation; the incremental recompute by calc flag (`recalc.luau`, BR-077) and the engine
   cache are the levers (not measured here).
4. **Numerics.** Everything the legacy code does in `int32`, `int16` narrowing returns, `uint64` and the arithmetic shift is reproduced; `u64.luau` exists only for the maximum-HP wrap-around
   (`maxhp += (int32)bonus` as `uint64`, then `cap_value(..., 1, UINT_MAX)`).

### 13.4 Findings that changed the design or the specification

* The golden hook cannot be fed the server's battle.conf defaults implicitly: three values the functions read (`gvg_flee_penalty`, `bg_flee_penalty`, `rental_mount_speed_boost`) have non-zero defaults; the first
  replay differed in hundreds of cases until the generator wrote them explicitly (it is the same class of defect as the `rules_config_refresh` finding of the weapon proof).
* Behaviour that only the golden replay exposed (all in `BR_RULESET_CORRECTIONS.md`): `status_calc_aspd` returns 0 before it adds the passive skills when the unit has no status; `status_calc_maxhp` ignores
  `sc == nullptr`; the maximum HP wraps to UINT_MAX for a base below the negative fixed bonus; Camouflage's DEF term reads a value nobody assigns; the re-entrance guard resets to zero.
* `db/re/status.yml` calc flags do not cover 14 contributions of 11 statuses, even after the `status_calc_bl_main` cascade (BR-077).

### 13.5 Mutation check and coverage

`tools/mutation_status.py`: 51 single-expression mutants (op semantics, clamps, order, the speed and ASPD rules, the uint64 wrap, group ranks, `sc == nullptr`, conditions, narrowing, shifts, ten data
constants, two state rules). 48 are killed by the golden replay; 3 (the ASPD potion rank in the fixed group and in the rate, the arithmetic shift) change nothing the legacy cases can observe (the aggregator
already reduces a rank group to its best member; `& 0xFF` hides the fill bits of a shift) and are killed by direct unit tests of the stage functions and of the expression language instead.

### 13.6 Not covered (and why)

* `status_calc_bl_main` composition (base + stat deltas: `hit = base + dex delta + luk / 3 ...`, DEF2 / MDEF2 deltas, amotion from AGI / DEX / weapon, MATK, maximum HP of a player from the job tables), `status_calc_pc_sub`
  item scripts and refine contributions, `status_calc_regen`: they read the finished stat values or the item bonus vocabulary (BR-098..110); the cascade of FLAGS is transcribed and tested (`recalc.luau`) but not
  golden-validated.
* Applying a status: rates, durations, resistance, overlap, exclusion, strip and food rules, the tick handlers (BR-052 to 064, 072): `status_change_start` / `_end` are not run by the hook. BR-054 and 057 stay
  expected-failure.
* `status_calc_consumablematk` has an empty body; 701 of the 1,018 renewal statuses are read by no `status_calc_*` function (they are states, options, damage-time or timer effects).
* The scripted statuses (BR-075) are compiled to bonus-delta lists; applying the deltas is the item-bonus vocabulary.
* Homunculus and mercenary BASE status calculation, pets, elementals.
