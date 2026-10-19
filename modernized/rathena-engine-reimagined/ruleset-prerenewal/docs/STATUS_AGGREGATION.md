# Status aggregation, pre-renewal: declarative definitions validated against the real legacy code

Scope: BR-RULESET-051, 065 .. 069, 071, 073, 075, 077 (stat contributions, DEF, speed, ASPD, the "same pattern" attributes, element / mode overrides, states, scripted
statuses, partial recalculation) for the PRE ruleset. Source of truth: the PRE branch of `src/map/status.cpp` (`#ifdef RENEWAL` and the `RENEWAL_*` sub-flags are
off: the golden is recorded from a strict `PRERE` build). Everything below was checked against the real `status_calc_*` functions, not against the spec text.

## 1. The model

**Definition** (`scripts/status/define.luau`): `status.define(NAME, { fields = {val1 = "lv", val2 = "agi_inc", ...}, contributes = { agi = { entry, ... }, ... } })`.
`NAME` is the legacy `SC_` suffix (the database name). The status record (id, sc number, states, calc flags, flags, fail / end lists, opt, min rate / duration, script)
comes from `data/status_records.luau` and is merged in (`status.load_records`). 319 statuses are defined: the 316 the stat functions read, `PUSH_CART` (hidden behind the
`pc_iscarton` macro) and `RECOGNIZEDSPELL` / `MAGICPOWER` (read by `status_calc_bl_main`); 271 are in `db/pre-re/status.yml`, 48 are not (`in_pre_db = false`: the code
knows them, a PRE server never starts them).

**Contribution** (`entry`): `{ at = <status.cpp line>, op = ..., v = number | fn(s, e), v2 = ..., when = fn(s, e), group = ..., stage = ... }`.
`at` is the citation **and** the evaluation order: the legacy functions are straight-line code, so inside one attribute the contributions fold in source order.
Operations are pure functions of the running value and one or two operands:

| op | meaning (D = truncating division) | | op | meaning |
|---|---|---|---|---|
| `add v` | acc + v | | `final v` | `return v;` narrowed to the legacy return type, ends the function |
| `mul_pct v` | acc + D(acc * v, 100) | | `final_div v` | `return acc /= v;` |
| `mul_frac v v2` | acc + D(acc * v, v2) | | `final_clamp v` | `return cap_value(v, lo, hi);` |
| `scale v v2` | D(acc * v, v2) | | `stop` | `return cap_value(acc, lo, hi);` after the status' own adds (Harmonize) |
| `div v` | D(acc, v) | | `gmax v` / `gset v` + `group` | the `val` / `max` variable of speed and ASPD (max-groups, first-wins chains) |
| `set v` / `max v` | v / max(acc, v) | | `pct_over v v2` | acc + D((acc - v) * v2, 100) (Concentrate); `sub_pct2` (Joint Beat) |

**Aggregation** (`scripts/status/aggregate.luau`, `attrs.luau`): for one attribute the active statuses' contributions (plus the unit rules no status owns: map penalties of FLEE, item
speed, the player part of the HP bonus) are emitted sorted by `at`, then folded with the attribute's stage list: `pre` (map penalty) > `override` (terminal contributions: `return 0;`
`return 90;`, Harmonize's `str -= v; return clamp`; the first active one in legacy order ends the calculation) > `ordered` (everything else, one after the other) > `clamp`
(the `cap_value` bounds of the legacy function). That is the default `override > add > mul_pct > clamp` chain made faithful: the legacy interleaves adds and percentages on the
running value (Blessing halves the value *after* the adds before it, Concentrate scales `agi - card_bonus`), so the order is the source order, not a type order. Early returns are
narrowed to the legacy return type without a clamp (`uint16` / `int16` / `signed char` for PRE DEF and MDEF). Custom stage lists: `speed.luau` (mount > slow > haste groups, 40 floor,
then the ordered tail), `aspd.luau` (`aspd_rate - best quicken`), `mode.luau`, `maxhp.luau`.

