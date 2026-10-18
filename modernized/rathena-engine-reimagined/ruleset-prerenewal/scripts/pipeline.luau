--!strict-compatible
-- Pre-renewal pipeline stage order (BR-RULESET-018 weapon, BR-RULESET-044 magic).
-- Order is registered per ruleset because pre-renewal and renewal differ in order,
-- not only in constants. Stage bodies are native-defaults stages or Luau references
-- supplied later; this file declares order and opt-in only.
local M = {}

M.ORDER = {
	weapon = {
		"init", "base_attack", "skill_ratio", "const_add", "skill_atk_bonus",
		"defense", "post_defense", "card_attacker", "card_target", "element",
		"element_bonus", "div_hands", "target_modifiers", "map_rates", "final",
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
