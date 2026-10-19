# golden_gen (renewal): recording the real legacy RENEWAL damage code

Same mechanics as `ruleset-prerenewal/conformance/golden_gen/` (read its README first), run against a **renewal** build of a **copy** of the legacy
tree in a scratch directory; the repository's legacy sources are never modified.

| File | Role |
|---|---|
| `build.sh <dir>` | copies the tree, keeps `#define RENEWAL` (and every `RENEWAL_*` sub-flag) on, applies `apply_edits.py`, drops in the hook, builds `map-server` (Ninja, SQLite backend, ~5 min on 4 cores; the binary loads `db/re` and compiles the `#ifdef RENEWAL` branches) |
| `apply_edits.py` | the patch, as anchored edits (aborts unless every anchor matches exactly once; idempotent) |
| `legacy-golden-hook.patch` | the same edits as a unified diff, for review (`make_patch.sh <dir>` regenerates it from a build tree) |
| `golden_hook.inc` | included at the end of `battle.cpp` (so it can call the file-static legacy functions): builds synthetic `map_session_data` / `mob_data` units with explicit status data, seeds the RNG, calls `battle_calc_attack(BF_WEAPON, ...)` |
| `golden_stage.hpp` | the stage-probe macro `GSTAGE(name, damage, damage2)` (byte-identical to the pre-renewal one) |
| `gen_cases.py` | builds the cases, runs the server once (10 s for 8,858 cases), writes `../golden/weapon_re.jsonl.gz` |
| `golden_status_hook.inc` | the status hook: included at the end of `status.cpp` (so it can call the file-static `status_calc_*`), builds a synthetic player / monster / homunculus with explicit facts and an explicit status set `{sc_type, val1..val4}` and calls the real functions; see "Status golden" below |
| `enum_statuses.py` | parses `status.cpp` (RENEWAL branches) and lists which statuses each `status_calc_*` function references (317 statuses, 40 functions; `docs/STATUS_ENUMERATION.md`); also writes the `sc_type` name table the hook includes |
| `gen_status_cases.py` | builds the status cases from that list, runs the server once (about 10 s for 31,000 cases) and writes `../golden/status_re.jsonl.gz` |
| `gen_status_consts.py` | writes `scripts/status/consts.luau` from the constants dump of the binary (`RA_GOLDEN_ST_DUMP`) |
| `gen_tables.py` | generates `data/damage_tables.luau` from `db/re` + `db/size_fix.yml` and cross-checks it, and `data/damage_cfg.luau`, against what the binary loaded (400 element cells, 24 weapon rows, 22 battle config values) |

```sh
conformance/golden_gen/build.sh   $SCRATCH/golden-re                 # once
python3 -I conformance/golden_gen/gen_cases.py --server $SCRATCH/golden-re/rA --dump $SCRATCH/dump.txt   # rewrites conformance/golden/weapon_re.jsonl.gz
python3 -I conformance/golden_gen/gen_tables.py $REPO data/damage_tables.luau --dump $SCRATCH/dump.txt
python3 -I conformance/golden_gen/gen_cases.py --server $SCRATCH/golden-re/rA --seed 7 --scale 3 --out /tmp/fuzz.jsonl.gz   # ad-hoc fuzz set
GOLDEN_SRC=/tmp/fuzz.jsonl.gz python3 -I tools/build_golden.py --force && LUAU=<luau> python3 -I tools/run_tests.py --report
```

## Status golden (`golden_status_hook.inc`)

Same copy, same binary: `RA_GOLDEN_ST_CASES=<cases> RA_GOLDEN_ST_OUT=<out> map-server --run-once` runs `golden_status_main()` before `golden_main()`. The hook is included at the end of
`status.cpp`, so the functions under test are the real static ones: `status_calc_str agi vit int dex luk pow sta wis spl con crt batk watk hit critical flee flee2 def def2 mdef mdef2
speed aspd (fixed and percent) fix_aspd aspd_rate patk smatk res mres hplus crate maxhp maxsp maxap element element_lv attack_element mode` and `status_calc_pseudobuff_matk`, plus
`status_calc_state` (the `sc->cant` restrictions) for every one of the 1,025 `sc_type` values. A case is one unit (`bl=0` player, `1` monster, `2` homunculus) with explicit facts
(`vit`, `weapon`, `sex`, `class_`, `option` bits, `perm_speed`, `skilltimer`, `ud_skill_*`, `bonus_*`, `map` 0/1 GvG/2 battleground, `cfg.*` battle.conf values, `skill.NAME=lv`),
`sc.SC_NAME=v1,v2,v3,v4` entries and `in.<function>=value` inputs; `nosc=1` passes `sc == nullptr`. Nothing is derived. Output: `id=N fn=value ...` (and `states=<bit mask>`).
The battle.conf values the functions read (`gvg_flee_penalty` 20, `bg_flee_penalty` 20, `rental_mount_speed_boost`) are ALWAYS written explicitly: the hook would otherwise keep the
server's defaults (found while bringing up the replay: several hundred cases differed because a Luau default of 0 met a binary default of 20).