**Expressible in the engine `attrs` module** (`engine-core/include/engine/attrs/attrs.hpp`): `scripts/status/engine_model.luau` turns the emitted list into flat
`Contribution { source, tag, value }` records (tag = `<stage>/<op>`, a second operand is the following record tagged `<tag>.2`) and folds them with five stage functions
(`base`, `pre`, `override`, `ordered`, `clamp`; for `aspd_rate` `base`, `quicken`, `ordered`, `clamp`): the left fold from 0 over the contribution list, as `AttributeSystem::fold`.
It gives the legacy value on every standard-attribute golden case (`tests/acceptance/status_golden_test.luau`). What `attrs.hpp` does **not** offer and the package did not need:
nothing; what the package keeps outside the fold: the control flow of `speed` (Mado Gear, Free Cast), `mode`, the max HP wrap, `states` and the chain (a dirty-flag graph over the
attribute fold -- `chain.luau` is the Luau counterpart of the recalculation graph the engine would run when a contribution changes).

## 2. Coverage and result

Golden: `conformance/golden/status_pre.jsonl.gz`, **13,100 cases** recorded from the real functions (strict pre-renewal build, a fresh build regenerates the file byte for byte),
**13,100 / 13,100 match** (every output compared, no tolerance). Replay 0.3 s (23 us / case); the extra suite incl. all status tests runs in 2.5 s.

