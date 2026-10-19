# Corrections and additions to `analysis/rathena/BR_RULESET.md` (renewal branches)

Found while replaying the renewal weapon attack against the real legacy code (8,858 golden cases + 153,708 fuzz cases, `docs/ARCHITECTURE_PROOF.md`). `BR_RULESET.md` itself is **not** edited; the
text below is the proposed replacement. Evidence column: the golden category that pins the statement (`conformance/golden/weapon_re.jsonl.gz`) and the legacy line.

## Corrections of statements that the code contradicts

**BR-RULESET-019 (RE order).** Replace the cell by:
> Prelude as PRE (lucky dodge, crit roll, hit roll). Then, only when the attack hits: ATK parts (players, `battle_calc_damage_parts`; monsters: `battle_calc_base_damage`) -> attacker card fix on
> weapon and equip ATK -> **target card fix on every part when the target is a player** (`NK_IGNOREELEMENT` variant for status and mastery ATK) -> status ATK bonuses on the parts (EDP) -> sum of the parts
> -> P.ATK -> mastery ATK -> `crit_atk_rate` or `non_crit_atk_rate`, then short and long attack rate -> skill ratio (status ATK% is its first term) -> constant addition -> Adv. Katar Mastery -> RES ->
> DEF and post-DEF (guard `damage + damage2 != 0` evaluated once, before DEF) -> skill ATK bonus. Always (also on a miss): element step (players already applied it per part; monsters unless
> `attack_attr_none`) -> x1.4 crit with `crit_def_rate` -> flat additions -> target card fix on the **total, only for non-player attackers** -> div fix -> hands -> target modifiers and GvG/BG -> final.

The sentence "card fix per part" was right, the target-side application per part (also for player attackers) was missing. Evidence: probes of all 8,858 cases (`pipeline.ORDER.weapon`); `battle.cpp:5502-5560`.

**BR-RULESET-034 (RE crit).** Replace by:
> RE crit damage is applied **after the element step**: players `floor((float)damage * (1.4f + 0.01f * crate))` per lane (also the left hand), computed in float32 (`1.4f + 0.01f*crate` is rounded to
> float32 first, and so is the product); non-players `floor((float)damage * 1.4f)`. The target's `crit_def_rate` then reduces the result. A critical hit does **not** ignore DEF in RE
> (`attack_ignores_def` has the crit test only under `#ifndef RENEWAL`, `battle.cpp:3341-3343`). Monsters multiply a critical base damage by 14/10 already inside `battle_calc_base_damage` (`battle.cpp:2635`), so a
> monster crit is x1.4 twice with a truncation after each (legacy defect, reproduced). In PRE crit still means maximum damage, no multiplier.

Evidence: `crit_player` 250, `crit_mob` 200, `crit_def_applies` 60, `element_mob_crit` 120, `element_pc_crit` 80; worked cases 1400 (crate 0) and 1770 (crate 37, float32).