Categories (31,470 cases at the committed seed, `gen_status_cases.py` header): `single` (every referenced status x player / monster / homunculus x ten val patterns: a level series,
the values the source's conditions compare with, random), `pair_site` (statuses read by one statement) and `pair` (a sample per function), `override` (the statuses that `return`
early against random others), `combo` (2..12 statuses), `extreme` (negative and out-of-clamp inputs), `facts` and `speed_facts` (mounts, skills, items, cart, Madogear, Free Cast, GvG,
maximum HP / SP / AP bonuses), `wrap` (uint64 wrap-around of the maximum HP), `clamp` (the edges of every function's range), `worked` (E-4b and the named semantic points the tests
pin), `states` and `states_combo`. Intermediate int32 overflows are undefined behaviour in the legacy C++, so the generator keeps products inside int32 (the replay counts overflow
cases: 0).

```sh
conformance/golden_gen/build.sh $SCRATCH/golden-st-re                       # once (the same build as above)
python3 -I conformance/golden_gen/gen_status_cases.py --server $SCRATCH/golden-st-re/rA        # rewrites conformance/golden/status_re.jsonl.gz, byte-identical for the seed
python3 -I tools/run_tests.py --luau <luau> --status-report                  # match rate by function, category and status + contribution coverage
python3 -I tools/mutation_status.py --luau <luau>                            # 51 mutants of scripts/status
```

## What the copy is changed to do (all read-only observations of the original code path)

* `random.hpp/.cpp`: an optional log of every `rnd()` / `rnd_value(lo, hi)` result (no effect on the stream). Identical to the pre-renewal edit.
* `battle.cpp`: `GSTAGE` probes between the stages of `battle_calc_weapon_attack` in its **`#ifdef RENEWAL`** branches: the four ATK-part groups after
  `battle_calc_skill_base_damage` (`p_status p_weapon p_equip p_mastery`) and again after the card fixes and status bonuses (`c_*`), the sum, the ratio,
  the constant addition, RES, the DEF reduction, post-DEF, the element step, the crit multiplier, the target card fix, hands, map rates, final; plus
  the float32 weapon ATK range actually rolled in (`watk_mm`), the final hit rate and crit chance compared with the draw, the DEF values used, the
  weapon elements. 34 distinct probe names; they fix the stage order of `scripts/pipeline.luau` (`tests/acceptance/weapon_golden_test.luau`).
* `map.cpp`: `golden_main()` is called after `ScriptHost::run_oninit()` (a no-op unless `RA_GOLDEN_CASES` is set); `golden_status_main()` before it (a no-op unless `RA_GOLDEN_ST_CASES` or `RA_GOLDEN_ST_DUMP` is set).
* `status.cpp`: `#include "golden_status_hook.inc"` at the very end (read-only calls of the static functions; `golden_status_table.inc`, the sc_type name table, is generated by `enum_statuses.py --table`).

Differences from the pre-renewal hook (`golden_hook.inc`, 303 code lines, 261 of them shared with the pre-renewal one):

* status data the renewal parts read: `watk/watk2` (uint16 sums of weapon and refine ATK, as `status.cpp:4368` stores them), `rhw.wlv`, `eatk`, `patk`, `crate`, `res`, `mres`, `bonus.atk_rate`,
  `weapon_damage_rate[type]`, `ignore_res_by_race`, `arrow_ele`, the ammo item (arrow / cannonball), a shield in the left hand, the weapon range of a monster, mob `damagetaken`;
* skills: Vulture's Eye, Weaponry Research, Sword / Two-Hand / Mace / Spear / Axe Mastery, Demon Bane (and the dual-wield passives of the pre-renewal hook);
* statuses: Telum, Potent Venom, Kaite, Raid, Steel Body, Ground Gravity (and the pre-renewal list);
* `rules_config_refresh()` after every per-case battle.conf toggle: the delegated map-rules calls (`crowd_flee`, `crowd_def`) read a **snapshot** (`rules_bridge.cpp`) of
  `agi_penalty_*` / `vit_penalty_*`, so a hook that only writes `battle_config` would silently leave the snapshot at its default (found while covering `crowd_penalty` type 2).

