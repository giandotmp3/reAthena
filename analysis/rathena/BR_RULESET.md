# BR-RULESET: rules a Luau script ruleset must own (not in AI_NATIVE_SPEC.md)

Scope: `src/map/battle.cpp`, `status.cpp`, `skill.cpp`, `pc.cpp`, `script.cpp`, `mob.cpp`, and the `db/` schemas. All paths are under `/home/user/reAthena`. I did not write any files.

Priority scale:
- **P0**: needed for any playable combat parity.
- **P1**: needed for full class coverage.
- **P2**: tail or niche.

Mode column: **B** = both modes (the rule text names any constants that differ), **RE** = renewal only, **PRE** = pre-renewal only.

Confidence column: H = High, M = Medium. No rule is rated Low. Open questions are in section 7.

## 0. Findings that change how to read this list

1. **Overlap with the existing spec.** The base stat and defense formulas are already BR-MAP-004..020 (MaxHP, MATK, HIT/FLEE/CRI, soft DEF/MDEF, crit chance, hit chance, crowd penalties, defense formulas, element table, size table, ASPD). Cast time and delay are BR-MAP-051..053. The rules below cover only what those do not: modifiers, ordering, status effects, skill lifecycle and item bonuses.
   - The source already carries `rules::combat::*` and `rules::skills::*` calls, with comments like `BR-MAP-014 (map-rules)`, for hit, crit, defense and cast. Those are pure-function seams, so they are good native-core candidates.
2. **Hook points already exist in the legacy code.**
   - `src/map/skills/` has about 1,297 per-skill `SkillImpl` classes. The hook set is `castendNoDamageId`, `castendDamageId`, `castendPos2`, `calculateSkillRatio`, `modifyHitRate`, `applyAdditionalEffects`, `applyCounterAdditionalEffects`, `modifyDamageData` and `modifyElement` (`src/map/skills/skill_impl.hpp:7-62`). It is the obvious template for the Luau skill contract.
   - `ScriptHost` (`src/map/script_host.hpp:86-102`) is already the single entry point for item, equip and combo scripts.
   - `db/*/status.yml` already has a declarative schema (CalcFlags, States, Flags, Fail, EndOnStart, EndReturn, EndOnEnd, Script).
3. **Renewal is not one switch.**
   - `src/config/renewal.hpp:37-74` has independent sub-flags: RENEWAL_CAST, DROP, EXP, LVDMG, ASPD and STAT.
   - Pre-renewal and renewal also have separate DBs: `db/pre-re` and `db/re`. `skill_db` has 1,228 vs 1,635 entries, and `status.yml` has 699 vs 1,018.
   - A ruleset package manifest must therefore carry sub-flags, not a boolean.
4. **Scale of the status schema.**
   - `status_calc_*` (status.cpp 6773-8750) references 269 distinct SC_ types. `battle_calc_damage` references 52 more. `battle.cpp` as a whole references 240.
   - Status values are untyped `val1..val4`, whose meaning is defined per status in code. A schema must name them.
