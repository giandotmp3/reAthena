# golden_gen: recording the real legacy damage code

Everything here runs against a **copy** of the legacy tree built in a scratch directory; the repository's legacy sources are never
modified.

| File | Role |
|---|---|
| `build.sh <dir>` | copies the tree, comments out `#define RENEWAL` in the copy (pre-renewal), applies `apply_edits.py`, drops in the hook, builds `map-server` (Ninja, SQLite backend, ~5 min on 4 cores) |
| `apply_edits.py` | the patch, as anchored edits (aborts unless every anchor matches exactly once; idempotent) |
| `legacy-golden-hook.patch` | the same edits as a unified diff, for review (`make_patch.sh` regenerates it) |
| `golden_hook.inc` | included at the end of `battle.cpp` (so it can call the file-static legacy functions): builds synthetic `map_session_data` / `mob_data` units with explicit status data, seeds the RNG, calls `battle_calc_attack(BF_WEAPON, ...)` |
| `golden_stage.hpp` | the stage-probe macro `GSTAGE(name, damage, damage2)` |
| `gen_cases.py` | builds the cases, runs the server once, writes `../golden/weapon_pre.jsonl.gz` |
| `gen_tables.py` | generates `data/damage_tables.luau` from `db/pre-re` and cross-checks it against the tables the binary loaded |
| `splice_rule_cases.py` | rewrites the converted rule entries of `cases/damage.luau` from the golden data |
| `golden_status_hook.inc` | the **status calculation** hook, included at the end of `status.cpp`: builds synthetic PC / mob units with an explicit status set `{sc_type, val1..val4}` and calls the real static `status_calc_*` functions (see "Status golden" below) |
| `st_extract.py` | parses the PRE branch of `status.cpp` (a small C subset: tokenizer, statement / expression parser); `list` prints the statuses each function reads, `incs` writes the generated include files the status hook needs (status table, constants, enum names) |
| `st_gen_defs.py` | one-time generator of the FIRST DRAFT of `scripts/status/defs/*.luau` (the Luau files are then maintained by hand); not run by CI |
| `gen_status_cases.py` | builds the status cases (>= 13,000), runs the patched server, writes `../golden/status_pre.jsonl.gz` |
| `st_consts.py`, `st_records.py`, `st_fields_doc.py`, `splice_status_cases.py` | generate `scripts/status/consts.luau` from the legacy dump, `data/status_records.luau` from the migrated database, `docs/STATUS_FIELDS.md`, and the converted status rule cases |

```sh
conformance/golden_gen/build.sh   $SCRATCH/golden            # once
python3 -I conformance/golden_gen/gen_cases.py --server $SCRATCH/golden/rA          # rewrites conformance/golden/weapon_pre.jsonl.gz
python3 -I conformance/golden_gen/gen_cases.py --server $SCRATCH/golden/rA --seed 7 --scale 3 --out /tmp/fuzz.jsonl.gz   # ad-hoc fuzz set
GOLDEN_SRC=/tmp/fuzz.jsonl.gz python3 -I tools/build_golden.py --force      # then: tools/run_tests.py --luau X --report
```

## What the copy is changed to do (all read-only observations of the original code path)

* `random.hpp/.cpp`: an optional log of every `rnd()` / `rnd_value(lo, hi)` result (no effect on the stream).
* `battle.cpp`: `GSTAGE` probes between the stages of `battle_calc_weapon_attack`, the final hit rate and crit chance that are compared
  with the draw, the DEF values and soft DEF actually used, the weapon elements; plus the hook include.
* `map.cpp`: `golden_main()` is called after `ScriptHost::run_oninit()` (a no-op unless `RA_GOLDEN_CASES` is set).

## Function under test

`battle_calc_attack(BF_WEAPON, src, target, 0, 0, 0)` (battle.cpp:6782), which calls the static `battle_calc_weapon_attack`
(5418) and then applies the BR-RULESET-001 miss normalisation (`total < 1` is a miss, players report `div` 1). It runs the real
`is_attack_critical`, `is_attack_hitting`, `battle_calc_base_damage`, `battle_calc_defense_reduction`, `battle_attr_fix`,
`battle_calc_cardfix`, `battle_calc_damage` (target statuses) and `battle_calc_gvg_damage`. Units are built field by field; no
`status_calc_*` runs, so every number in a case is an explicit input.

## Covered (5,424 cases, 40 categories)