**BR-RULESET-025 (RES).** Replace by:
> If the target's `res > 0`, the total damage is non-zero and the skill does not use the simple defense formula: `ignore = min(sum, max_res_mres_ignored)` where `sum` is, for player attackers, the
> `ignore_res_by_race` bonuses of the target's race and of RC_ALL, plus `val2` of Telum and of Potent Venom for any attacker (`max_res_mres_ignored` default 50). `res' = (int16)(res - ignore * res / 100.0)`.
> Each hand: `damage -= (int64)((double)(float)(res' / (res' + 400.0)) * 80.0 / 100.0 * damage)`, truncation toward zero (the ratio is narrowed to float32 first). It is applied after the ratio and
> constant addition and before DEF. E-2 holds (840 and 912). MRES for magic is not verified.

Evidence: `res` 200, `res_nonpositive` 40, E-2 cases; `battle.cpp:5611-5641`.

**BR-RULESET-010 (RE weapon ATK).** Replace by:
> Per hand, with `watk = weapon ATK + refine ATK` (uint16) and `ATK = weapon base ATK`: `variance = 5.0f*ATK*weaponLv/100.0f`, `bonus = ATK*STAT/200.0f` (float32; STAT = DEX for bow, instrument, whip and
> gun types, STR otherwise; the summoner Soul Attack exception is not covered). `min = max(0, (int32)(watk - variance + bonus))` narrowed to uint16, `max = min(65535, (int32)(watk + variance + bonus))`.
> The roll is `rnd_value(min, max)` and **always draws, even for a zero-width range**; a critical hit or Maximize Power takes `max` without a draw. This happens only when an item occupies the hand
> (`equip_index >= 0`): **a shield in the left hand makes the left roll (0..0) draw too, and so does its over-refine**. Then `+ rnd() % overrefine + 1`, then `weapon_damage_rate`, then the size modifier
> of that hand's weapon type (so the size modifier applies to the over-refine bonus too, unlike PRE where `+ batk` and the over-refine come after it).

The example E-9 ([120, 160]) is right for refine 0; with refine R the interval is [120+R, 160+R] (the variance does not include refine). Evidence: `weapon_roll` 200, `shield_draw` 100, `weapon_atk_u16` 60, `overrefine_wdr` 80, `bow_gun_dex` 120; `battle.cpp:2447-2499`.

**BR-RULESET-032 (attr_recover).** The note "it is not a ratio clamp" is true for the final element stage (monsters; `battle_attr_fix(..., flag=1)`) but **false for the ATK parts**: `battle_calc_damage_parts` calls
`battle_attr_fix` with `flag = 0`, and with `attr_recover == 0` a negative table ratio is clamped to 0 *in the ratio* (`battle.cpp:709`), so a part against an immune element becomes 0 and is not "cut later".
Evidence: `element_negative_ratio` 150, `element_mob_negative` 120.

**BR-RULESET-002 (range).** "Range is long for arrow or Soul Attack basic attacks" omits monsters: a non-player attacker whose weapon `range > 3` is `BF_LONG` (`is_skill_using_arrow`, `battle.cpp:2945-2948`), so its
long-range card rates, long defense rates, Adjustment / Defender / Smoke Powder branches and GvG long rate apply. Evidence: `mob_ranged` (flag 577 for range 4 and 9, 529 up to 3).

**BR-RULESET-006 (hit).** RE hit rate = `0 + hit - flee` (+ `arrow_hit` for arrow attacks) `+ Vulture's Eye level` (RE only) `+ hitrate * 2 * WeaponryResearch / 100` (both modes, the product is a signed int division)
then the clamp. The text lists Vulture's Eye but not the order; the clamp is last. Evidence: `hit_skills` 100.

**BR-RULESET-027 (DEF).** Add for RE: hard DEF is `int16` (`status_get_def` caps to +-32767; PRE `signed char`), a crit does not skip DEF, the soft DEF of players and monsters is `def2` **without any RNG**
(PRE: two different spreads), `def1 == -400` is read as `-399`, the crowd penalty applies to the hard DEF unless the target has Steel Body, `def2` is floored at 1 whenever the crowd penalty is enabled for the target type
(even below the threshold), and `weapon_defense_type` moves `def1 * type` into the soft DEF. Evidence: `def_edge` 270, `crowd_penalty` 150, `crowd_steelbody` 40, `def_weapon_defense_type` 60.

**BR-RULESET-039 (target modifiers, RE).** Confirmed by `target_status_map` (350): Assumptio gives **no** damage reduction in RE (it raises DEF in `status_calc`); Steel Body is `damage > 10 ? damage / 10 : 1`; Kaite
x4 only when `(flag & (BF_SHORT|BF_MAGIC)) == BF_SHORT`; Raid +30 % (+15 % vs boss) comes before Sound of Destruction; Ground Gravity +10 % for weapon or magic. Mob `DamageTaken` (`damage * pct / 100`, min 1) is applied after
all of them (`damagetaken` 60).

## Additions (rules the text leaves implicit)

**BR-RULESET-009 / 011 (ATK parts).** Exact algebra, all verified: `statusAtk = attr_fix(batk, Neutral, flag 0)`, right hand doubled after the element step (left not doubled); `equipAtk = attr_fix(eatk + arrow ATK, element)`,
arrow ATK only for an arrow attack with ammo equipped and not a cannonball (also for plain attacks); `percentAtk = (weaponAtk + equipAtk) * atk_rate / 100` (truncating); `masteryAtk` has no element; left values exist only for a
left-handed attack except `weaponAtk2`, which is always rolled. Order of RNG draws: right weapon roll, right over-refine, left weapon roll, left over-refine. Monsters use `battle_calc_base_damage` (draw `rnd() % (max - min)`, `+ batk`, crit x14/10).

