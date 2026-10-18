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
| `gen_tables.py` | generates `data/damage_tables.luau` from `db/re` + `db/size_fix.yml` and cross-checks it, and `data/damage_cfg.luau`, against what the binary loaded (400 element cells, 24 weapon rows, 22 battle config values) |

```sh
conformance/golden_gen/build.sh   $SCRATCH/golden-re                 # once
python3 -I conformance/golden_gen/gen_cases.py --server $SCRATCH/golden-re/rA --dump $SCRATCH/dump.txt   # rewrites conformance/golden/weapon_re.jsonl.gz
python3 -I conformance/golden_gen/gen_tables.py $REPO data/damage_tables.luau --dump $SCRATCH/dump.txt
python3 -I conformance/golden_gen/gen_cases.py --server $SCRATCH/golden-re/rA --seed 7 --scale 3 --out /tmp/fuzz.jsonl.gz   # ad-hoc fuzz set
GOLDEN_SRC=/tmp/fuzz.jsonl.gz python3 -I tools/build_golden.py --force && LUAU=<luau> python3 -I tools/run_tests.py --report
```

## What the copy is changed to do (all read-only observations of the original code path)

* `random.hpp/.cpp`: an optional log of every `rnd()` / `rnd_value(lo, hi)` result (no effect on the stream). Identical to the pre-renewal edit.
* `battle.cpp`: `GSTAGE` probes between the stages of `battle_calc_weapon_attack` in its **`#ifdef RENEWAL`** branches: the four ATK-part groups after
  `battle_calc_skill_base_damage` (`p_status p_weapon p_equip p_mastery`) and again after the card fixes and status bonuses (`c_*`), the sum, the ratio,
  the constant addition, RES, the DEF reduction, post-DEF, the element step, the crit multiplier, the target card fix, hands, map rates, final; plus
  the float32 weapon ATK range actually rolled in (`watk_mm`), the final hit rate and crit chance compared with the draw, the DEF values used, the
  weapon elements. 29 distinct probe names; they fix the stage order of `scripts/pipeline.luau` (`tests/acceptance/weapon_golden_test.luau`).
* `map.cpp`: `golden_main()` is called after `ScriptHost::run_oninit()` (a no-op unless `RA_GOLDEN_CASES` is set).

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

## Covered (8,858 cases, 59 categories)

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
