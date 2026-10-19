# Corrections to analysis/rathena/BR_RULESET.md (found by the status golden, PRE)

BR_RULESET.md was not edited (read-only for this work). Replacement text, with the evidence (status.cpp line numbers of the legacy tree in this repo):

**BR-RULESET-065** (replace "Result is clamped 0..65535"):
"Stat contributions (STR..CRT). Applied in code order as flat adds or `x*%/100` terms (truncating toward zero). Statuses that early-return (Harmonize) end the function with
`cap_value(.., 0, USHRT_MAX)` of their own expression. The functions return `uint16`, but `status_data` stores the stats as `int16`: a result of 32768..65535 is stored negative
(status.cpp:5885). A schema needs add, mul and override with a defined order: the order is the source order of the checks, not a type order (Blessing halves the value built by the adds before it)."

**BR-RULESET-066**:
"... PRE `def` / `mdef` are `signed char` (const.hpp:53-55), clamped -128..127 (RE: int16). PRE `def2` is clamped to >= 1 only on the normal path: the early returns
(Berserk, Eternal Chaos [7887-7890], Def Set, Mdef Set, Overed Boost [7648-7649]) return their value narrowed to the return type and are NOT clamped (Berserk gives def2 0;
Def Set 40000 gives 64 through `signed char`). Negative def is divided by truncation (`def /= 2`, `mdef += 25*mdef/100` [7813, 7971])."

**BR-RULESET-067**: "...min 40" applies to the net rate before cart / Paralysis / Steel Body / Defender; a Mado Gear unit ignores the haste/slow groups; the max-groups are first-wins chains (mount, slow-force, slow, haste, quicken), not one max over all."

**BR-RULESET-068 (PRE)**: "aspd_rate = base - best quicken (max over the family, bow exclusion only for Assassin Cross of Sunset), then Berserk, potions and slows are added; the result is clamped. Adrenaline shares a status_change_start block with Overthrust whose comment calls the ASPD value 'power increase' (11611-11627)."

**BR-RULESET-069**: "Rated M" -> H for PRE: all 36 functions (str..crt, batk, watk, cri, hit, flee, flee2, def, def2, mdef, mdef2, speed, aspd_rate, element(+lv), atk_element, mode, maxhp, maxsp, hp/sp
fix/rate, matk) were compared with the real code, 13,124 cases. PATK / SMATK / RES / MRES / HPLUS / CRATE / POW..CRT and MAXAP exist in a PRE build but `status_calc_bl_main` calls them only under `#ifdef RENEWAL`.
Max HP: `maxhp += hpbonus_fix` adds an int32 to a uint64 (8737): a negative sum larger than the max wraps, and the clamp then returns UINT_MAX; rate 0 gives 1.

**BR-RULESET-071**: add "the status record's CalcFlags do not cover every contribution: 12 statuses change an attribute whose flag they do not set (ANGRIFFS_MODUS.watk, BEYONDOFWARCRY.cri, BURNING.mdef2,
CAMOUFLAGE.def/def2, CROSSBOWCLAN.dex, DEFSET.def2, MDEFSET.mdef2, PARTYFLEE.flee, PYROCLASTIC.watk, TIDAL_WEAPON.watk, _INVISIBILITY.element_lv, _AUTOSHADOWSPELL.matk), so a partial recalculation misses them until the next full one."

**BR-RULESET-075**: "Script statuses run through the item-script engine; their `bonus*` calls add to the same `status_data` fields as equipment (observed at pc_bonus..pc_bonus5)."

**BR-RULESET-077** (reentry): "`status_calc_pc_sub` resets its recursion counter with `calculating = 0` at the normal exit instead of decrementing it (status.cpp:5012): an inner call that completes
un-counts its callers, so the depth-10 limit is not a strict nesting count."

**BR-RULESET-051** (Q5/Q-F): the status DB never names `val1..val4`; names per status are in `docs/STATUS_FIELDS.md` (319 statuses, 789 of 1,276 slots unused). `CalcFlags: All: true` sets bit 0 (`SCB_NONE`) too.
Also: 48 statuses that the PRE stat code reads are not in `db/pre-re/status.yml` (271 present).
