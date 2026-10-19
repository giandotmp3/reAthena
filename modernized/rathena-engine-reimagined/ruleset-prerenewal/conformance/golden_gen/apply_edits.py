#!/usr/bin/env python3
"""Applies the golden-generator edits to a COPY of the legacy tree (never to the repository).

usage: apply_edits.py <copy-root>

Edits (all anchored on exact legacy text; the script aborts if an anchor is not found exactly once):
  src/common/random.hpp/.cpp  - optional draw log: every rnd() / rnd_value() result is appended to
                                g_golden_rnd_log when it is non-null (no effect on the stream).
  src/map/battle.cpp          - GSTAGE(...) stage probes inside battle_calc_weapon_attack (read-only
                                observations of wd.damage/damage2) and `#include "golden_hook.inc"` at the end,
                                so the harness can call the file-static legacy functions.
  src/map/map.cpp             - golden_main() call after ScriptHost::run_oninit() (no-op unless RA_GOLDEN_CASES is set), then
                                golden_status_main() (no-op unless RA_GOLDEN_ST_CASES / RA_GOLDEN_ST_CONSTS is set).
  src/map/pc.cpp              - a push_back in pc_bonus .. pc_bonus5 into g_golden_bonus_log (null = off): the log of what a status script's `bonus` commands do.
  src/map/status.cpp          - `#include "golden_status_hook.inc"` at the end, so the file-static status_calc_* functions can be called.
"""
import sys, re, pathlib

root = pathlib.Path(sys.argv[1])

def write_if_changed(p, text):
    """Keeps the mtime of unchanged files, so a rebuild only recompiles what the edits really changed."""
    if p.read_text() != text:
        p.write_text(text)


def edit(rel, pairs):
    p = root / rel
    s = p.read_text()
    for old, new in pairs:
        n = s.count(old)
        if new in s:
            continue  # already applied (the script is idempotent)
        if n != 1:
            sys.exit(f"{rel}: anchor found {n} times (need exactly 1): {old[:70]!r}")
        s = s.replace(old, new)
    write_if_changed(p, s)

