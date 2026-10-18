--!nonstrict
-- BR-RULESET-118: ruleset package manifest. Values mirror mod.toml [ruleset].
-- Battle-config defaults are the documented conf/battle tunables, as data.
local M = {}

function M.manifest()
	return {
		mode = "pre-renewal",
		db_set = "db/pre-re",
		subflags = { cast = false, drop = false, exp = false, lvdmg = false, aspd = false, stat = false, edp = false },
		battle_defaults = {
			hit_rate_min = 5,
			hit_rate_max = 100,
			max_res_mres_ignored = 50,
			attack_attr_none = 14,
			left_cardfix_to_right = true,
			cardfix_monster_physical = true,
		},
		legacy_quirks = true,
	}
end

return M
