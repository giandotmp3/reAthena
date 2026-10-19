#!/usr/bin/env python3
"""Applies the RENEWAL golden-generator edits to a COPY of the legacy tree (never to the repository).

usage: apply_edits.py <copy-root>

Edits (all anchored on exact legacy text; the script aborts if an anchor is not found exactly once; it is idempotent):
  src/common/random.hpp/.cpp  - optional draw log: every rnd() / rnd_value() result is appended to g_golden_rnd_log when it
                                is non-null (no effect on the stream).
  src/map/battle.cpp          - GSTAGE(...) stage probes inside the RENEWAL branches of battle_calc_weapon_attack (read-only
                                observations of wd.* fields) and `#include "golden_hook.inc"` at the end, so the harness can
                                call the file-static legacy functions.
  src/map/map.cpp             - golden_main() call after ScriptHost::run_oninit() (no-op unless RA_GOLDEN_CASES is set), and the same for
                                golden_status_main() (no-op unless RA_GOLDEN_ST_CASES is set).
  src/map/status.cpp          - `#include "golden_status_hook.inc"` at the end (the status_calc_* golden runner, calls the static functions).
The random-draw and map.cpp edits are the same as the pre-renewal generator's; the probes that sit in shared code are the same
anchors, the ones in #ifndef RENEWAL blocks are replaced by probes in the #ifdef RENEWAL blocks.
"""
import sys, pathlib

root = pathlib.Path(sys.argv[1])


def edit(rel, pairs):
    p = root / rel
    s = p.read_text()
    for old, new in pairs:
        if new in s:
            continue  # already applied
        n = s.count(old)
        if n != 1:
            sys.exit(f"{rel}: anchor found {n} times (need exactly 1): {old[:90]!r}")
        s = s.replace(old, new)
    p.write_text(s)


# ---- random draw log -------------------------------------------------------------------------------------
edit("src/common/random.hpp", [
("inline std::mt19937 generator = std::mt19937(device());\n",
 "inline std::mt19937 generator = std::mt19937(device());\n\n"
 "// golden generator: optional log of every draw (kind 0 = rnd(), kind 1 = rnd_value(lo, hi)); null = off\n"
 "struct GoldenRnd { int kind; long long lo, hi, val; };\n"
 "inline std::vector<GoldenRnd>* g_golden_rnd_log = nullptr;\n"),
("	std::uniform_int_distribution<T> dist(min, max);\n	return dist(generator);\n",
 "	std::uniform_int_distribution<T> dist(min, max);\n	T golden_val_ = dist(generator);\n"
 "	if (g_golden_rnd_log) g_golden_rnd_log->push_back({1, (long long)min, (long long)max, (long long)golden_val_});\n"
 "	return golden_val_;\n"),
])
edit("src/common/random.cpp", [
("	return int31_distribution( generator );\n",
 "	int32 golden_val_ = int31_distribution( generator );\n"
 "	if( g_golden_rnd_log ) g_golden_rnd_log->push_back( { 0, 0, 0, golden_val_ } );\n"
 "	return golden_val_;\n"),
])