## Function under test

`battle_calc_attack(BF_WEAPON, src, target, 0, 0, 0)` (battle.cpp:6782), which calls the static `battle_calc_weapon_attack` (5418) and then applies the BR-RULESET-001 miss
normalisation. It runs the real `is_attack_critical`, `is_attack_hitting`, `battle_calc_damage_parts` (status / weapon / equip / mastery / percent ATK),
`battle_calc_base_weapon_attack` (float32 variance), `battle_calc_base_damage` (monsters), `battle_calc_cardfix`, `battle_attack_sc_bonus`, the P.ATK / crit rate /
ratio / RES / `battle_calc_defense_reduction` (`rules::combat::apply_defense_re`) / `battle_calc_attack_post_defense` / `battle_calc_element_damage` /
x1.4 crit steps, `battle_calc_attack_left_right_hands`, `battle_calc_damage` and `battle_calc_gvg_damage`. Units are built field by field; no `status_calc_*` runs, so
every number in a case is an explicit input.

## Covered (8,858 cases, 65 categories)

PC and mob attackers against PC and mob targets, melee, bow and gun (arrow attack, ammo present or not, cannonball), monsters with a ranged weapon; lucky dodge (flee2), crit
(chance after target LUK and crit-defense, `critadd_race`, arrow crit), hit rate clamp (and toggled bounds), perfect hit, Vulture's Eye and Weaponry Research, crowd penalties on
FLEE and DEF (types 0, 1, 2); weapon ATK roll (STR or DEX, every weapon level, Maximize Power, critical, Weapon Perfection, uint16 limits), unarmed players, a shield in the left hand,
over-refine and `weapon_damage_rate` (and the array-aliasing legacy defect for gun types), every weapon type x every size, status ATK (`batk`), equip ATK (`eatk`, arrow ATK), AtkRate, mastery ATK;
P.ATK (including values above 2^24 where the float conversion loses bits), the float32 crit multiplier (`crate`), `crit_atk_rate` / `non_crit_atk_rate` / short / long rates together with a
skill ratio; RES (ignore-RES by race, Telum, Potent Venom, the `max_res_mres_ignored` cap and its toggles); hard DEF -32768..32767 (-400 guard), soft DEF larger than the damage, `weapon_defense_type`;
element table (all 400 cells), Volcano / Deluge / Violent Gale as ratio additions, endow, EDP (renewal formula), negative ratios under `attr_recover` 0 and 1, `attack_attr_none` toggles for monsters, arrow element;
attacker and target card bonuses (race, element, size, class, both `left_cardfix_to_right` modes, extreme sums; target cards on every ATK part of a player attacker versus on the total of a monster);
dual wield with AS_RIGHT / AS_LEFT / KO_RIGHT / KO_LEFT; status ATK% (Curse, Provoke, Concentration, floor 0, cap 65535), Berserk, Overthrust; target Soul of Destruction, Shadow Clock, Defender, Adjustment,
Smoke Powder, Water Barrier, Kaite, Raid, Steel Body, Ground Gravity, Assumptio (no effect in renewal), mob `damagetaken`; normal / PvP / GvG maps with the GvG short / long rates; worked examples E-1 (renewal branch), E-2, E-3, E-9 and the crit values.

## Not covered

Skills (ratio, constant additions, `div` > 1 and negative `div`, skill masteries, double attack / chain action, Fear Breeze), magic and misc attacks, plants (`is_infinite_defense`), katar, left-hand-only fists,
Soul Attack (summoner; the STR/DEX exception of the weapon roll), Mild Wind and `SC_SUB_WEAPONPROPERTY` (status ATK in another element), `SC_WATK_ELEMENT`, statuses outside the lists above (Aeterna, Barrier, Spider Web,
Fusion, Reflect Shield, ...), the status_calc_* derivation of every input (refine P.ATK, armor RES, DEF2 sources, ASPD), reflect / absorb / devotion / Poison React side effects, PK mode, battleground rates,
`ksprotection`, `skill_min_damage`, item scripts, pets / homunculi / mercenaries as attackers, riding (Spear Mastery), Ranger / Research Fe / Beast Bane / Breakthrough masteries. The Luau pipeline asserts (and the
tests cover the assertion) when it is given an input outside this space, e.g. `bonus.double_rate`, a katar, or a field that does not fit the legacy int16 it is stored in.