**BR-RULESET-014 (RE).** In RE `crit_atk_rate` is `floor((float)(damage * rate / 100))` added after P.ATK and masteries; `non_crit_atk_rate`, `short_attack_atk_rate`, `long_attack_atk_rate` are plain `ATK_ADDRATE` of **any sign**
(PRE only adds positive crit / non-crit rates), applied in that order on the running sum (each step truncates). P.ATK is `floor((float)(damage * (100 + patk) / 100))`: for damage above 2^24 the float conversion loses bits. Evidence: `patk_float32` 150, `atk_rates_short_long`, `rates_with_ratio`.

**BR-RULESET-016 / 015.** The ATK% cap 0..65535 applies to the ATK% term alone, before the ratio terms are added (`status_atk_cap`); Berserk is +200 in RE (confirmed).

**BR-RULESET-020 / 021 (RE).** The attacker card fix is the PRE function minus the Adv. Katar step and the short / long steps; the target card fix runs per ATK part for player targets with the element and defending-element steps
skipped for status and mastery ATK. Evidence: `cards_mastery_status` 100, `cards_vs_player_pc` 150.

**BR-RULESET-024 (RE masteries).** Weapon Research (+2 per level) counts for every weapon, Axe Mastery adds +3 per level to one-hand swords before the Sword Mastery fall-through, Demon Bane is `trunc(lv * (baseLv / 20.0 + 3.0))` in **double**
against undead-element or demon monsters only. Evidence: `mastery_atk` 150.

**BR-RULESET-102 (legacy defect, new).** `indexed_bonus.weapon_damage_rate` has 16 slots (`pc.hpp:577`) but is indexed by the weapon type (0..23): types 16..23 (katar, revolver, rifle, gatling, shotgun, grenade, huuma, 2h staff)
read `indexed_bonus.arrow_addele[type - 16]`, the next member. For gun users a card that adds Water element damage to arrows therefore also raises the weapon damage. Reproduced (`gun_wdr_alias` 60); needs the `legacy_quirks` flag.

## Status layer: corrections and additions (BR-RULESET-051, 065 to 077)

Found while replaying the declarative status layer against the real `status_calc_*` functions (`conformance/golden/status_re.jsonl.gz`: 31,470 cases, 85,178 outputs; evidence column = the golden category or
worked example, the legacy line is `src/map/status.cpp` unless stated). `BR_RULESET.md` itself is **not** edited.

**BR-RULESET-051 (record, Q-F).** Confirmed: 1,018 renewal records, 697 shared with the 699 pre-renewal ones (462 identical, 66 %). The status database never names `val1..val4`; `docs/STATUS_FIELDS.md` does, for the 321
statuses with behaviour (247 have at least one named slot, 385 named slots). Two slots are shared by two meanings: `SC_BEYONDOFWARCRY.val3` is both the MaxHP reduction and an addition to the critical value (7550, 3281), and
`SC_CHASEWALK.val3` is a speed value whose sign selects a different branch (8085). One slot is never assigned: `SC_CAMOUFLAGE.val3` (read by 7843 and 7927 as a DEF percentage; `status_change_start` 11970 sets only `val4`,
`camouflage.cpp:29` passes only `val1`), so Camouflage never lowers DEF. A save-format migration is not needed: names are an overlay on the same four integers.

**BR-RULESET-065 (stats).** The list is not STR..CRT only: it is the six primary stats plus the six renewal trait stats (POW STA WIS SPL CON CRT, one or two statuses each), all capped 0..65535. Evidence: `single`,
`pair`, `override`. Additions: (a) Harmonize is the FIRST statement of every primary-stat function and returns the capped `stat - val2`; (b) `SC_CURSE` returns 0 for LUK (7144) before any later term, after Harmonize;
(c) `SC_BLESSING.val2 == 0` (demon / undead targets) HALVES the stat with `x -= x / 2` instead of adding `val2` (worked example "Blessing halves the stat", 81 -> 41, 80 -> 40); (d) Improve Concentration adds
`(stat - itembonus) * pct / 100`, where `itembonus` (`val3` for AGI, `val4` for DEX) is the player's item bonus captured at start, and is VOID while Quagmire is active (6857, 7069); (e) Marionette / Spirit lend three stats
packed in `val3` / `val4` as `STR << 16 | AGI << 8 | VIT` (arithmetic shift of an int32, so a negative value would sign-extend into bits the `& 0xFF` removes); (f) a rate term multiplies the running value, so the
order of statements matters (`stat order` worked example: +3 Cheer Up before or after +20 % Full Throttle changes the result), which is why every contribution carries the legacy line as its order key.