# ---- stage probes + hook include -------------------------------------------------------------------------
G = lambda n, a="wd.damage", b="wd.damage2": f'GSTAGE("{n}", {a}, {b});\n'
edit("src/map/battle.cpp", [
('#include "battle.hpp"\n',
 '#include "battle.hpp"\n#include "golden_stage.hpp" // golden generator probe (copy only)\n'),
# after the ATK parts (players) or the base damage (mobs, PRE-style path) are known
("			battle_calc_skill_base_damage(&wd, src, target, skill_id, skill_lv); // base skill damage\n",
 "			battle_calc_skill_base_damage(&wd, src, target, skill_id, skill_lv); // base skill damage\n"
 "		" + G("base") + "		" + G("p_status", "wd.statusAtk", "wd.statusAtk2") + "		" + G("p_weapon", "wd.weaponAtk", "wd.weaponAtk2")
 + "		" + G("p_equip", "wd.equipAtk", "wd.equipAtk2") + "		" + G("p_mastery", "wd.masteryAtk", "wd.percentAtk")),
# after the card fixes on the parts and the status ATK bonuses (still parts), before the sum
("		// final attack bonuses that aren't affected by cards\n		battle_attack_sc_bonus(&wd, src, target, skill_id, skill_lv);\n\n		if (sd) { //monsters, homuns and pets have their damage computed directly\n",
 "		// final attack bonuses that aren't affected by cards\n		battle_attack_sc_bonus(&wd, src, target, skill_id, skill_lv);\n"
 "		" + G("c_status", "wd.statusAtk", "wd.statusAtk2") + "		" + G("c_weapon", "wd.weaponAtk", "wd.weaponAtk2")
 + "		" + G("c_equip", "wd.equipAtk", "wd.equipAtk2") + "		" + G("c_mastery", "wd.masteryAtk", "wd.masteryAtk2")
 + "\n		if (sd) { //monsters, homuns and pets have their damage computed directly\n"),
# sum of the parts, P.ATK, mastery, crit_atk_rate, short/long rates
("		}\n\n		// Skill ratio\n		ATK_RATE(wd.damage, wd.damage2, battle_calc_attack_skill_ratio(&wd, src, target, skill_id, skill_lv));\n\n		// Additive damage bonus\n		ATK_ADD(wd.damage, wd.damage2, battle_calc_skill_constant_addition(&wd, src, target, skill_id, skill_lv));\n\n		// Advance Katar Mastery\n",
 "		}\n		" + G("sum") + "\n		// Skill ratio\n		ATK_RATE(wd.damage, wd.damage2, battle_calc_attack_skill_ratio(&wd, src, target, skill_id, skill_lv));\n"
 "		" + G("ratio") + "\n		// Additive damage bonus\n		ATK_ADD(wd.damage, wd.damage2, battle_calc_skill_constant_addition(&wd, src, target, skill_id, skill_lv));\n"
 "		" + G("const_add") + "\n		// Advance Katar Mastery\n"),
# RES (probe before it so the ratio-to-RES step is visible), then after
("		// Res reduces physical damage by a percentage and is calculated before DEF and other reductions.\n",
 "		" + G("katar") + "		// Res reduces physical damage by a percentage and is calculated before DEF and other reductions.\n"),
("		if (wd.damage + wd.damage2) {\n#ifdef RENEWAL\n			// Check if attack ignores DEF (in pre-renewal we need to update base damage even when the skill ignores DEF)\n",
 "		" + G("res") + "		if (wd.damage + wd.damage2) {\n#ifdef RENEWAL\n			// Check if attack ignores DEF (in pre-renewal we need to update base damage even when the skill ignores DEF)\n"),
# post defense (after the DEF reduction, shared anchor of the PRE generator is the defred probe below)
("			battle_calc_attack_post_defense(&wd, src, target, skill_id, skill_lv);\n		}\n\n#ifdef RENEWAL\n		// add any miscellaneous player ATK bonuses\n",
 "			battle_calc_attack_post_defense(&wd, src, target, skill_id, skill_lv);\n			" + G("postdef") + "		}\n\n#ifdef RENEWAL\n		// add any miscellaneous player ATK bonuses\n"),
# defense / element probes (same anchor as the PRE generator; the code is shared)
("	battle_calc_element_damage(&wd, src, target, skill_id, skill_lv);\n\n#ifdef RENEWAL\n	if (is_attack_critical(&wd, src, target, skill_id, skill_lv, false)) {",
 "	" + G("defense") + "	battle_calc_element_damage(&wd, src, target, skill_id, skill_lv);\n	" + G("element") + "\n#ifdef RENEWAL\n	if (is_attack_critical(&wd, src, target, skill_id, skill_lv, false)) {"),
# after the x1.4 crit multiplier and crit_def_rate
("	switch (skill_id) {\n		case TK_DOWNKICK:\n		case TK_STORMKICK:\n",
 "	" + G("crit") + "	switch (skill_id) {\n		case TK_DOWNKICK:\n		case TK_STORMKICK:\n"),
("	if(sd) {\n#ifndef RENEWAL\n		uint16 skill;\n\n		if ((skill = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)",
 "	" + G("pre_card") + "	if(sd) {\n#ifndef RENEWAL\n		uint16 skill;\n\n		if ((skill = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)"),
("#ifdef RENEWAL\n	// In renewal only do it for non player attacks\n	if( tsd && !sd ){",
 "	" + G("card_atk") + "#ifdef RENEWAL\n	// In renewal only do it for non player attacks\n	if( tsd && !sd ){"),
("	// only do 1 dmg to plant, no need to calculate rest\n	if(infdef){\n		battle_calc_attack_plant(&wd, src, target, skill_id, skill_lv);\n		return wd;\n	}\n",
 "	" + G("card_tgt") + "	// only do 1 dmg to plant, no need to calculate rest\n	if(infdef){\n		battle_calc_attack_plant(&wd, src, target, skill_id, skill_lv);\n		return wd;\n	}\n"),
("	battle_apply_div_fix(&wd, skill_id);\n\n	battle_calc_attack_left_right_hands(&wd, src, target, skill_id, skill_lv);\n",
 "	battle_apply_div_fix(&wd, skill_id);\n	" + G("div") + "\n	battle_calc_attack_left_right_hands(&wd, src, target, skill_id, skill_lv);\n	" + G("hands")),
("	battle_calc_weapon_final_atk_modifiers(&wd, src, target, skill_id, skill_lv);\n\n	battle_absorb_damage(target, &wd);",
 "	" + G("map") + "	battle_calc_weapon_final_atk_modifiers(&wd, src, target, skill_id, skill_lv);\n	" + G("final") + "\n	battle_absorb_damage(target, &wd);"),
])
edit("src/map/battle.cpp", [
# base damage range used for monsters / PRE-style base damage
("	if (sc && sc->getSCE(SC_MAXIMIZEPOWER))\n		atkmin = atkmax;\n\n	//Weapon Damage calculation",
 "	GSTAGE(\"basemm\", atkmin, atkmax);\n	if (sc && sc->getSCE(SC_MAXIMIZEPOWER))\n		atkmin = atkmax;\n\n	//Weapon Damage calculation"),
# renewal player weapon ATK range actually rolled in (after the float32 variance step)
("		if ((sc && sc->getSCE(SC_MAXIMIZEPOWER)) || critical == true)\n			damage = atkmax;\n		else\n			damage = rnd_value(atkmin, atkmax);",
 "		GSTAGE(\"watk_mm\", atkmin, atkmax);\n		if ((sc && sc->getSCE(SC_MAXIMIZEPOWER)) || critical == true)\n			damage = atkmax;\n		else\n			damage = rnd_value(atkmin, atkmax);"),
# final hit rate (after clamp) and crit chance compared with the draw
("	// BR-MAP-011 (map-rules): rnd()%100 < hitrate, drawn even when hitrate <= 0\n	return rules::combat::roll_hit( hitrate, rules_rng() );",
 "	GSTAGE(\"hitrate\", hitrate, flee);\n	// BR-MAP-011 (map-rules): rnd()%100 < hitrate, drawn even when hitrate <= 0\n	return rules::combat::roll_hit( hitrate, rules_rng() );"),
("		// BR-MAP-010 (map-rules): rnd()%1000 < cri, drawn even when cri <= 0\n		return rules::combat::roll_critical( cri, rules_rng() );",
 "		GSTAGE(\"cri\", cri, 0);\n		// BR-MAP-010 (map-rules): rnd()%1000 < cri, drawn even when cri <= 0\n		return rules::combat::roll_critical( cri, rules_rng() );"),
# DEF values after crowd penalty, soft DEF actually used
("	//Damage reduction based on vitality\n	if (tsd) {	//Sd vit-eq",
 "	GSTAGE(\"defs\", def1, def2);\n	//Damage reduction based on vitality\n	if (tsd) {	//Sd vit-eq"),
("	if (battle_config.weapon_defense_type) {\n		vit_def += def1*battle_config.weapon_defense_type;",
 "	GSTAGE(\"vitdef\", vit_def, def1);\n	if (battle_config.weapon_defense_type) {\n		vit_def += def1*battle_config.weapon_defense_type;"),
("	int32 right_element = battle_get_weapon_element(*wd, *src, *target, skill_id, skill_lv, EQI_HAND_R, true);\n\n	// Elemental attribute fix",
 "	int32 right_element = battle_get_weapon_element(*wd, *src, *target, skill_id, skill_lv, EQI_HAND_R, true);\n	GSTAGE(\"ele\", right_element, battle_get_weapon_element(*wd, *src, *target, skill_id, skill_lv, EQI_HAND_L, true));\n\n	// Elemental attribute fix"),
("	// Cap damage to 0\n	if (battle_config.attr_recover == 0)\n		battle_min_damage(*wd, *src, skill_id, 0);\n",
 "	GSTAGE(\"attr\", wd->damage, wd->damage2);\n	// Cap damage to 0\n	if (battle_config.attr_recover == 0)\n		battle_min_damage(*wd, *src, skill_id, 0);\n"),
])
# the RE damage reduction: damage right before/after rules::combat::apply_defense_re
edit("src/map/battle.cpp", [
("	// Post skill/vit reduction damage increases\n#ifndef RENEWAL\n	//Refine bonus",
 "	GSTAGE(\"defred\", wd->damage, wd->damage2);\n	// Post skill/vit reduction damage increases\n#ifndef RENEWAL\n	//Refine bonus"),
])

