--!nonstrict
-- PROVISIONAL starter skills; per-level arrays follow BR-RULESET-078 shape.
-- Hooks reference formulas by name so behavior stays in scripts/rules.
local P = "ruleset.prerenewal:skill/"
return {
	{ id = P .. "sm_bash", client_id = 5, max_level = 10, type = "weapon", target = "attack",
	  sp = { 8, 8, 8, 8, 8, 8, 8, 8, 8, 15 }, hooks = { "calculate_skill_ratio" } },
	{ id = P .. "sm_provoke", client_id = 6, max_level = 10, type = "misc", target = "attack",
	  sp = { 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 }, hooks = { "apply_additional_effects" } },
	{ id = P .. "al_heal", client_id = 28, max_level = 10, type = "misc", target = "support",
	  sp = { 13, 16, 19, 22, 25, 28, 31, 34, 37, 40 }, hooks = { "castend_nodamage_id" } },
	{ id = P .. "al_incagi", client_id = 29, max_level = 10, type = "misc", target = "support",
	  sp = { 18, 21, 24, 27, 30, 33, 36, 39, 42, 45 }, hooks = { "castend_nodamage_id" } },
	{ id = P .. "al_blessing", client_id = 34, max_level = 10, type = "misc", target = "support",
	  sp = { 28, 32, 36, 40, 44, 48, 52, 56, 60, 64 }, hooks = { "castend_nodamage_id" } },
	{ id = P .. "mg_firebolt", client_id = 19, max_level = 10, type = "magic", target = "attack",
	  sp = { 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 }, hooks = { "modify_element" } },
}
