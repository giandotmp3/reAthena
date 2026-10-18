--!nonstrict
-- Pre-renewal pipeline stage order (BR-RULESET-018 weapon, BR-RULESET-044 magic).
-- Weapon: the order of battle_calc_weapon_attack as recorded by the golden stage probes. The BR-RULESET-018 text lists the card
-- fixes BEFORE the element table; the code applies the table first (battle.cpp:5674) and the card fixes after it (5757-5790).
-- With the text's order 62 of 5,424 golden outputs differ (truncation), so the code order is the contract.
-- Order is registered per ruleset because pre-renewal and renewal differ in order,
-- not only in constants. Stage bodies are native-defaults stages or Luau references
-- supplied later; this file declares order and opt-in only.
local M = {}

M.ORDER = {
	weapon = {
		"init", "base_attack", "skill_ratio", "const_add", "skill_atk_bonus",
		"defense", "post_defense", "element", "element_bonus", "card_attacker",
		"card_target", "div_hands", "target_modifiers", "map_rates", "final",
	},
	magic = {
		"init", "skill_ratio", "skill_atk_bonus", "mdef", "element",
		"card_target", "target_modifiers", "map_rates", "final",
	},
	misc = { "init", "fixed_formula", "target_modifiers", "map_rates", "final" },
}

-- Native opt-in by ID (architecture 1.1). Only populated if M0(a) shows Luau is too slow.
M.NATIVE_OPT_IN = {}

function M.register(engine)
	for kind, order in pairs(M.ORDER) do
		engine.pipeline.define(kind, order)
	end
	for _, opt in ipairs(M.NATIVE_OPT_IN) do
		engine.pipeline.use_native(opt.pipeline, opt.stage, opt.native_id, opt.params)
	end
end

return M
