# Architecture proof: is renewal "a data diff plus a few stage overrides"?

Exit criterion of milestone M5 (architecture 12, CLAUDE.md "Decisions in force"): `ruleset.renewal` must be mostly a data diff plus a few stage overrides over the shared
mechanism, and must need **no** change to engine-core, script-host, mods or `rathena.base`. This file reports what was measured when the renewal **basic weapon attack** was
implemented in Luau and checked against the real legacy renewal code. Every number below is produced by a script (`tools/diff_report.py` -> `PIPELINE_DIFF.md`,
`pipeline_diff.json`; `tools/mutation_check.py`; the golden replay) or by a test; the exact tables are in `PIPELINE_DIFF.md`.

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
* `tests/`: Luau suite (111 tests), Python wrapper (49 tests), `tools/`: bundler, lint, `diff_report.py`, `mutation_check.py`, `trace_case.sh`.

## 3. Validation

| Check | Result |
|---|---|
| Golden replay | 8,858 / 8,858 (65 categories). Mismatch classes (output, draw count, draw sequence, error): 0 / 0 / 0 / 0 |
| Intermediate stage probes (34 probe names) | identical for every case: a regression localises to a stage, and the order is proven by the real code |
| Fresh-seed fuzz (`--seed N --scale 3`, 6 seeds, never committed) | 6 x 25,618 = 153,708 cases, 0 mismatches |
| Reproducible generator | `build.sh` from scratch + `gen_cases.py` produced a byte-identical `weapon_re.jsonl.gz` |
| Mutation check (`tools/mutation_check.py`) | 28 single-expression mutants, all killed (from 1 to 6,681 cases differ); the float32 evaluation of the weapon ATK range is an *equivalent* mutant (verified: float32 and double truncate identically for every weapon ATK 1..400, refine 0..60, level 1..4, stat 1..130) |
| Three mutation survivors found on the way | stage order crit-vs-element, ratio-vs-crit-rate and mastery-vs-P.ATK was not pinned by the first 8,308 cases; 550 interaction cases were added (`rates_with_ratio`, `element_mob_crit`, `element_pc_crit`, `status_atk_cap`, `cards_mastery_status`) and the mutants now die |
| Worked examples of BR_RULESET section 7 (renewal branches) | E-1 RE 1650, E-2 840 / 912, E-3 1380, E-9 [120, 160], crit 1400 / 1770: reproduced by the **legacy code** and by the Luau pipeline |
| Speed | replay of the 8,858 cases 0.37 s (42 us per case including comparison); 26.8 us per attack in the pure-Luau fake ctx |

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

Functions of `weapon.luau` (same-name comparison): 19 identical (96 lines), 11 with different logic (204 lines), 35 only in renewal (394 lines, of which 22 are the stage bodies that
pre-renewal keeps inline in `calc_attack`), 11 only in pre-renewal (140 lines). About 55 of the 394 renewal-only lines are features that also exist in the legacy *pre-renewal* code path but
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
4. **`legacy_quirks` is declared in the manifest but no code reads it.** Two reproduced defects ship unconditionally: the doubled monster crit multiplier (x14/10 in the base damage, x1.4 after DEF; `battle.cpp:2636`, `5686`) and the
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
    LUAU=<luau> python3 -m unittest discover -s tests                     # Python wrapper, 49 tests
    LUAU=<luau> python3 -I tools/mutation_check.py                        # 28 mutants
    python3 -I tools/diff_report.py                                       # regenerates PIPELINE_DIFF.md / pipeline_diff.json
