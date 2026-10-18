--!nonstrict
-- Pure renewal formulas with a stable call shape (BR-RULESET-010, 025, 034). The same code backs the damage stages
-- (scripts/damage/weapon.luau), so the golden data that validates the stages validates these entry points too.
-- Formulas without golden data (magic, heal, status resistance) stay pending: they raise NotImplemented.
local weapon = import("scripts/damage/weapon")
local fp = import("scripts/damage/fp")
local tables = import("data/damage_cfg")

local M = {}

local function pending(rule)
	error("NotImplemented: " .. rule)
end

-- BR-RULESET-025: damage after RES (res_reduction in weapon.luau)
function M.res_reduction(damage, target_res, ignore_res_pct)
	return weapon.res_reduction(damage, target_res, ignore_res_pct, tables.max_res_mres_ignored)
end

-- BR-RULESET-034: crit damage, floored in float32; players x(1.4f + 0.01f * crate), others x1.4f
function M.crit_damage(damage, crate, is_player)
	local mult = fp.f32(1.4)
	if is_player then mult = fp.add(mult, fp.mul(fp.f32(0.01), crate)) end
	return math.floor(fp.mul(fp.f32(damage), mult))
end

-- BR-RULESET-010: weapon ATK range [min, max] before the roll (weapon ATK, weapon level, STR or DEX), float32 as in the legacy code
function M.weapon_atk_range(atk, weapon_lv, stat)
	return weapon.weapon_atk_range(atk, atk, weapon_lv, stat)
end

function M.level_advantage(src_lv, tgt_lv) return pending("BR-RULESET-054") end
function M.status_rate_resist(stat, level_adv) return pending("BR-RULESET-054") end
function M.mdef_reduce(damage, mdef, mdef2) return pending("BR-RULESET-045") end
function M.heal_amount(base_lv, int, skill_lv) return pending("BR-RULESET-048") end
function M.delayed_status_duration(duration_ms, delay_ms) return pending("BR-RULESET-057") end

return M