| Case `fn` | Legacy function (status.cpp line) | statuses it reads | cases | matched |
|---|---|---|---|---|
| `agi` | status_calc_agi 6848 | 25 | 242 | 242 |
| `aspd_rate` | status_calc_aspd_rate 8427 | 39 | 543 | 543 |
| `atk_element` | status_calc_attack_element 8857 | 16 | 252 | 252 |
| `batk` | status_calc_batk 7316 | 10 | 184 | 184 |
| `chain` | status_calc_bl_main 5858 (flags propagation, narrowing, deltas) | - | 981 | 981 |
| `con` | status_calc_con 7276 (") | 2 | 147 | 147 |
| `crate` | status_calc_crate 8715 (") | 1 | 143 | 143 |
| `cri` | status_calc_critical 7525 | 9 | 175 | 175 |
| `crt` | status_calc_crt 7296 (") | 2 | 147 | 147 |
| `def` | status_calc_def 7769 | 40 | 366 | 366 |
| `def2` | status_calc_def2 7878 | 17 | 218 | 218 |
| `dex` | status_calc_dex 7060 | 24 | 242 | 242 |
| `element` | status_calc_element 8790 | 11 | 217 | 217 |
| `element_lv` | status_calc_element_lv 8824 | 12 | 223 | 223 |
| `fix_aspd` | status_calc_fix_aspd 8397 | 8 | 174 | 174 |
| `flee` | status_calc_flee 7633 | 40 | 486 | 486 |
| `flee2` | status_calc_flee2 7745 | 4 | 153 | 153 |
| `hit` | status_calc_hit 7565 | 21 | 224 | 224 |
| `hp_fix` | status_get_hpbonus(FIX) 3169 (players and mobs) | - | 342 | 342 |
| `hp_rate` | status_get_hpbonus(RATE) 3169 | - | 282 | 282 |
| `hplus` | status_calc_hplus 8697 (") | 1 | 143 | 143 |
| `int` | status_calc_int 6982 | 24 | 246 | 246 |
| `luk` | status_calc_luk 7135 | 19 | 226 | 226 |
| `matk` | status_calc_pseudobuff_matk 7452 | 14 | 284 | 284 |
| `maxhp` | status_calc_maxhp 8733 (non-players) + status_get_hpbonus | 35 | 428 | 428 |
| `maxsp` | status_calc_maxsp 8752 (non-players) + status_get_spbonus | 12 | 311 | 311 |
| `mdef` | status_calc_mdef 7944 | 17 | 224 | 224 |
| `mdef2` | status_calc_mdef2 8002 | 6 | 172 | 172 |
| `mode` | status_calc_mode 8895 | 1 | 222 | 222 |
| `mres` | status_calc_mres 8675 (") | 3 | 155 | 155 |
| `patk` | status_calc_patk 8569 (RENEWAL-only caller) | 11 | 183 | 183 |
| `pow` | status_calc_pow 7196 (4th-job stat, RENEWAL-only caller) | 2 | 147 | 147 |
| `res` | status_calc_res 8645 (") | 7 | 171 | 171 |
| `script` | the Script: of every active status (status_calc_pc_sub 4213-4221) | - | 537 | 537 |
| `smatk` | status_calc_smatk 8608 (") | 10 | 179 | 179 |
| `sp_fix` | status_get_spbonus(FIX) 3329 | - | 240 | 240 |
| `sp_rate` | status_get_spbonus(RATE) 3329 | - | 240 | 240 |
| `speed` | status_calc_speed 8039 | 63 | 1420 | 1420 |
| `spl` | status_calc_spl 7256 (") | 2 | 147 | 147 |
| `sta` | status_calc_sta 7216 (") | 2 | 147 | 147 |
| `state` | status_calc_state 5592 | - | 500 | 500 |
| `str` | status_calc_str 6773 | 24 | 241 | 241 |
| `vit` | status_calc_vit 6921 | 19 | 226 | 226 |
| `watk` | status_calc_watk 7360 | 32 | 423 | 423 |
| `wis` | status_calc_wis 7236 (") | 2 | 147 | 147 |
| **total** | | **319 distinct** | **13,100** | **13,100** |

* 319 statuses are defined; **all 319** appear in at least one case (435 statuses in all, the others are state / script carriers). Match rate by function and by status:
  `docs/STATUS_GOLDEN_REPORT.txt` (`python3 -I tools/run_tests.py --luau X --status-report --full`).
* Contribution coverage: 603 of the 606 contributions fire in at least one golden case. The three that never do are homunculus-only (`DEFENCE.def@7802`, `OVERED_BOOST.def@7854`,
  `PYROCLASTIC.batk@7332`, `bl->type == BL_HOM`).
* Mutation check (`tools/mutation_status.py`): 45 mutants of the Luau (flooring division, reversed order, dropped clamp / narrowing / override stage, wrong constant, swapped
  group max, ...) -- **45 / 45 killed**; the first run had 5 survivors, which led to the cases for odd negative DEF / MDEF (flooring), the weapon level 5 case, packed negative `val3`,
  the int16 overflow chain cases, and the Dancing / Longing state cases. (A logical shift in `ops.sar` is an equivalent mutant: every use masks with `& 0xFF`.)
* Plain Lua (lupa) and Luau give the same results; `script-host`'s tests (192 pass) and `rathena-sim conformance` (suite 106 pass / 76 expected-failure) run the package unchanged.

## 3. Not covered, and why

* **Player base values**: `status_calc_pc_sub` (equipment, job tables, skills -> base status), `status_calc_maxhp_pc` / `maxsp_pc` / `maxap_pc`, `status_base_amotion_pc` (player ASPD / `amotion`,
  `adelay`), `status_calc_misc`, regen -- they read the job database and the `map-rules` library (BR-MAP-004 / 019 / 020), not statuses. The chain refuses the player flags that would reach them (AGI, DEX
  -> ASPD; VIT -> MAXHP; INT -> MAXSP). Max HP / SP of players is covered only through its status and skill part (`status_get_hpbonus` / `spbonus`: the `hp_fix` / `sp_rate` ... cases).
* **Other unit kinds**: homunculus, mercenary, elemental, pet, NPC branches (`BL_HOM` entries above, the homunculus ASPD / base ATK / DEF / MDEF terms of `status_calc_bl_main`).
* **Damage motion** (`SCB_DSPD`) of players.
* The **4th-job stats** (`pow sta wis spl con crt`, `patk smatk res mres hplus crate`, `maxap`) are compiled into a PRE build but `status_calc_bl_main` calls them only `#ifdef RENEWAL`: they are
  checked as functions (the cases above), a PRE server never computes them and the statuses that feed them are not in `db/pre-re`.
* Status **start / end** (gating, resistance, overlap, exclusion, strip, 052 .. 064, 076), the periodic **tick effects** (072), cast / delay / SP cost (070) and the statuses' effects in `battle.cpp` /
  `skill.cpp`: other rules. `states` covers `status_calc_state` only (no start / end plumbing); `scripted` covers `bonus` .. `bonus5`, locals, `if`, `getstatus` -- the full script language is the legacy-npc unit's.
* Values whose products exceed int32 (undefined behaviour in the legacy); the replay's ctx errors on them.
* `status_calc_pc_sub` recursion guard (077) is source-derived (`recalc.luau`), there is no golden for it.

## 4. Legacy quirks found (all reproduced; `tests/acceptance/status_quirks_test.luau`)

1. PRE `def` / `mdef` are `signed char` (`config/const.hpp:53-55`): `cap_value(def, CHAR_MIN, CHAR_MAX)` = -128..127 (renewal: int16).
2. Early returns are **not clamped**: Berserk / Eternal Chaos make `def2` 0 although PRE clamps `def2` to >= 1 (`status.cpp:7887-7890` vs `7884`, `7933`); `Def Set` returns `val1` through a
   `signed char` (40000 -> 64); `Overed Boost` returns `val2` through an `int16` (`7648-7649`, 40000 -> -25536); `Mdef Set` likewise.
3. `status_data` stores the stats as `int16` while the `status_calc_*` functions return `uint16`: a calculated 32768..65535 is stored negative (`status->str = status_calc_str(...)`, `5885`); katar doubling
   `status->cri *= 2` (`6163`) wraps the same way.
4. `maxhp += hpbonus_fix` adds an `int32` to a `uint64` (`8737`): a negative flat sum larger than the max HP (Marionette -1000 on < 1000 HP) wraps and `cap_value(.., 1, UINT_MAX)` returns UINT_MAX;
   rate 0 gives 1.
5. Truncation toward zero of negative values: `def /= 2` (Stone, Freeze, SKE) and `mdef += 25 * mdef / 100` (`7813`, `7971`).
6. `status_calc_pc_sub` resets its recursion counter with `calculating = 0` at the normal exit instead of decrementing it (`5012`): an inner call that finishes un-counts its callers.
7. Adrenaline shares a `status_change_start` block with Overthrust whose comment says "Power increase" for the value that is the ASPD (`11611-11627`).
8. 12 statuses contribute to an attribute their database `CalcFlags` do not trigger (even after the STR -> BATK, VIT -> DEF2 ... propagation), so a partial recalculation misses them:
   `ANGRIFFS_MODUS.watk`, `BEYONDOFWARCRY.cri`, `BURNING.mdef2`, `CAMOUFLAGE.def/def2`, `CROSSBOWCLAN.dex`, `DEFSET.def2`, `MDEFSET.mdef2`, `PARTYFLEE.flee`, `PYROCLASTIC.watk`, `TIDAL_WEAPON.watk`,
   `_INVISIBILITY.element_lv`, `_AUTOSHADOWSPELL.matk` (audit test in `status_quirks_test.luau`).
9. `CalcFlags: All: true` sets the whole bitset, bit 0 (`SCB_NONE`) included.
10. The migrated status ids drop the leading underscore of the database names (`status/invisibility` is `_INVISIBILITY`), so a name -> id round trip needs the record table (`st_records.py` maps them).

## 5. Regenerating and checking

    # unit tests (main 106 pass / 76 expected-failure, extra 92 pass)
    python3 -I tools/run_tests.py --luau $LUAU
    # the golden (needs the legacy build, ~5 min once): see conformance/golden_gen/README.md "Status golden"
    # match rate by function / category / status, contribution coverage
    python3 -I tools/run_tests.py --luau $LUAU --status-report --full
    # 45 mutants of the Luau, each must fail the replay
    python3 -I tools/mutation_status.py --luau $LUAU

BR_RULESET.md corrections found while doing this: `docs/BR_RULESET_CORRECTIONS.md`. Field names: `docs/STATUS_FIELDS.md`.