# ---- random draw log ---------------------------------------------------------------------------
edit("src/common/random.hpp", [
("inline std::mt19937 generator = std::mt19937(device());\n",
 "inline std::mt19937 generator = std::mt19937(device());\n\n"
 "// golden generator: optional log of every draw (kind 0 = rnd(), kind 1 = rnd_value(lo, hi)); null = off\n"
 "struct GoldenRnd { int kind; long long lo, hi, val; };\n"
 "inline std::vector<GoldenRnd>* g_golden_rnd_log = nullptr;\n"
 "// golden status generator: optional log of the pc_bonus* calls a status script makes ({argument count, type, ..., value}); null = off\n"
 "inline std::vector<std::vector<long long>>* g_golden_bonus_log = nullptr;\n"),
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

# ---- stage probes + hook include -----------------------------------------------------------------
STAGE = lambda n: f'GSTAGE("{n}", wd.damage, wd.damage2);\n'
edit("src/map/battle.cpp", [
# header: probe macro
('#include "battle.hpp"\n',
 '#include "battle.hpp"\n#include "golden_stage.hpp" // golden generator probe (copy only)\n'),
# base damage of the normal attack (PRE: skill_id 0 call), incl. crit/non-crit atk rate
("		battle_calc_skill_base_damage(&wd, src, target, 0, 0); // base damage\n		wd.basedamage = wd.damage;\n",
 "		battle_calc_skill_base_damage(&wd, src, target, 0, 0); // base damage\n		wd.basedamage = wd.damage;\n		" + STAGE("base")),
# after skill ratio + constant addition (PRE branch)
("		ATK_ADD(wd.damage, wd.damage2, battle_calc_skill_constant_addition(&wd, src, target, skill_id, skill_lv));\n#endif\n\n#ifdef RENEWAL\n		if(skill_id == HW_MAGICCRASHER)",
 "		ATK_ADD(wd.damage, wd.damage2, battle_calc_skill_constant_addition(&wd, src, target, skill_id, skill_lv));\n		" + STAGE("ratio") + "#endif\n\n#ifdef RENEWAL\n		if(skill_id == HW_MAGICCRASHER)"),
# after status ATK% (PRE branch of battle_attack_sc_bonus)
("#else\n		// final attack bonuses that aren't affected by cards\n		battle_attack_sc_bonus(&wd, src, target, skill_id, skill_lv);\n#endif\n",
 "#else\n		// final attack bonuses that aren't affected by cards\n		battle_attack_sc_bonus(&wd, src, target, skill_id, skill_lv);\n		" + STAGE("sc_bonus") + "#endif\n"),
# after DEF + post-defense, before element
("	battle_calc_element_damage(&wd, src, target, skill_id, skill_lv);\n\n#ifdef RENEWAL\n	if (is_attack_critical(&wd, src, target, skill_id, skill_lv, false)) {",
 "	" + STAGE("defense") + "	battle_calc_element_damage(&wd, src, target, skill_id, skill_lv);\n	" + STAGE("element") + "\n#ifdef RENEWAL\n	if (is_attack_critical(&wd, src, target, skill_id, skill_lv, false)) {"),
# attacker card fix (and flat additions before it)
("	if(sd) {\n#ifndef RENEWAL\n		uint16 skill;\n\n		if ((skill = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)",
 "	" + STAGE("pre_card") + "	if(sd) {\n#ifndef RENEWAL\n		uint16 skill;\n\n		if ((skill = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)"),
("#ifdef RENEWAL\n	// In renewal only do it for non player attacks\n	if( tsd && !sd ){",
 "	" + STAGE("card_atk") + "#ifdef RENEWAL\n	// In renewal only do it for non player attacks\n	if( tsd && !sd ){"),
("	// only do 1 dmg to plant, no need to calculate rest\n	if(infdef){\n		battle_calc_attack_plant(&wd, src, target, skill_id, skill_lv);\n		return wd;\n	}\n",
 "	" + STAGE("card_tgt") + "	// only do 1 dmg to plant, no need to calculate rest\n	if(infdef){\n		battle_calc_attack_plant(&wd, src, target, skill_id, skill_lv);\n		return wd;\n	}\n"),
("	battle_apply_div_fix(&wd, skill_id);\n\n	battle_calc_attack_left_right_hands(&wd, src, target, skill_id, skill_lv);\n",
 "	battle_apply_div_fix(&wd, skill_id);\n	" + STAGE("div") + "\n	battle_calc_attack_left_right_hands(&wd, src, target, skill_id, skill_lv);\n	" + STAGE("hands")),
("	battle_calc_weapon_final_atk_modifiers(&wd, src, target, skill_id, skill_lv);\n\n	battle_absorb_damage(target, &wd);",
 "	" + STAGE("map") + "	battle_calc_weapon_final_atk_modifiers(&wd, src, target, skill_id, skill_lv);\n	" + STAGE("final") + "\n	battle_absorb_damage(target, &wd);"),
])
edit("src/map/battle.cpp", [
# base damage range actually used (PC: dex-scaled min .. weapon atk; mob: atk .. atk2)
("	if (sc && sc->getSCE(SC_MAXIMIZEPOWER))\n		atkmin = atkmax;\n\n	//Weapon Damage calculation",
 "	GSTAGE(\"basemm\", atkmin, atkmax);\n	if (sc && sc->getSCE(SC_MAXIMIZEPOWER))\n		atkmin = atkmax;\n\n	//Weapon Damage calculation"),
# final hit rate (after clamp) and the crit chance that is compared with the draw
("	// BR-MAP-011 (map-rules): rnd()%100 < hitrate, drawn even when hitrate <= 0\n	return rules::combat::roll_hit( hitrate, rules_rng() );",
 "	GSTAGE(\"hitrate\", hitrate, flee);\n	// BR-MAP-011 (map-rules): rnd()%100 < hitrate, drawn even when hitrate <= 0\n	return rules::combat::roll_hit( hitrate, rules_rng() );"),
("		// BR-MAP-010 (map-rules): rnd()%1000 < cri, drawn even when cri <= 0\n		return rules::combat::roll_critical( cri, rules_rng() );",
 "		GSTAGE(\"cri\", cri, 0);\n		// BR-MAP-010 (map-rules): rnd()%1000 < cri, drawn even when cri <= 0\n		return rules::combat::roll_critical( cri, rules_rng() );"),
# DEF values after crowd penalty, vit DEF actually used
("	//Damage reduction based on vitality\n	if (tsd) {	//Sd vit-eq",
 "	GSTAGE(\"defs\", def1, def2);\n	//Damage reduction based on vitality\n	if (tsd) {	//Sd vit-eq"),
("	if (battle_config.weapon_defense_type) {\n		vit_def += def1*battle_config.weapon_defense_type;",
 "	GSTAGE(\"vitdef\", vit_def, def1);\n	if (battle_config.weapon_defense_type) {\n		vit_def += def1*battle_config.weapon_defense_type;"),
("	int32 right_element = battle_get_weapon_element(*wd, *src, *target, skill_id, skill_lv, EQI_HAND_R, true);\n\n	// Elemental attribute fix",
 "	int32 right_element = battle_get_weapon_element(*wd, *src, *target, skill_id, skill_lv, EQI_HAND_R, true);\n	GSTAGE(\"ele\", right_element, battle_get_weapon_element(*wd, *src, *target, skill_id, skill_lv, EQI_HAND_L, true));\n\n	// Elemental attribute fix"),
])

edit("src/map/battle.cpp", [
# damage right after the DEF reduction, before the refine bonus / min 1 of the post-defense step
("	// Post skill/vit reduction damage increases\n#ifndef RENEWAL\n	//Refine bonus",
 "	GSTAGE(\"defred\", wd->damage, wd->damage2);\n	// Post skill/vit reduction damage increases\n#ifndef RENEWAL\n	//Refine bonus"),
# damage right after the element table, before the PRE star-crumb / EDP additions
("#ifndef RENEWAL\n	// These mastery bonuses are non-elemental and should apply even if the attack misses",
 "	GSTAGE(\"attr\", wd->damage, wd->damage2);\n#ifndef RENEWAL\n	// These mastery bonuses are non-elemental and should apply even if the attack misses"),
])

# hook include at the very end of battle.cpp
p = root / "src/map/battle.cpp"
s = p.read_text()
if "golden_hook.inc" not in s:
    s += '\n#include "golden_hook.inc" // golden generator (copy only): synthetic units + case runner\n'
write_if_changed(p, s)

edit("src/map/map.cpp", [
("	ScriptHost::run_oninit();	// Init npcs (OnInit)\n",
 "	ScriptHost::run_oninit();	// Init npcs (OnInit)\n	::golden_main(); // golden generator (no-op unless RA_GOLDEN_CASES is set)\n"),
("#include \"storage.hpp\"\n",
 "void golden_main(); // golden generator (copy only)\n#include \"storage.hpp\"\n"),
])

# ---- bonus log (statuses with a Script: the bonus commands they run) ------------------------------------------------------------
def bonus_log(n, sig):
    args = {1: "type, val", 2: "type, type2, val", 3: "type, type2, type3, val", 4: "type, type2, type3, type4, val", 5: "type, type2, type3, type4, type5, val"}[n]
    return (sig + "\n{\n", sig + "\n{\n\tif( g_golden_bonus_log ) g_golden_bonus_log->push_back( { " + str(n) + "LL, " + ", ".join(f"(long long){a.strip()}" for a in args.split(",")) + " } );\n")
edit("src/map/pc.cpp", [
    bonus_log(1, "void pc_bonus(map_session_data *sd,int32 type,int32 val)"),
    bonus_log(2, "void pc_bonus2(map_session_data *sd,int32 type,int32 type2,int32 val)"),
    bonus_log(3, "void pc_bonus3(map_session_data *sd,int32 type,int32 type2,int32 type3,int32 val)"),
    bonus_log(4, "void pc_bonus4(map_session_data *sd,int32 type,int32 type2,int32 type3,int32 type4,int32 val)"),
    bonus_log(5, "void pc_bonus5(map_session_data *sd,int32 type,int32 type2,int32 type3,int32 type4,int32 type5,int32 val)"),
])

# ---- status calculation hook (golden_status_hook.inc, generated golden_status_sc.inc / golden_status_consts.inc) ----------------
p = root / "src/map/status.cpp"
s = p.read_text()
if "golden_status_hook.inc" not in s:
    s += '\n#include "golden_status_hook.inc" // golden generator (copy only): synthetic units + status_calc_* runner\n'
write_if_changed(p, s)
edit("src/map/map.cpp", [
("	::golden_main(); // golden generator (no-op unless RA_GOLDEN_CASES is set)\n",
 "	::golden_main(); // golden generator (no-op unless RA_GOLDEN_CASES is set)\n	::golden_status_main(); // status calculation golden (no-op unless RA_GOLDEN_ST_CASES / RA_GOLDEN_ST_CONSTS is set)\n"),
("void golden_main(); // golden generator (copy only)\n",
 "void golden_main(); // golden generator (copy only)\nvoid golden_status_main(); // status calculation golden (copy only)\n"),
])
print("edits applied")