# hook include at the very end of battle.cpp
p = root / "src/map/battle.cpp"
s = p.read_text()
if "golden_hook.inc" not in s:
    s += '\n#include "golden_hook.inc" // golden generator (copy only): synthetic units + case runner\n'
p.write_text(s)

edit("src/map/map.cpp", [
("	ScriptHost::run_oninit();	// Init npcs (OnInit)\n",
 "	ScriptHost::run_oninit();	// Init npcs (OnInit)\n	::golden_main(); // golden generator (no-op unless RA_GOLDEN_CASES is set)\n"),
("#include \"storage.hpp\"\n",
 "void golden_main(); // golden generator (copy only)\n#include \"storage.hpp\"\n"),
])

# ---- status-calculation hook (golden_status_hook.inc): included at the end of status.cpp so it can call the static status_calc_* ----
p = root / "src/map/status.cpp"
s = p.read_text()
if "golden_status_hook.inc" not in s:
    s += '\n#include "golden_status_hook.inc" // status golden generator (copy only): synthetic units + status_calc_* runner\n'
p.write_text(s)
edit("src/map/map.cpp", [
("	::golden_main(); // golden generator (no-op unless RA_GOLDEN_CASES is set)\n",
 "	::golden_status_main(); // status golden generator (no-op unless RA_GOLDEN_ST_CASES is set)\n"
 "	::golden_main(); // golden generator (no-op unless RA_GOLDEN_CASES is set)\n"),
("void golden_main(); // golden generator (copy only)\n",
 "void golden_main(); // golden generator (copy only)\nvoid golden_status_main(); // status golden generator (copy only)\n"),
])
print("edits applied")
