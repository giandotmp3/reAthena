--!nonstrict
-- Renewal pipeline stage order (BR-RULESET-019 weapon, 044 magic).
-- Weapon: the order of battle_calc_weapon_attack in the #ifdef RENEWAL branches (battle.cpp:5418-5822), VERIFIED against the stage
-- probes recorded from the real renewal server (conformance/golden: every case's probes occur in this order; tests/acceptance).
-- Differences from the BR-RULESET-019 text are listed in docs/ARCHITECTURE_PROOF.md section 5.
-- Magic is declared from BR-RULESET-044 only: no golden data covers it yet (marked below), so the order is unverified.
-- Stage names are the architecture 6.4 vocabulary; the pre-renewal package registers a different ORDER over (mostly) the same names.
-- The engine owns the stage loop (script-host pipeline.run); this package only declares the order and the stage bodies.
local M = {}

M.ORDER = {
	weapon = {
		"init", "base_attack", "card_attacker", "card_target_parts", "sc_bonus", "sum", "p_atk", "mastery", "crit_rate",
		"skill_ratio", "const_add", "adv_katar_mastery", "res", "defense", "post_defense", "skill_atk_bonus", "element",
		"crit_multiplier", "flat_additions", "card_target", "div_hands", "target_modifiers", "map_rates", "final",
	},
	-- UNVERIFIED (BR-RULESET-044, no golden data): card fix -> two-hand staff -> S.MATK -> ratio -> ... -> MDEF -> element
	magic = {
		"init", "base_attack", "card_attacker", "two_hand_staff", "s_matk", "skill_ratio", "skill_atk_bonus", "ignore_mdef",
		"mdef", "element", "target_modifiers", "map_rates", "final",
	},
}

-- Stages whose bodies are registered protected: replacing, cancelling or moving them needs the host's protected-stage permission
-- (a mod may still insert stages next to them). `init` sets up the attack and `final` closes it (BR-RULESET-001 tail).
M.PROTECTED = { init = true, final = true }

-- Declares the stage orders and registers the stage bodies. The magic order is declared only (no bodies: UNVERIFIED, see above).
function M.register(engine)
	for kind, order in pairs(M.ORDER) do
		engine.pipeline.define(kind, order)
	end
	import("scripts/damage/weapon").register(engine)
end

return M