**BR-RULESET-066 (DEF / DEF2).** Confirmed: Berserk and Eternal Chaos (RE) return 0, Keeping returns 90 (DEF only), Steel Body gives NO DEF in renewal, DEF2 may be negative. Corrections / additions: Eternal Chaos
returns 0 for DEF2 as well (7889), Defset returns `val1` narrowed to int16 (`defType`): a value above 32767 wraps (33000 -> -32536, worked example), Nyanggrass returns 0 for players and `def / 2` for everyone else
WITHOUT the int16 cap (7786); Provoke, Strip Shield and Poison lower the hard DEF of NON-players only but Provoke and Poison lower DEF2 for everyone (7825, 7912, 7817, 7908); the Renewal Angelus term of DEF2 is
`vit / 2 * val2 / 100` of the unit's battle VIT, not of DEF2 (7900); MDEF2: Berserk 0, `SC_SKA` 90.

**BR-RULESET-067 (speed).** E-4b is exactly right (150 with Quagmire and Increase AGI -> 187, `worked`). The model is more detailed than the rule text: `speed_rate = 100 - mount + slow - haste`, with `mount` =
Fusion 25, or (players) riding / dragon 25, Warg rider `15 + 5 * lv`, rental mount `rental_mount_speed_boost` (8073-8085); `slow` = the maximum of ~30 statuses, except two special FIRST branches that replace the
group (Hiding with Tunnel Drive `120 - 6 * lv`, Chase Walk with a negative `val3`), and Ensemble Fatigue before the player's dance penalty `500 - (40 + 10 * bard_link) * lesson`; the slow group is capped at 150 under Marsh
of Abyss; `haste` = the maximum of ~30 statuses, the Dodge passive (TF_MISS) of the Assassin line and the negative item speed; the floor 40 applies after the haste; Free Cast or Exceed
Break replace the whole rate by `175 - 5 * lv` / `160 - 10 * lv` and skip the groups AND the floor. After the rate: cart `speed += speed * (50 - 5 * Pushcart lv) / 100`, Paralysis +50 %, `speed * rate / 100`,
Steel Body = 200, Defender / Armor `max(speed, 200)`, Walk Speed `speed * 100 / val1`, cap 20..1000. Madogear ignores all statuses: `+50 - 10 * license` % (-25 at level 5), -25 with Acceleration. `permanent_speed` and
`sc == nullptr` skip everything. Evidence: `speed_facts`, `facts`, `worked`.

**BR-RULESET-068 (ASPD, RENEWAL_ASPD).** Correction of the names: the quicken group of 7 is Two-Hand Quicken, One-Hand Quicken, Mercenary Quicken, **Adrenaline Rush** (`SC_ADRENALINE`) and Spear Quicken; **Full
Adrenaline Rush** (`SC_ADRENALINE2`) is 6 and Fleet 5 (8270-8274); none of them while Quagmire is active; Assassin Cross of Sunset ADDS its `val2` when `bonus < val2` (no weapon test in renewal); Madness Cancel 20 and
Berserk 15 are assignments when the running bonus is lower; of the ASPD potions only the strongest present one (3 > 2 > 1 > 0) adds its `val1` (8306). The percent group is a plain sum narrowed to int16, and the
player's Musical / Dancing Lesson and Plagiarism levels are added only when the unit has some status: `if (sc == nullptr || sc->empty()) return 0` comes first (8262; the golden category `facts` found it). `aspd_rate`
(1000 = 100 %, used by non-players) takes the best of the quicken family through `max < val` comparisons with Star Comfort as an unconditional assignment first. `fix_aspd` returns a fixed value under Overed Boost.

**BR-RULESET-069 (the other stats).** (a) Maximum HP / SP / AP are `uint64` computations: `maxhp += (int32 bonus)` wraps modulo 2^64 when a negative fixed bonus (Marionette -1000, a negative `bonus_hp`) exceeds the
value, and the final `cap_value(maxhp, 1, UINT_MAX)` then returns UINT_MAX (a 500 HP monster under Marionette has 4,294,967,295 HP; worked example, category `wrap`: 259 of 480 cases). The rate bonus is capped at -100
before `rate += 100`. These functions read the status set themselves (`status_get_hpbonus`), so `sc == nullptr` does not remove statuses from them. (b) FLEE: for players the GvG / battleground penalty is applied before
anything else, even without a status; Poison Mist returns 0 and Overed Boost returns its `val2` ahead of every term; fixed terms precede the rate terms except Saturday Night Fever, which subtracts a fixed amount in
the middle of the rate block. (c) Minimums: CRI 10, HIT 1, FLEE 1, FLEE2 10, P.ATK / S.MATK / RES / MRES / H.PLUS / C.RATE 0..32767. (d) The flat MATK of Transcendence is added before the `sc == nullptr` test.

