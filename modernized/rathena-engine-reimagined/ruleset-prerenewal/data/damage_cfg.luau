--!nonstrict
-- battle.conf tunables read by the weapon pipeline (BR-RULESET-118). Defaults of conf/battle/*.conf as the
-- legacy binary loads them; verified against the server by conformance/golden_gen (dump of battle_config).
-- Booleans of the conf are 0/1 here because the legacy code compares integers.
return {
	min_hitrate = 5, max_hitrate = 100,
	attack_attr_none = 14,             -- BL_ mask of attacker types whose basic attacks skip the element table (MOB|PET|HOM)
	left_cardfix_to_right = 1,         -- left-hand card bonuses are merged into the right hand
	attr_recover = 0,                  -- 0: damage never goes below 0 after the element table (negative ratios do not heal)
	cardfix_monster_physical = 1,
	skill_min_damage = 0,
	weapon_defense_type = 0,
	agi_penalty_type = 1, agi_penalty_target = 1, agi_penalty_count = 3, agi_penalty_num = 10,
	vit_penalty_type = 1, vit_penalty_target = 1, vit_penalty_count = 3, vit_penalty_num = 5,
	gvg_short_damage_rate = 80, gvg_long_damage_rate = 80,
	pk_mode = 0, ksprotection = 0,
}