PC and mob attackers against PC and mob targets, melee and bow (arrow attack, long range); lucky dodge (flee2), crit (chance after
target LUK and crit-defense, crit_atk_rate, crit_def_rate), hit rate clamp 5..100 (and toggled bounds), perfect hit, crowd penalties on
FLEE and DEF; base damage (min == max, mob ATK above ATK2, zero ATK, unarmed, every weapon type x every size, over-refine roll,
`batk`, Maximize Power, Weapon Perfection); hard DEF 0..127 / negative, soft DEF larger than the damage, `weapon_defense_type`; refine;
element table (all 400 cells) with Deluge / Volcano / Violent Gale (percent first), endow (Enchant Arms), EDP, negative ratios under
`attr_recover` 0 and 1, `attack_attr_none`; attacker and target card bonuses (race, element, size, class, short / long rates,
near / far defense, both `left_cardfix_to_right` modes, extreme sums); dual wield with AS_RIGHT / AS_LEFT / KO_RIGHT / KO_LEFT levels;
status ATK% (Curse, Provoke, Concentration), Berserk, Overthrust; target Assumptio, Sound of Destruction, Shadow Clock, Defender,
Adjustment, Smoke Powder, Water Barrier; normal / versus / GvG maps with the GvG short / long rates; worked examples E-1, E-3 and E-6.

## Not covered

Skills (ratio, constant additions, `div` > 1 and negative `div`, masteries, double attack / chain action, Fear Breeze), magic and misc
attacks, plants (`is_infinite_defense`), katar, left-hand-only fists, guns, statuses outside the list above (Aeterna, Barrier, Kaite,
Spider Web, Fusion, Reflect Shield, ...), reflect / absorb / devotion / Poison React side effects, PK mode, battleground rates,
`ksprotection`, `skill_min_damage`, item scripts, pets / homunculi / mercenaries as attackers. The Luau pipeline asserts (and tests
cover the assertion) when it is given an input outside this space, e.g. `bonus.double_rate`.

## Status golden (`GOLDEN_PRERE=1`)

The status calculation is recorded from a **strict pre-renewal** build (`GOLDEN_PRERE=1` also enables `#define PRERE` in the copy's `renewal.hpp`, which is what
`configure --enable-prere` does: no `RENEWAL_ASPD` / `RENEWAL_CAST` / `RENEWAL_STAT` ... either; without it `build.sh` comments out only `#define RENEWAL`, the
configuration the basic-attack golden was recorded with -- regenerating the basic-attack golden from the strict build gives a byte-identical file, so nothing
there depends on the sub-flags).

```sh
GOLDEN_PRERE=1 conformance/golden_gen/build.sh   $SCRATCH/golden-st-pre            # ~5 min on 4 cores, edits the COPY only
python3 -I conformance/golden_gen/gen_status_cases.py --server $SCRATCH/golden-st-pre/rA --dump-out $SCRATCH/consts.txt   # rewrites conformance/golden/status_pre.jsonl.gz (6 s)
python3 -I conformance/golden_gen/st_consts.py $SCRATCH/consts.txt scripts/status/consts.luau
python3 -I tools/build_golden.py --force && python3 -I tools/run_tests.py --luau X --status-report
```

A fresh build in an empty directory regenerates the committed file byte for byte (sha256 `3e3b90c3...`). What the hook records, all through the REAL functions:
`status_calc_str agi vit int dex luk pow sta wis spl con crt batk watk critical hit flee flee2 def def2 mdef mdef2 speed aspd_rate fix_aspd patk smatk res mres hplus crate
element element_lv attack_element mode`, `status_get_hpbonus / status_get_spbonus` (FIX and RATE, players and mobs) and through them `status_calc_maxhp / maxsp` (mobs),
`status_calc_pseudobuff_matk`, `status_calc_state` (the twelve `sc.cant` flags after one status starts or ends), `status_calc_bl_main` (the recalculation chain: base
status + stale battle status + a flag set + statuses -> the new battle status), and the **Script:** of every active status through the real script engine (the log of the
`pc_bonus*` calls, `g_golden_bonus_log`). It also dumps the status database as the real loader holds it (699 records), the constants and the `battle.conf` defaults.

Case categories (`cat`): `empty` (the `sc->empty()` path, extreme bases), `single` / `single_unit` (every status the function reads, four value levels, PC and mob),
`pair`, `multi`, `override` (early-return statuses with others), `clamp`, `all`, `speed_struct`, `aspd_struct`, `flee_struct`, `watk_struct`, `max_struct`, `max_neg`,
`pc_bonus`, `matk_struct`, `mode_struct`, `state`, `chain`, `chain_extreme`, `script`, `script_combo`, `worked` (E-4b and one illustration per converted rule), `quirk`.
The statuses and the `valN` each function reads, and the constants those are compared with, come from the parse of `status.cpp`, not from a hand list. Products are kept
below 2^31 (the legacy `int` would overflow, which is undefined); the replay's ctx refuses any int32 overflow.