**BR-RULESET-071 (overrides).** The first applicable statement in code order returns: defense element (Freeze / Crystal Armor option water, Stone / Strong Protection earth, ... Elemental Change, Shape Shift), element
level (1 for most, `val1` for Elemental Change), attack element (Enchant Arms first, then water, earth, fire, wind, poison, holy, dark, ghost, tidal), mode (`MODECHANGE`: replace the bits under `MD_MASK` with `val2`,
add `val3`, remove `val4`, each only when non-zero). The unsigned-char results wrap (`val1` above 255).

**BR-RULESET-073 (states).** `status_calc_state` is a union over the active set: 75 records list states, 9 of them conditional (`NOMOVECOND`, `NOCASTCOND`, `NOCHATCOND`, `NOPICKITEMCOND`, `NODROPITEMCOND`,
`NOCONSUMEITEMCOND`: Hiding, Gospel, Dancing, Fear, Camouflage, Crystallize, Wink Charm, Oblivion Curse, No Chat). A conditional state without a condition is never restrictive, and an unconditional flag on the same
status makes the conditional one dead code (Dancing: `NOATTACK` is unconditional, so the attack rule of the lambda is never consulted). Pick-up, drop, equip, unequip, consume, death penalty and interact apply to
players only. Evidence: `states` (6,138 cases, every sc_type) and 13 named worked examples.

**BR-RULESET-075 (scripts).** 168 renewal statuses carry a script, all in the subset `bonus[2..4] name, args;` / `.@v = expr;` / `if (!getstatus(SC_X)) { ... }` with `getstatus(SC_X, n)` reading the n-th val of a
status: they are status contributions expressed as bonus deltas through the SAME val slots. `scripts/status/bonus_scripts.luau` compiles them to delta lists; applying the deltas is the item-bonus vocabulary
(BR-RULESET-098..110), not covered here.

**BR-RULESET-077 (recalculation).** (a) The re-entrance guard is a static counter that every call raises (`if (++calculating > 10) return -1`, 3784), that a call which runs to the end resets to ZERO (5012), and that the
outer call tests after each item script (`if (!calculating) return 1`): a nested recalculation that completes makes the outer one abort; a refused call leaves the counter raised. (b) Calc flags select the recomputation
and `status_calc_bl_main` cascades them (STR -> BATK, AGI -> FLEE / DEF2 / ASPD, VIT -> DEF2 / MDEF2 / MAXHP, LUK -> BATK CRI FLEE2 and, in renewal, MATK HIT FLEE, ...). (c) **Data gaps in `db/re/status.yml`:** 14
contributions of 11 statuses are not covered by their own calc flags even after the cascade, so starting or ending the status does not recompute that stat until something else does: Angriffs Modus (WATK), Attack
Stance (PATK, SMATK), Beyond of Warcry (CRI), Burning (MDEF2), Camouflage (DEF, DEF2), Crossbow Clan (DEX), Defset (DEF2), Mdefset (MDEF2), Party Flee (FLEE: no calc flag at all), Tidal Weapon (ATK_ELE, WATK),
Invisibility (DEF_ELE for the element level); Marionette's -1000 HP is reachable only through VIT for players (a monster has no MAXHP cascade from VIT). Test: `tests/acceptance/status_define_test.luau`.

## Latent gaps found in the pre-renewal package (shared legacy code the PRE golden set never exercised)

* `battle_add_weapon_damage` is shared by both modes and applies `weapon_damage_rate`; `ruleset-prerenewal/scripts/damage/weapon.luau` implements only the over-refine part.
* Monster `rhw.range > 3` makes a basic attack ranged in PRE too (`is_skill_using_arrow`); the PRE `S.flag` only looks at the player arrow flag. The PRE hook always sets range 1.
* Weaponry Research raises the hit rate in PRE as well (`is_attack_hitting` has no `#ifdef`); `arrow_ele` and mob `DamageTaken` are also shared. None is in the PRE input space.
* The PRE golden hook toggles `battle_config` knobs directly; the delegated map-rules calls (`crowd_flee`, `crowd_def`) read a snapshot (`rules_bridge.cpp`) that is only refreshed by `rules_config_refresh`. PRE never toggles those
  knobs, so nothing is wrong today; the renewal hook calls the refresh and covers `agi_penalty_type` / `vit_penalty_type` 0, 1 and 2.