5. **Possible legacy defect.** `battle.cpp:1803-1804` puts the `SC_BURNT` "+666% vs fire" line inside the `SC_DARKCROW` block, so it only fires while the target also has Dark Crow. See Q1 below.
6. **Architecture conflict to decide.** `analysis/rathena/REIMAGINED_ARCHITECTURE.md` (approved HITL #2) chose an in-place C++20 refactor and keeps the legacy NPC script VM (K2). It has no Luau layer. The vision in this task diverges, so it needs an ADR.
7. **Injection check.** I saw no instruction-shaped text aimed at automated analysis in the files read. `// BR-MAP-0xx (map-rules)` comments are provenance only. I confirmed the rules against code, not comments. No credentials were encountered.

## 1. Damage pipeline: ordered stages

**Entry point**
- `battle_calc_attack` dispatches to `battle_calc_weapon_attack`, `battle_calc_magic_attack` or `battle_calc_misc_attack`, then normalises misses (BR-RULESET-001).

**Weapon path**
- Renewal and pre-renewal differ in the order of stages, not just in constants.
- Stage names, in order:

| # | Stage | PRE | RE |
|---|---|---|---|
| S0 | Init, lucky dodge, multi-hit roll, crit roll, hit roll | same | same |
| S1 | Base attack | `battle_calc_base_damage` (min = DEX-based, max = weapon ATK; size fix; + batk) | ATK parts: status / weapon / equip / mastery / percent, each with its own element and card fix |
| S2 | Attacker card and SC multipliers | after masteries, on the whole damage | on parts, before summing |
| S3 | Skill ratio | multiply, then constant add | sum parts, P.ATK, crit_atk_rate, then ratio, then constant add |
| S4 | RES | none | % reduction before DEF |
| S5 | Defense | `100−DEF`% then `− vitDEF` (random) | `ATK×(4000+DEF)/(4000+10·DEF) − softDEF` |
| S6 | Post-defense | refine bonus, min 1, masteries | min 1 |
| S7 | Skill ATK bonus | before DEF | after DEF |
| S8 | Element | `battle_attr_fix`, then star crumbs / EDP / element endow | PC element already applied per part; ×1.4 crit here |
| S9 | Target card fix | for all targets that are PCs | only for non-PC attackers |
| S10 | Div-fix, hands | same | same |
| S11 | Target and caster SC modifiers (`battle_calc_damage`), GvG/BG/PK rates | same | same |
| S12 | Final modifiers, reflect | same | same |

**Magic path**
- Order: MATK roll, split, skill ratio, skill ATK bonus, MDEF, element, card fix (pre-renewal), then the `battle_calc_damage` stage.
- In renewal the card fix runs first, then S.MATK, then the ratio (BR-RULESET-044).

**Misc path**
- Fixed formulas per skill (BR-RULESET-047).

## 2. Damage pipeline rules

| ID | Rule | Citation | Mode | Pri | Conf |
|---|---|---|---|---|---|
| BR-RULESET-001 | The calculator is chosen by attack type (weapon, magic, misc). If total damage < 1 the result is MISS, `div` becomes 1 for players, and Weapon Blocking can trigger on a miss. Otherwise the damage level is DEF. Vanish-on-hit applies when total damage > 1. | battle.cpp:6782-6825 | B | P0 | H |
| BR-RULESET-002 | Context init: `div` comes from the skill's hit count. Ground skills get `amotion` 0. Flags are WEAPON plus SKILL if a skill or splash flag exists, else NORMAL. Range is long for arrow or Soul Attack basic attacks, otherwise short. Blow count includes the player blow bonus. | battle.cpp:5308-5354 | B | P0 | H |
| BR-RULESET-003 | Lucky dodge: a basic attack (or Sacrifice) against a target with `flee2` > 0 is fully avoided when `rnd%1000 < flee2`. | battle.cpp:5454-5461 | B | P0 | H |
| BR-RULESET-004 | The multi-hit roll happens before crit, and a crit cannot happen on a double-attack hit when Double Attack or Chain Action lack the crit flag. **Double Attack** needs a dagger (or a flagged source) and a non-fist weapon. Chance = max(lv × 5 (PRE) / lv × 7 (RE), `bonus.double_rate`). Kagemusya gives `val1×10`. **Chain Action** (revolver) is 5%/lv. **Fear Breeze** gives cumulative 12/9/6/3% for 2/3/4/5 hits, capped by ammo. | battle.cpp:4273-4348, 3038-3044 | B (5 vs 7 per lv) | P0 | H |
| BR-RULESET-005 | Crit eligibility: skills need the crit flag, except always-crit skills. `cri` gets arrow bonuses, +100 per Camouflage second (max 10), and +200 (PRE) / +300 (RE) for Sharpshooting. Kirikage adds 250+50·lv. Some skills halve `cri`, and some need a specific weapon or status. Target crit-defense scales it down, then `rnd%1000 < cri`. The base chance is BR-MAP-010. | battle.cpp:3021-3161 | B | P0 | H |
| BR-RULESET-006 | Hit determination is checked in a fixed order. Auto-hit cases: crit, perfect-hit %, Fusion, Splasher without splash, Crusader spirit Shield Boomerang, target in a non-stone/burning opt1 state, skill ignores flee. Otherwise Neutral Barrier blocks ranged non-magic. Hitrate is 80 (PRE) or 0 (RE) + hit − flee, with modifiers (Fog Wall −50, arrow hit, RE Vulture's Eye +lv, Double Attack +lv on a 2-hit, Weaponry Research, Sword Training). It is clamped, Rapid Smiting adds +20 after the clamp, then `rnd%100`. Formula basis is BR-MAP-011. | battle.cpp:3234-3324 | B | P0 | H |
| BR-RULESET-007 | Plant / infinite-defense targets take 1 damage per hit, skipping the pipeline. Camouflage ends unless the skill is Sharpshooting or Arrow Storm. | battle.cpp:5483-5496, 5793-5796, 4982-5045, 2894 | B | P1 | H |
| BR-RULESET-008 | **PRE base damage** (and non-PC in RE). PC max = weapon ATK, min = DEX × (80 + 20·weaponLv)%, capped at max. Bows: min = min×max/100. Maximize Power: min = max. Non-crit rolls `rnd%(max−min)+min`, so the top value is never rolled. Crit uses max. Add bow `arrow_atk`. Apply the size modifier (unless Weapon Perfection). Then add `batk`. | battle.cpp:2519-2639 | PRE (mobs in both) | P0 | H |
| BR-RULESET-009 | **RE ATK parts.** Players: statusAtk = batk, element-adjusted as Neutral (or weapon element under Mild Wind), then doubled for the right hand. weaponAtk = variance roll, then element. equipAtk = eatk + arrow ATK, then element. percentAtk = (weapon+equip) × atk_rate/100. masteryAtk is element-free. Monsters, homunculi and pets use the PRE base-damage path. | battle.cpp:3895-3958 | RE | P0 | H |
| BR-RULESET-010 | RE weapon ATK roll. variance = 5·ATK·weaponLv/100. stat bonus = ATK·STAT/200 (STAT = DEX for bows, instruments, whips and guns, or STR if Soul Attack is learned). Range = [ATK − var + bonus, ATK + var + bonus]. Crit or Maximize takes the max. Then `weapon_damage_rate`, then size fix (skipped by Weapon Perfection). | battle.cpp:2447-2499 | RE | P0 | H |
| BR-RULESET-011 | Equip ATK = `status.eatk` + `bonus.arrow_atk` (not for cannonballs) for arrow skills. In PRE, `SP_BASE_ATK` adds to batk. | battle.cpp:3437-3452, 3940-3944; pc.cpp:3737-3746 | RE | P1 | H |
| BR-RULESET-012 | Some skills replace the base damage. Examples: Sacrifice = 9% max HP; Dragon Breath = (HP/50 + maxSP/4)·lv, scaled by level above 100 and trainings; Self-Destruction; Happo Kunai; Shield Boomerang / Rapid Smiting = batk + 4·refine + shield weight/10; Spiral Pierce uses weapon weight (PRE 80%·weight with a 50·lv ratio; RE weight/10 with ×0.7 and a size fix); PRE-only Ishhen = 40·STR + HP·8·lv/100. | battle.cpp:3983-4140 | B (per-skill splits) | P0 | H |
| BR-RULESET-013 | `NK_SPLASHSPLIT` skills divide ATK or MATK by the count of targets hit (`miscflag`). A zero count is an error. | battle.cpp:4185-4196, 6006-6011 | B | P1 | H |
| BR-RULESET-014 | PRE base-stage additions. A crit adds `crit_atk_rate`. A non-crit adds `non_crit_atk_rate` unless the skill is flagged to ignore it. TK_POWER adds 2·lv·(party members on the map − 1) % for PRE, or a flat 20·lv% for RE. Target `crit_def_rate` reduces crit damage. Acid Terror subtracts def2 (PRE). | battle.cpp:4198-4238 | B | P1 | H |
| BR-RULESET-015 | Skill ratio = 100 + `bonus.skill_ratio` + Overthrust + Max Overthrust + Berserk (+100 PRE / +200 RE). Plain-attack extras: Poison React (+30·lv, may poison), Crush Strike, Giant Growth (+250, or +125 on PvP maps). Then the per-skill `calculateSkillRatio` hook. | battle.cpp:4413-4479 | B | P0 | H |
| BR-RULESET-016 | ATK% from statuses (Curse −25, Provoke, Concentration, True Sight, Joint Beat, Bloodlust, Fleet and others, capped 0..65535). Only certain skills use it. **RE**: it seeds the skill ratio. **PRE**: it multiplies the damage after the ratio. | battle.cpp:4361-4402, 4427, 4579-4580 | B (placement) | P0 | H |
| BR-RULESET-017 | Constant additions after the ratio (e.g. Asura = 250 + 150·lv, Rapid Smiting's random bonus ≥ 100). They are added before the SC modifiers. | battle.cpp:4489-4530, 5499, 5601 | B | P1 | H |
| BR-RULESET-018 | **PRE order**: base → ratio → constant add → skill ATK bonus → DEF → refine → masteries → Aura Blade → min 1 → attacker card fix → target card fix → element → star crumbs / EDP. Shield Boomerang and Rapid Smiting apply DEF before the ratio. | battle.cpp:5485-5521, 5648-5659, 4925-4946, 5761-5790 | PRE | P0 | H |
| BR-RULESET-019 | **RE order**: parts → card fix per part → SC bonuses → sum → P.ATK → masteries → crit rate → ratio → constant → Advanced Katar Mastery (+10+2·lv%) → RES → DEF (if any hand is not ignored) → post-DEF → skill ATK bonus → element → ×1.4 crit → flat additions → non-PC target card fix → div → hands → GvG/BG → final modifiers. | battle.cpp:5502-5817 | RE | P0 | H |
| BR-RULESET-020 | Attacker card fix: multiplicative factors on a base of 1000, rounded down. Order: race, element, size, race2, class, `add_dmg`, then short/long rate. Arrow attacks use the arrow variants. Left-hand bonuses merge into the right when `left_cardfix_to_right`. PRE also folds in Adv. Katar Mastery. **Example E-3.** | battle.cpp:773-1091, 810 | B | P0 | H |
| BR-RULESET-021 | Defender card fix (weapon): element vs attack element, defender's own element, size, weapon-size, race / race2 / `subrace3`, class, `add_def`, near / far attack-defense rates, `SC_DEF_RATE`. A left-hand element is applied separately when it differs. | battle.cpp:1093-1158 | B | P0 | H |
| BR-RULESET-022 | Magic card fix. **RE**: sequential rounded steps in the order size → race2 → element → attack-element → race → class, with `APPLY_CARDFIX_RE` on percent values. **PRE**: one multiplicative product. Defender side includes element, size, race, class, magic defense rate and `SC_MDEF_RATE`. Near attack-defense also counts vs magic in PRE. | battle.cpp:813-939 | B | P0 | H |
| BR-RULESET-023 | Status-based elemental vulnerability applied as card fix: Magic Poison +50, and +100 Climax Bloom / Climax Earth, +15 Misty Frost, +5·lv Cloud Poison by the attack element. In RE magic it applies right after the element mods. For PC defenders in PRE it adds to the element fix. | battle.cpp:729-755, 842-844, 896, 1159-1164 | B | P1 | H |
| BR-RULESET-024 | Mastery damage by passive and weapon type. Examples: Demon Bane = lv·(baseLv/20 + 3) vs demons / undead mobs; Ranger Main, Research FE, Beast Bane, Sword / Two-hand mastery. In RE the mastery term starts at 0 and Weaponry Research applies to all weapons. A skill exclusion list decides which skills ignore masteries, refine and star crumbs. Some SC "masteries" (Camouflage, Cart Boost, Nibelungen) are added to masteryAtk. | battle.cpp:2281-2420, 3399-3436, 3787-3883 | B | P0 | H |
| BR-RULESET-025 | RES: when target RES > 0 and the skill does not use simple defense, damage −= RES/(RES+400) × 80%. The attacker's ignore-RES (Telum, Potent Venom, race bonuses) is capped at 50%. MRES is the same for magic with A_VITA. **Example E-2.** | battle.cpp:5611-5641, 6073-6100 | RE | P0 | H |
| BR-RULESET-026 | Defense modifiers beyond BR-MAP-013..016: Expiatio −5%/lv DEF, Camouflage −5%/s DEF, Force of Vanguard +2%/lv, Revitalize, Overed Boost. Mob attackers add the target's Divine Protection, Ranger Main and Research FE to vitDEF. `weapon_defense_type` moves hard DEF into vitDEF. | battle.cpp:4744-4788, 4829-4837, 4865-4868 | B | P1 | H |
| BR-RULESET-027 | DEF bypass and piercing. PRE crit ignores DEF. Fusion, per-skill weapon conditions, `ignore_def_*` card flags (by element, race or class), and the skill's ignore-defense flag also bypass it. Piercing: Investigate always; weapon-card `def_ratio_atk` (not on crit in PRE). Left-hand cards can pass to the right (`left_cardfix_to_right`). | battle.cpp:3171-3208, 3334-3390 | B | P0 | H |
| BR-RULESET-028 | Post-defense. PRE: add refine bonus (atk2), min 1, then masteries. Both: Aura Blade (PRE 20·lv; RE (3+lv)·baseLv, marked TODO), min 1. RE: Sonic Acceleration +90%. | battle.cpp:4925-4972 | B | P1 | M |
| BR-RULESET-029 | Element selection. Skill element, or weapon element (right / left hand) for ELE_WEAPON and basic attacks. Ammo element for bows. Endow overrides all. Goldene Ferse can force Holy. Spirit charm × 10 endows. ELE_ENDOWED uses the status element, ELE_RANDOM picks randomly. Misc attacks treat weapon / endowed as Neutral. The per-skill `modifyElement` hook runs last. | battle.cpp:3466-3554 | B | P0 | H |
| BR-RULESET-030 | Element application. `NK_IGNOREELEMENT` skips it. Non-player basic attacks are Neutral when `attack_attr_none` covers their unit type. In RE, PCs already applied element per part. Some skills are forced Neutral after weapon element (Cart Revolution, Magic Crasher, Fallen Empire, Gate of Hell). Envenom and Throw Shuriken / Kunai apply the table twice in PRE. | battle.cpp:3651-3748 | B | P0 | H |
| BR-RULESET-031 | Statuses modify the element ratio. **PRE**: damage is first changed by a percentage, then multiplied by the table. **RE**: the percentage is added to the ratio, and the result is `dmg − dmg·(100−ratio)/100`. Includes Volcano, Violent Gale, Deluge, Spider Web (×2 vs Fire, then ends), Insignias, Burnt (+400), Oratio, Soul Curse. Also side effects: Earth damage frees Magnetic Field. **Example E-1.** | battle.cpp:515-721 | B (divergent) | P0 | H |
| BR-RULESET-032 | `attr_recover`: when 0, a negative element ratio is clamped to 0 (no healing). Damage is then capped at 0. | battle.cpp:709-719, 3775 | B | P1 | H |
| BR-RULESET-033 | Element endow bonuses. PRE: star crumbs, spirit spheres, EDP (+`val3`%), and `SC_WATK_ELEMENT` (a % of the normal-attack damage taken as that element), all applied after element and only to the right hand. RE: EDP = weapon and equip ATK × (2.5 + 0.3·lv), skipped by 5 skills. | battle.cpp:3711-3772, 4594-4612 | B | P1 | H |
| BR-RULESET-034 | RE crit damage: players × (1.4 + 0.01·`crate`), floored in float32. Non-players × 1.4. Target `crit_def_rate` then reduces it. In PRE crit means max damage and no multiplier. | battle.cpp:5676-5688; 2574-2577 | RE | P0 | H |
| BR-RULESET-035 | Flat skill additions after element: Taekwon kicks +10·Running when fists; Shield Press = VIT·refine; Tiger Cannon; Gate of Hell; homunculus skills (2500 / 4000 / 6000·lv + level); Max Pain. PRE also adds Weaponry Research +2/lv, Ground Drift, Hilt Binding +4, then the PRE attacker card fix. | battle.cpp:5690-5760 | B | P1 | H |
| BR-RULESET-036 | `div` fix: a negative `div` means "divide by it then multiply", which fixes the per-hit value. A total below `div` is raised to `div` for certain skills or when `skill_min_damage` flags it. | battle.cpp:4244-4263 | B | P1 | H |
| BR-RULESET-037 | Hands. Katar off-hand = main × (1+2·TF_DOUBLE)% for normal attacks only. Dual wield (Assassin, Kagerou/Oboro) scales right and left hands: right 50 + 10·lv, left 30 + 10·lv (Assassin); right 70 + 10·lv, left 50 + 10·lv (Kagerou/Oboro). Left-only wielders get unarmed right. Both hands min 1. **Example E-6.** | battle.cpp:5058-5107 | B (RE excludes katar from dual wield) | P1 | H |
| BR-RULESET-038 | `battle_calc_damage` (target side) in order: pre-gates (kill-steal protection, Maelstrom, `no_*_damage` special states, bypass skills); target increases; target reductions; caster-side effects. 52 statuses are involved. Details in BR-RULESET-039 and BR-RULESET-040. | battle.cpp:1672-2069 | B | P0 | H |
| BR-RULESET-039 | Target modifiers, in order. Barrier turns any positive damage into `div`. RE: Kaite ×4 vs melee. Ground Gravity +10%. Shield Chain Rush +10%. Aeterna ×2 (consumed, except special cases). RE: Raid +30% (+15% vs boss). Deep Sleep +50% (ends). Crystallize by weapon type. Sound of Destruction ×2. Dark Crow, Holy Oil, Rush Quake and Shadow Scar add percentages. Reductions: PRE Assumptio ×2/3 (vs-map) or ÷2; Shadow Clock −15%; Defender; Adjustment −20% ranged; Fog Wall (−25% skill / ÷4 ranged normal); Smoke Powder (−15% melee / −50% ranged); Water Barrier −20%; Energy Coat (SP-tiered 6% per 20%); Pain Killer flat; RE Steel Body ÷10 (min 1). | battle.cpp:1705-1963 | B | P0 | H |
| BR-RULESET-040 | Caster-side and tail. Breaking Limit multipliers, Bloodlust / Bloodsucker HP drain, Poisoning Weapon +10%, Deadly Infect spread, add-damage statuses. Then PK rates, `skill_min_damage`, Relieve, monster / elemental `damagetaken` %, and the "damage-to-0" statuses (`battle_status_block_damage`). | battle.cpp:1965-2068 | B | P0 | H |
| BR-RULESET-041 | Map-mode damage rates. GvG / BG: skills ×60% (weapon / magic / misc); basic attacks ×80% (short and long). PK: skills ×60%, basic short ×80% / long ×70%. Result min 1; skills with the ignore-reduction flag are exempt. GvG / BG target eligibility: Emperium needs the Guild Approval skill; max-castle guilds are blocked. Defaults are in `conf/battle`. | battle.cpp:2112-2248, 5133-5187 | B | P0 | H |
| BR-RULESET-042 | Final weapon modifiers. Reject Sword: for dagger / 1H sword / 2H sword attackers, `val2`% chance to reflect 50% back (`val3` uses). Poison React auto-casts Envenom lv5. Crescent Elbow counter. Fusion HP penalty. Enchant Blade adds MATK-based damage on plain attacks. Camouflage ends. Then the skill-damage DB adjustment. | battle.cpp:5197-5298 | B | P1 | H |
| BR-RULESET-043 | Damage reflection (`battle_calc_return_damage`). Status-blockers: White Imprisonment, Dark Crow, Kyomu. Short range: item `short_weapon_damage_return` or Reflect Shield `val2`%. Death Bound amplifies then splits 30 / 70. Long range: `long_weapon_damage_return`. GvG / BG / PK rates apply. Devotion may redirect the reflected damage by `devotion_rdamage` %. | battle.cpp:5365-5408, 5114-5123, 6835-6911 | B | P1 | H |
| BR-RULESET-044 | Magic damage, ordering. MATK is a random roll between min and max. Splash divides it. **PRE**: damage × skill ratio immediately. **RE**: card fix → two-hand staff bonus → S.MATK → ratio. Then the skill ATK bonus (PRE before, RE after MDEF), then the ignore-MDEF check, then MDEF, then element, then PRE card fix. Insignia level-3 adds +25 ratio. Option-spirit flat adds (Gust and others: 6 + INT/4 + max(DEX−10,0)/30). Result min 1. | battle.cpp:5899-6034, 6035-6071, 6149-6272 | B | P0 | H |
| BR-RULESET-045 | MDEF. **RE**: `mdef` is cut by the ignore-MDEF % (statuses, race / class / race2 bonuses, capped 0..100), clamped to ≥ 0, and `dmg = dmg·(1000+mdef)/(1000+10·mdef) − mdef2`. **PRE**: `mdef` reduced by ignore %, then `dmg·(100−mdef)/100 − mdef2` (or the `magic_defense_type` variant). | battle.cpp:6102-6147 | B | P0 | H |
| BR-RULESET-046 | Magic special bases. Turn Undead / Resurrection instakill chance (per-mille, cap 700): **RE** 10·lv + LUK + INT + baseLv + 300 − 300·HP/maxHP; **PRE** 20·lv … + 200 − 200·HP/maxHP; it fails on status-immune targets. Otherwise RE = MATK·lv, PRE = baseLv + INT + 10·lv. Grand Cross: PRE ATK + MATK, RE (ATK+MATK)/2, then the ratio; a self-cast halves for players. Earthquake, Soul Burn (2×SP), Aspersio (40), Renovatio, Dark Breath. | battle.cpp:5921-6000, 6201-6290 | B | P1 | H |
| BR-RULESET-047 | Misc (BF_MISC) attacks use fixed formulas. Throw Stone 50 / 30. Traps: PRE Land Mine lv·(DEX+75)·(100+INT)/100, Blast Mine lv·(DEX/2+50)·(100+INT)/100, Claymore lv·(DEX/2+75)·(100+INT)/100; RE all = lv·DEX·(3 + baseLv/100)·(1 + INT/35) ±10% + 40·Research Trap. Blitz Beat PRE (DEX/10 + INT/2 + 3·Steel Crow + 40)·2 vs RE 20·lv + 6·SteelCrow + AGI + DEX/5. | battle.cpp:6321-6430 | B (divergent) | P1 | H |
| BR-RULESET-048 | Heal amount (`skill_calc_heal`). **PRE**: (baseLv+INT)/8·(4+8·lv), +2%·Meditatio per level. **RE**: (baseLv+INT)/5·30·lv/10, with additive % bonuses. Casting on undead or damaging mode halves it. Cap by `max_heal_lv` / `max_heal`. Specific skills (Sanctuary 100·lv / 777, Highness Heal, Mediale) have their own formulas. **Example E-5.** | skill.cpp:523-640 | B (divergent) | P0 | H |
| BR-RULESET-049 | Non-player attackers. Mobs, pets and homunculi attack Neutral (`attack_attr_none` = 14 by default). With `cardfix_monster_physical` the attacker's cards count as weapon even for mobs. Mob `cri` is scaled by `mob_critical_rate`; others by `critical_rate`. Elementals get +20% under the matching Insignia. | battle.cpp:3675, 804-807, 2587-2623; status.cpp:2775-2792 | B | P1 | H |
| BR-RULESET-050 | Skill damage DB (`skill_damage_db.txt`): per skill, a % adjustment by caster type, map mode (normal, PvP, GvG, BG, mapflag, restricted zone 1..7) and target class. Applied at the very end of weapon, magic and reflect. | battle.cpp:2787-2855; db/skill_damage_db.txt:1-40 | B | P1 | H |

## 3. Status effects: what a declarative "stat contribution" schema must express

| ID | Rule | Citation | Mode | Pri | Conf |
|---|---|---|---|---|---|
| BR-RULESET-051 | A status record carries: id, icon, default-duration skill, states (no-move, no-cast, no-attack…), calc flags (which stats it recalculates), opt1 / opt2 / opt3 / options, flags (see 074), min rate, min duration, Fail / EndOnStart / EndReturn / EndOnEnd lists, and an optional script. Script statuses run only for players. The two packages hold 1,018 (RE) and 699 (PRE) records. | db/status.yml:22-41; status.hpp:3274-3295; status.cpp:10186-10189 | B | P0 | H |
| BR-RULESET-052 | Start gating, in order. Dead targets are refused (except no-chat / jailed). Map-disabled statuses. Gravity Control blocks everything. Madogear-failing statuses. Boss / MVP resist flags. EndReturn cancels and returns true. The Fail list refuses (also when the same status was the last effect this attack). Then resistance. | status.cpp:10171-10236 | B | P0 | H |
| BR-RULESET-053 | **PRE resistance.** Per status, a `sc_def` (rate and duration) and `sc_def2` (flat rate, with a LUK / level-difference term), plus a separate duration reduction. Examples: Poison / Stun / Silence / Bleeding from VIT, Sleep from INT, Stone / Freeze from MDEF, Curse from LUK, Confusion STR + INT. Poison has different player vs monster duration math. Curse is immune if LUK is 0. **Example E-4.** | status.cpp:9682-9801 | PRE | P0 | H |
| BR-RULESET-054 | **RE resistance.** `levelAdv = (max(0, srcLv − tgtLv)²/5)·100`. Rate resistance is a stat × 100 − `levelAdv`; duration loses a fixed `tick_def2` (e.g. Stun −500 ms… Bleeding −12 s, Freeze −3 s). Different stats than PRE (Silence and Blind use INT, Sleep and Bleeding AGI, Confusion LUK, Stone and Freeze MDEF). **Example E-4.** | status.cpp:9678-9801 | RE | P0 | H |
| BR-RULESET-055 | Common resistance tail. Player and monster `sc_def_rate` scaling and caps (`pc_max_sc_def`, `mob_max_sc_def`). `SC_SCRESIST` / Siegfried add resistance. Rate is reduced by %, then flat, then item `reseff`, then rounded up to a multiple of 10. Clamped to the status' `min_rate`, then rolled out of 10000. Duration reduced the same way (RE also applies `reseff` to duration), minimum `min_duration`. Flags `NORATEDEF` / `NOTICKDEF` skip parts. | status.cpp:9886-9992 | B | P0 | H |
| BR-RULESET-056 | Immunity edge cases. Golden Thief Bug / Wand of Hermod (`status_isimmune`) block statuses from magic skills unless the skill has `IgnoreGTB`. Leech End / Oblivion Curse grant immunity to Blind & Stun, or Silence & Curse. | status.cpp:9658-9669, 9930-9941 | B | P1 | H |
| BR-RULESET-057 | Delayed application. If a delay is given, the status is stored and applied by timer. **RE** subtracts the delay from the duration (min 1); PRE keeps the full duration. Stone Wait uses the delay as the time before petrification. Bleeding always starts immediately. | status.cpp:10266-10320 | B | P1 | H |
| BR-RULESET-058 | Type rules before the delay: undead race / element is immune to Stone / Stone Wait / Freeze (unless forced); Burning is blocked on Fire level 2. | status.cpp:10252-10299 | B | P1 | H |
| BR-RULESET-059 | Post-delay immunities and prerequisites are per type: Signum Crucis only on demon / undead non-players; Kyrie not on mobs; Adrenaline needs the right weapon; Cloaking / Camouflage need walls at low level; strip statuses need an equippable slot and respect `unstripable_equip`; mercenary buffs only on mercenaries; Sacrament-style spirit links need the right class and level < 70. | status.cpp:10338-10683 | B | P1 | H |
| BR-RULESET-060 | Exclusion. A new opt1 status ends every other opt1 status. `EndOnStart` ends listed statuses. Hardcoded cures: Blessing ends Curse (then Stone) on players and does not apply; Impositio lower level replaces; Silence ends Gospel (self). The Toxin / Paralyse / Venom Bleed / Magic Mushroom / Death Hurt / Pyrexia / Leech End family does not stack: a target already afflicted refuses, and a caster-side bonus entry is removed. | status.cpp:10571-10587, 10685-10767 | B | P0 | H |
| BR-RULESET-061 | Overlap. If the status is already active, a lower or equal incoming `val1` is refused (returns true so animations still play) unless the status ignores level overlap. Exceptions per status: Adrenaline / Weapon Perfection / Overthrust compare `val2`; Gospel, Endure, guild auras, Joint Beat (ORs `val2` flags; Break Neck blocks), Jailed (keeps old data). | status.cpp:10769-10827 | B | P0 | H |
| BR-RULESET-062 | Food. A normal food stat buff is refused if the matching cash-food buff has a higher `val1`, and vice versa. | status.cpp:10485-10532 | B | P2 | H |
| BR-RULESET-063 | Strip statuses (weapon, shield, armor, helm, accessory, shadow gear) unequip the item on apply. They fail if nothing is equipped or the bonus `unstripable_equip` forbids it. Duration 1 means "strip only". | status.cpp:10394-10476, 10537-10554 | B | P1 | H |
| BR-RULESET-064 | End semantics. Ending a status with the stale timer id is ignored. Endure with `val4` is permanent (cannot be ended normally). Spider Web ends only after all its unit groups are gone. Ending runs the state recalc, display removal and `EndOnEnd`. Type-specific on-end effects exist (e.g. Granitic Armor deals `maxHP·val3/100`, never lethal). | status.cpp:13451-13560 | B | P0 | H |
| BR-RULESET-065 | Stat contributions (STR…CRT). Applied in code order as flat adds or `x·%/100` terms. Some statuses early-return and override all else (Harmonize returns before anything else). Result is clamped 0..65535. A schema needs add, mul and override with a defined order. | status.cpp:6773-7300 | B | P0 | H |
| BR-RULESET-066 | DEF / DEF2 contributions. Override statuses first (Berserk, Eternal Chaos RE → 0; Keeping → 90; Steel Body → 90 in PRE only; DefSet; Nyanggrass). Then additive and percent terms in code order. Some apply only to non-players (Provoke, Strip Shield, Poison ×0.75). PRE clamps `def2` to ≥ 1, RE allows negatives. | status.cpp:7769-7935 | B | P0 | H |
| BR-RULESET-067 | Speed model. Haste group = the maximum of the haste statuses and item speed; slow group = the maximum of the slows. Net speed rate = 100 − haste + slow, min 40. Then cart (+50 − 5·lv %), Paralysis, Steel Body (200), Defender / Armor (≥ 200), Walkspeed. Madogear ignores most statuses. Casting with Free Cast / Exceed Break changes the rate. **Example E-4b.** | status.cpp:8039-8246 | B | P0 | H |
| BR-RULESET-068 | ASPD contributions. **PRE** (`aspd_rate`): the best of the quicken family (max over Two-Hand Quicken, One-Hand, Merc Quicken, Adrenaline, Spear Quicken, Gatling Fever, Fleet, Invincible, Assassin Cross of Sunset on non-bow weapons), then Berserk −300, ASPD potions, and slow statuses added. **RE** (`RENEWAL_ASPD`): a percent bonus group with caps (Quicken family 7, Adrenaline Rush 2 = 6, Fleet 5, Berserk 15, Madness Cancel 20, potions added) and a fixed group (Gust / Blast option −50, Soul Shadow and others). This flag is independent of `RENEWAL`. | status.cpp:8258-8517 | B (flag-split) | P0 | H |
| BR-RULESET-069 | Same pattern for BATK, WATK, HIT, FLEE, FLEE2, CRI, MDEF, MDEF2, MAXHP / MAXSP / MAXAP, P.ATK, S.MATK, RES, MRES, H.PLUS, C.RATE, and the new-class stats. Each function is a flat list of status checks. Not read line by line: rated M. | status.cpp:7316-7765, 7944-8036, 8733+ | B | P0 | M |
| BR-RULESET-070 | Cast / delay / SP-cost contributions from statuses. Cast: Slow Cast, Poem of Bragi, Suffragium, Foresight (halves; counter decrements even when not applied), Izayoi, Insignia, and others, with PRE and RE-cast (`RENEWAL_CAST`) variants. Delay: Poem of Bragi, Wind Insignia, Magic Mushroom. SP cost: Laziness, Recognized Spell, Offertorium, Gloomy Day, Nibelungen, Adaptation. (Base formulas are BR-MAP-051..053.) | skill.cpp:10202-10216, 10350-10403, 10500-10507, 9835-9854 | B | P0 | H |
| BR-RULESET-071 | Statuses that override element, defense element, mode, size, race, range or look. They are tagged by calc flags `ATK_ELE`, `DEF_ELE`, `MODE`, `SIZE`, `RACE`, `RANGE` and recalculated through their own functions. | status.hpp:3132-3136; status.cpp:119-121 | B | P1 | M |
| BR-RULESET-072 | Periodic effects (tick handlers). Poison: players lose 2 + maxHP·3/200 (Deadly Poison 2 + maxHP/50); monsters 2 + maxHP/200 (/100); stops at 25% HP; blocked by Slow Poison. Bleeding: `rnd%600+200`, never lethal to monsters. Burning: 1000 + 3% maxHP fixed. Toxin: 1 damage + 3% SP drain on target, 1% SP regen on caster. Maximize Power / Cloaking drain SP. **Example E-8.** | status.cpp:14152-14308 | B | P0 | H |
| BR-RULESET-073 | States from `States:` (NoMove, NoCast, NoAttack, NoPickItem…) are computed from the active set, with conditional flags, by `status_calc_state`. | status.cpp:5531-5700; status.hpp:3078+ | B | P0 | H |
| BR-RULESET-074 | Status flags with behavior: NoDispell, NoClearance, NoBanishingBuster, NoClearBuff, NoSave, NoSaveInfinite, NoRemoveOnDead, RemoveOnDamaged, RemoveOnRefresh, RemoveOnChangeMap / MapWarp, RemoveOnUnequip (weapon / armor), RemoveOnHermode, RequireWeapon / Shield / NoWeapon, StopAttacking / Casting / Walking, BossResist, MvpResist, Debuff, OverlapIgnoreLevel, SpreadEffect, SendVal1..3. `status_change_clear` and the dispel paths depend on them. | status.hpp:3216-3271; status.cpp:13387 | B | P0 | H |
| BR-RULESET-075 | Scripted status effects: the `Script:` field runs during recalculation for every active status that defines one (e.g. Speedup0/1 use `bonus bSpeedRate`, Atkpotion uses `bonus bBaseAtk`). This is the existing precedent for "status contributes via script". | status.cpp:4213-4221; db/re/status.yml:748-766 | B | P0 | H |
| BR-RULESET-076 | Soul Link (Spirit) is class-gated. The `val2` link type must match the target's second-class mask; Soul Linker "High" link needs level < 70 and a first-class upper job. | status.cpp:10588-10680 | B | P2 | H |
| BR-RULESET-077 | Recalculation is partial and guarded. Calc flags select which stats are recomputed. The player recalculation aborts if reentered more than 10 levels deep. Scripts that retrigger recalculation abort the current pass. | status.cpp:3784, 6546-6600, 4018, 4051 | B | P0 | H |

## 4. Skill behavior

| ID | Rule | Citation | Mode | Pri | Conf |
|---|---|---|---|---|---|
| BR-RULESET-078 | `skill_db.yml` field semantics. Per level arrays (up to 13): Range, HitCount, Element, SplashArea, ActiveInstance, Knockback, GiveAp, CastTime, FixedCastTime, AfterCastActDelay, AfterCastWalkDelay, Duration1 / 2, Cooldown, and Requires (HP, SP, AP, rate costs where positive = current and negative = max, MaxHpTrigger, Zeny, Weapon, Ammo, State, Status, SpiritSphere, ItemCost, Equipment). Also Type (weapon / magic / misc), TargetType (passive / attack / ground / self / support / trap), DamageFlags (NoDamage, Splash, SplashSplit, IgnoreAtkCard / Element / Defense / Flee / DefCard, Critical, IgnoreLongCard, SimpleDefense), Hit type, CastCancel, CastDefenseReduction, NoNearNPC, CopyFlags, Unit block, Status link. | db/skill_db.yml:22-130; skill.hpp:62-86 | B | P0 | H |
| BR-RULESET-079 | The two packages differ per skill. `db/pre-re/skill_db.yml` has 1,228 skills and `db/re/skill_db.yml` has 1,635. Many pre-RE skills have different costs, ranges and delays than their RE namesakes, so these are separate data sets, not one set with flags. | db/pre-re/skill_db.yml; db/re/skill_db.yml; db/skill_db.yml:132-139 | B | P0 | H |
| BR-RULESET-080 | Dispatch. `skill_castend_id` (single target) calls `skill_castend_nodamage_id` or `skill_castend_damage_id` by cast type. `skill_castend_pos` (ground) calls `skill_castend_pos2`. A few target skills are redirected to ground (Wall of Thorn, Frost Misty, Spider Web, Resurrect Homunculus use a random offset). Each per-skill effect is a `SkillImpl` override. | skill.cpp:4874-5007, 5274, 5433, 4201, 4388 | B | P0 | H |
| BR-RULESET-081 | Cast-end lifecycle order: timer sanity; set attack delay; cooldown block check; target validity; special redirects; `skill_castend_id_check`; status check; out-of-range check (with `skill_add_range` and optional consume); walkpath check; castend condition check; consume requirements; give AP; skill-immune target check; merc / homun conditions; stop walking; set `canact_tick`; apply cooldown; special statuses (Brandish Spear, Grand Cross); walk delay; dispatch; auto-repeat; end Camouflage; clear skill-item state. On failure Asura Strike still consumes everything and zeroes SP. | skill.cpp:4874-5269 | B | P0 | H |
| BR-RULESET-082 | Pre-cast check (`castbegin`): GM bypass (but not for skill-items), menu-skill mutual exclusion, skill-item consumption, then state / weapon / ammo / spheres / status / equipment / item requirements, and partner-based reductions (e.g. −20% SP per Wizard / Mage party member). | skill.cpp:8270-8330, 8924, 9358 | B | P0 | M |
| BR-RULESET-083 | Requirement computation. HP and SP cost = flat + (positive rate × current, negative rate × max). Then `dsprate` and per-skill SP rate / flat bonuses; Reproduce +30%; clamp 0..32767; then SP statuses (Laziness, Recognized Spell +25%, Offertorium, Telekinesis, Adaptation, Gloomy Day, Crescive Bolt). Zeny +250·lv or 1000 under Unlucky. Ammo defaults to 1 for weapon-using skills. Trap items fall back to Alloy Trap / Trap when the player lacks them. | skill.cpp:9796-9959 | B | P0 | H |
| BR-RULESET-084 | Consume-on-fail semantics: out-of-range with `skill_out_range_consume` and failed walkpath still consume requirements; damage skills on skill-immune targets consume and then fail. | skill.cpp:5020-5039, 5084-5087 | B | P1 | H |
| BR-RULESET-085 | Delay and cooldown lifecycle. `canact_tick = max(tick + delay, canact_tick − SECURITY_CASTTIME)` (skipped for skill-items without delay). Cooldown: players get a bonus-adjusted cooldown; **RE** homunculi use the skill cooldown, **PRE** homunculi use the skill delay as cooldown; mercenaries use cooldown. A failed cast resets `canact_tick`. | skill.cpp:5103-5133, 5252-5256 | B (divergent) | P0 | H |
| BR-RULESET-086 | After-cast walk delay: skills without "move enabled" state set a walk delay (monsters: just the skill's; others: `default_walk_delay` + skill's). | skill.cpp:5168-5174 | B | P1 | H |
| BR-RULESET-087 | `skill_attack` pre-gates: skill-immune and Emperium targets, ground skill target re-check, Trick Dead, PRE Gravitation (only Gravitational Field and autocasts deal damage). Pet skill attacks use their own fixed damage with an element table. | skill.cpp:2735-2801 | B | P0 | H |
| BR-RULESET-088 | Magic reflection (Kaite and similar) swaps caster and target, with PRE vs RE differences in who recomputes the element and how Energy Coat and reflect-reduction apply. Magic Rod absorbs and gives SP. Hallucination Walk can negate. Wizard soul-link consumes a Crystal Fragment. | skill.cpp:2803-2902 | B | P1 | H |
| BR-RULESET-089 | Knockback. Blow count comes from the skill and bonuses. It is zeroed when the hit did less than `div` damage (except Palm Strike) or when reflected. Triangle Shot has a probabilistic push. | skill.cpp:2928-2940 | B | P1 | H |
| BR-RULESET-090 | Additional effects after a hit: only if the damage level is at least BLOCK. Taekwon stances proc a combo (15%, or 20% for Counter). Item `addeff` entries trigger by attack type and range (arrow rate adds for long range), on target or self. `addeff_onskill` triggers by skill. Shadow Weapon triggers Shadow Scar. | skill.cpp:1230-1350 | B | P0 | H |
| BR-RULESET-091 | Splash and area. Splash size comes from SplashArea (plus `splash_range` bonus); targets are filtered by enemy / ally mask. `miscflag` carries the target count for `SPLASHSPLIT`. | skill.cpp:3345-3700 | B | P0 | M |
| BR-RULESET-092 | Ground unit lifecycle: `skill_unitsetting` computes duration (map-dependent), layout, range, interval and target mask, then per-skill state (`val1..val3`, e.g. Safety Wall HP and count, Fire Wall hits 4+lv, Sanctuary heals, Wall of Fog). Traps are hidden according to `traps_setting`. `skill_unit_onplace` / `onout` apply effects. | skill.cpp:5781-5900, 6390 | B | P0 | M |
| BR-RULESET-093 | Unit-specific rules: Fire Pillar and Land Protector placement, Quagmire / Demonstration become hit-all in PvP / GvG with `vs_traps_bctall`, Meteor uses a passed duration, Warp Portal morphs to an active state after 2 s. | skill.cpp:5850-5900 | B | P2 | M |
| BR-RULESET-094 | Combo state: basic attacks with Taekwon ready-stances open a combo window of 2000 − 4·AGI − 2·DEX ms. | skill.cpp:1250-1277; skill.cpp:10453-10457 | B | P1 | H |
| BR-RULESET-095 | Delay reduction for combo skills (Triple Attack, Chain Combo, Combo Finish, Tiger Fist, Dragon Combo…): `1000 − 4·AGI − 2·DEX` when no delay is set. Spirit-linked Shield Boomerang and Sonic Blow halve their delay. | skill.cpp:10444-10457, 10484-10495 | B | P1 | H |
| BR-RULESET-096 | The native / script hook set must include at least the nine `SkillImpl` virtuals (see section 0 item 2). The skill factory maps skill id to an implementation; skills without one fall back to the generic switch code. | skills/skill_impl.hpp:7-62; skills/skill_factory.hpp | B | P0 | H |
| BR-RULESET-097 | Skill-items (cast from an item or autocast) skip requirements and delay; Hocus-Pocus and Abracadabra use the picked skill's own data. | skill.cpp:9780-9790, 5103, 5206 | B | P1 | H |

## 5. Item effects

| ID | Rule | Citation | Mode | Pri | Conf |
|---|---|---|---|---|---|
| BR-RULESET-098 | The item bonus API is `bonus`, `bonus2` … `bonus5`. Number of arguments picks `pc_bonus` .. `pc_bonus5`. Bonus types can take a skill name or id (validated). The dispatcher has about 506 `SP_*` cases. This is the main "item effect vocabulary" a script ruleset must define. | script.cpp:10029-10126; pc.cpp:3690, 4463, 5129, 5270, 5353 | B | P0 | H |
| BR-RULESET-099 | A hand flag (`lr_flag`) says which weapon, shield or ammo the script runs for. Hand-specific bonuses (ATK, element, range, card effects) go to the right or left weapon data. Most stat bonuses are ignored when the flag is "arrow". | pc.cpp:3705-3726, 3846-3870, 3908-3928 | B | P0 | H |
| BR-RULESET-100 | Stacking and clamps per bonus family. Stats are summed and clamped to 16 bits. PRE DEF / MDEF clamp to a signed byte, RE to 16 bits. `bSpeedRate` keeps the strongest (non-stackable); `bSpeedAddRate` stacks. ASPD adds ×10. FLEE2 and Critical are scaled ×10. Table bonuses merge by key, cap at `MAX_PC_BONUS`, and clamp rates to ±10000. | pc.cpp:3720-3806, 3929-3947, 3176-3249, 3481-3517 | B | P0 | H |
| BR-RULESET-101 | Defaults when a bonus omits flags: add-effect defaults to target, weapon, short + long range. Element add-damage defaults to weapon, normal + skill, short + long. Default duration comes from the status' linked skill at level 7. | pc.cpp:3183-3191, 3492-3501 | B | P1 | H |
| BR-RULESET-102 | RE vs PRE bonus meaning. `bBaseAtk` is a stackable `eatk` in RE and `batk` in PRE. `bAspdRate` is `aspd_rate −= 10·v` in PRE and `aspd_rate2 += v` in RE. Weapon-size modifiers come from `size_fix` by weapon type; mounted spear users get the Large modifier vs Medium. | pc.cpp:3737-3746, 3941-3947; status.cpp:4246-4265; db/*/size_fix.yml | B | P0 | H |
| BR-RULESET-103 | Equipment parse order in `status_calc_pc_sub`: reset → autobonus → per slot (equip-script on first calc, refine sanitise, weapon data and element, DEF refine, item script with hand flag, forged star crumbs) → item combos → card scripts → random options → status scripts → timed bonus scripts → pet bonus → final clamps. Order matters because several bonuses are "keep strongest". | status.cpp:3925-4265 | B | P0 | H |
| BR-RULESET-104 | Refine and forging. Refine data supplies the DEF / ATK bonus and the over-refine bonus. RE adds `refine·2` P.ATK / S.MATK (weapon) and RES / MRES (armor). Forged weapons add star crumbs (3 stars → +40) plus element and ranking bonus. | status.cpp:3948-4040; db/refine.yml | B | P0 | H |
| BR-RULESET-105 | Item combos run their script when every item of the set is equipped and none is restricted on the map. | status.cpp:4076-4112; db/item_combos.yml | B | P1 | H |
| BR-RULESET-106 | Equip and unequip scripts run on equip / unequip (and on login, first calc), unless the item is restricted on the map and the player lacks the all-equipment permission. Cards and slotted items do the same. | pc.cpp:12348-12361, 12446-12460, 1218; status.cpp:3941-3943, 4146 | B | P0 | H |
| BR-RULESET-107 | Consuming an item. Refused while sitting for delayed-consume items, in some opt1 states, while casting, or on item cooldown. On restricted maps the item may be consumed without effect. Non-delayed items are removed before the script runs. The script runs as a server NPC and then the previous script is restored. Ranked-alchemist potions set a +50% flag. | pc.cpp:6408-6535 | B | P0 | H |
| BR-RULESET-108 | Item heal. HP bonus = 100 + 2·VIT + 10·Recovery + 5·Potion Learning, then ranked-potion +50%, item-group and per-item `itemhealrate`, Recovery Potion. Penalties: Critical Wound, Death Hurt, No Recover (100%). Vitality Activation ×1.5; Water Insignia +10%; Bite Scar blocks HP. SP uses INT and Mana Recovery. **Example E-7.** | pc.cpp:10780-10868; script.cpp:6038-6080 | B | P0 | H |
| BR-RULESET-109 | Timed `bonus_script` entries and `autobonus` (chance-on-attack / hit bonus scripts with duration). They are saved per player and removed by flags (e.g. on Madogear). | pc.cpp:15093-15260; script.cpp:10128-10240 | B | P1 | M |
| BR-RULESET-110 | `item_db.yml` fields: Id, AegisName, Type, SubType, Buy / Sell, Weight (×10), Attack, MagicAttack, Defense, Range, Slots, Jobs, Classes, Gender, Locations, WeaponLevel, ArmorLevel, EquipLevelMin / Max, Refineable, Gradable, View, Flags (BuyingStore, DeadBranch, Container, UniqueId, BindOnEquip, NoConsume, DropEffect), Delay, Stack, NoUse, Trade, Script, EquipScript, UnEquipScript. PRE and RE have separate item DBs. | db/item_db.yml:22-100; db/pre-re/item_db_*.yml; db/re/item_db_*.yml | B | P0 | H |

## 6. Native fast paths and the native / script contract

| ID | Requirement | Citation (evidence) | Pri |
|---|---|---|---|
| BR-RULESET-111 | **Hot: default attack calculation** (BR-RULESET-001..010, 018 / 019, 021, 022, 036, 039..041). It runs per hit for every player, mob and AoE target, many times per tick. Keep native, driven by script-published tables: element matrix, size matrix, per-status modifier lists (condition mask + add / mul + order slot), card-fix buckets. Scripts override only through per-skill hooks and registered stage hooks. | battle.cpp:5418-5823 | P0 |
| BR-RULESET-112 | **Hot: stat recalculation** (BR-RULESET-065..069, 077, 103). It runs on every equip, buff or status change and re-executes every equipped item's script. Contract: scripts return a compiled, cacheable list of bonus deltas (not live memory writes); recompute is incremental by calc flag; recursion limit 10. | status.cpp:3775-4265, 6546 | P0 |
| BR-RULESET-113 | **Hot: mob AI** (100 ms hard tick, 1 s lazy tick). Aggro scan, chase, target change, loot, slave logic and berserk / angry / follow / rush states are driven by monster modes (`MD_*`), not by game rules a designer edits. Keep native. The script surface should be limited to: mob skill selection conditions, idle / kill events, and AI-mode tunables. | mob.cpp:1295-2337, 7020-7021; mob.hpp:35 | P0 |
| BR-RULESET-114 | **Hot: ground units and area scans** (unit tick, `skill_area_sub`, `battle_check_target`). Native iteration; per-unit effect callbacks (`onplace` / `onout`) are scriptable but must be batched per tick, with declarative effects (damage tick, apply status) fast-pathed. | skill.cpp:3345, 6390; battle.cpp:7898 | P1 |
| BR-RULESET-115 | **Numeric fidelity.** Legacy math uses int64 truncation, some float32 steps (`(float)` casts at battle.cpp:5563, 5679, 6098) and per-step rounding. Luau numbers are doubles. The runtime must provide integer division, float32 rounding and int64 helpers so scripts reproduce results exactly. RNG calls must follow legacy draw counts (BR-MAP-010 / 011 draw even when the chance is ≤ 0) so seeded replays match. | battle.cpp:3157, 3322, 5563, 5679 | P0 |
| BR-RULESET-116 | **Side effects inside calculation.** Rules trigger actions while computing damage: Magma Flow casts a skill, Reject Sword reflects, Poison React autocasts Envenom, Crescent Elbow counters, statuses are ended or started (Aeterna, Spider Web, Energy Coat SP charge). The contract needs an ordered effect queue returned from a calculation, applied by native code after the result is final, with a re-entrancy guard. Scripts must not mutate world state directly. | battle.cpp:1908-1909, 5208-5252, 5229-5236, 1894-1897, 600-603 | P0 |
| BR-RULESET-117 | **Hook surface** (minimum): skill hooks (the nine in BR-RULESET-096); damage-stage hooks (base, ratio, element, defense, final); status hooks (start gate, resist, calc contribution, tick, end); item hooks (bonus script, equip / unequip, use, combo); formula hooks (heal, cast, delay, SP cost). Each hook receives immutable unit snapshots (`status_data`, active statuses with `val1..val4`, equipment) and returns deltas plus an effect queue. | skills/skill_impl.hpp:7-62; script_host.hpp:86-102 | P0 |
| BR-RULESET-118 | **Ruleset package.** A manifest names the mode (renewal or pre-renewal), the sub-flags (CAST, DROP, EXP, LVDMG, ASPD, STAT, plus EDP), the DB set (`db/pre-re` vs `db/re`) and the battle-config defaults (the `conf/battle/*.conf` tunables used above: hit-rate bounds 5..100, crowd penalties, GvG / BG / PK rates, `max_res_mres_ignored` 50, `attack_attr_none` 14, `left_cardfix_to_right` yes, `cardfix_monster_physical` yes). Static tables stay data (YAML); only formulas and per-skill / per-status / per-item behavior become scripts. | config/renewal.hpp:37-74; conf/battle/*.conf | P0 |
| BR-RULESET-119 | **Verification.** Legacy already has a golden-trace harness (`RATHENA_GOLDEN_TRACE`) that records inputs and outputs of hit, crit, defense and delay calculations. Reuse it as the conformance suite for both packages, keyed by BR-RULESET ID. | battle.cpp:4739-4741, 4910-4914; skill.cpp:10421-10426, 10616-10627 | P0 |

## 7. Worked Given / When / Then examples

**E-1 (BR-RULESET-031) Status element boost, PRE vs RE**
- Given a Water attack of 1000 vs a Fire level-1 target (table ratio 150) and an attacker with Deluge `val3` = 15.
- When damage passes the element step:
  - **PRE:** damage becomes 1000 + 1000×15/100 = 1150, then ×150/100 = **1725**.
  - **RE:** ratio becomes 150 + 15 = 165, and damage = 1000 − 1000×(100−165)/100 = **1650**.

**E-2 (BR-RULESET-025) RES**
- Given RE, a physical hit of 1000 after skill ratio, and target RES 100 (no ignore).
- Then reduction = 100/(100+400) × 0.8 = 0.16, so damage = 1000 − 160 = **840**.
- With the attacker ignoring 50%: RES becomes 50, reduction 50/450 × 0.8, and damage = 1000 − 88 = **912**.

**E-3 (BR-RULESET-020) Card fix multiplies, it does not add**
- Given PRE, damage 1000, race card +20% and size card +15%.
- Then cardfix = 1000 × 120/100 = 1200, then ×115/100 = 1380, so damage = **1380** (not 1350).

**E-4 (BR-RULESET-053 / 054) Stun resistance**
- Given a target with VIT 50, LUK 30, level 80 vs an attacker of level 80, and Stun at 50% (rate 5000) for 5000 ms.
- **PRE:** `sc_def` = 5000, `sc_def2` = 300. Rate = 5000 − 2500 − 300 = 2200 (22%). Duration = 5000 − 2500 − 300 = **2200 ms**.
- **RE:** `levelAdv` = 0, so rate = 5000 − 2500 = 2500 (25%). Duration = 5000 − 2500 + 500 = **3000 ms**.

**E-4b (BR-RULESET-067) Speed**
- Given base speed 150, Quagmire (slow 50) and Increase AGI (haste 25).
- Then speed rate = 100 + 50 − 25 = 125, so speed = 150 × 125/100 = **187**.

**E-5 (BR-RULESET-048) Heal level 5**
- Given base level 99, INT 80, lv 5, no Meditatio.
- **PRE:** (99+80)/8 = 22, ×(4 + 5×8) = 22 × 44 = **968**.
- **RE:** (179/5) = 35, ×30×5/10 = **525** (MATK is added elsewhere).

**E-6 (BR-RULESET-037) Dual wield**
- Given an Assassin with right-hand and left-hand skill 5 each, and both hands at 1000 before scaling.
- Then right = 1000 × (50+50)/100 = **1000**, left = 1000 × (30+50)/100 = **800**.

**E-7 (BR-RULESET-108) Item heal**
- Given a potion of 150 HP, VIT 50, Recovery lv 5, Potion Learning 0.
- Then bonus = 100 + 100 + 50 = 250, so HP restored = 150 × 250/100 = **375**.

**E-8 (BR-RULESET-072) Poison tick**
- Given a player with maxHP 10000 poisoned (not Deadly Poison).
- Then each tick = 2 + 10000×3/200 = **152**; ticks stop once HP ≤ max(2500, 152).

**E-9 (BR-RULESET-010) RE weapon ATK range**
- Given weapon ATK 100, weapon level 4, STR 80 (melee).
- Then variance = 5×100×4/100 = 20 and stat bonus = 100×80/200 = 40, so the roll is uniform in **[120, 160]**. Crit or Maximize gives 160.

**E-10 (BR-RULESET-060) Blessing on a cursed player**
- Given a player with Curse who receives Blessing.
- Then Curse ends, Blessing is not applied, and the call returns true.

## 8. Open questions for SMEs

- **Q1** (BR-RULESET-039): is Burnt's +666% vs fire attackers meant to be unconditional? The code only applies it inside the Dark Crow block (battle.cpp:1803-1804). Ship as-is or fix?
- **Q2** (BR-RULESET-008): PRE base damage uses `rnd()%(max−min)+min`, which never returns max (battle.cpp:2575). Is that official behavior to keep?
- **Q3** (BR-RULESET-118): must the six renewal sub-flags be independently selectable in the new packages, or only two bundles (pre-renewal and renewal)? Today `RENEWAL_ASPD` etc. can differ from `RENEWAL`.
- **Q4** (BR-RULESET-028, 035, 042): several formulas are marked `!TODO: Confirm` (Aura Blade RE at battle.cpp:4954, homunculus skills at 5727-5733, Dark Crow, Shadow Scar). Port as-is, or confirm against official data first?
- **Q5** (BR-RULESET-051): can status `val1..val4` be replaced by named fields per status, accepting a save-format migration for saved statuses?
- **Q6** (BR-RULESET-115): does the new runtime need bit-exact legacy results (including float32 steps and RNG draw counts), or only statistical parity? This sets the cost of the Luau numeric helpers.
- **Q7** (BR-RULESET-113): is mob AI in scope for scripting at all, or fully native with only mob skill conditions scripted?
- **Q8** (BR-RULESET-043): `battle_calc_attack_gvg_bg` contains a condition `(!skill_id || skill_id || …)` that is always true (battle.cpp:5137). Intent: reflect on every attack? Confirm before encoding.
- **Q9** (section 0 item 6): the approved architecture keeps the legacy NPC script VM and has no Luau layer. Is the Luau ruleset a new decision that supersedes REIMAGINED_ARCHITECTURE K2, or a later phase on top of it?

## 9. Not covered (out of scope or only skimmed)

- Rule bodies I did not read line by line: per-status stat functions other than STR, AGI, DEF, DEF2, SPEED and ASPD; most `skill_castend_*` per-skill code; mob skill DB conditions; `skill_counter_additional_effect`.
- Mastery tables beyond the first weapon types in `battle_addmastery`.
- Items: exact per-bonus semantics for the full set of 506 `SP_*` cases (BR-RULESET-098 covers shape and ordering only).