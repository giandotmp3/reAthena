--!strict
-- API stubs for renewal formulas. Each is pending (rule ID in the message) until implemented.
local M = {}

local function pending(rule: string): never
  error("NotImplemented: " .. rule)
end

function M.res_reduction(damage: number, target_res: number, ignore_res_pct: number): number -- BR-RULESET-025
  return pending("BR-RULESET-025")
end
function M.level_advantage(src_lv: number, tgt_lv: number): number -- BR-RULESET-054
  return pending("BR-RULESET-054")
end
function M.status_rate_resist(stat: number, level_adv: number): number -- BR-RULESET-054
  return pending("BR-RULESET-054")
end
function M.crit_damage(damage: number, crate: number, is_player: boolean): number -- BR-RULESET-034
  return pending("BR-RULESET-034")
end
function M.weapon_atk_range(atk: number, weapon_lv: number, stat: number): (number, number) -- BR-RULESET-010
  return pending("BR-RULESET-010")
end
function M.mdef_reduce(damage: number, mdef: number, mdef2: number): number -- BR-RULESET-045
  return pending("BR-RULESET-045")
end
function M.heal_amount(base_lv: number, int: number, skill_lv: number): number -- BR-RULESET-048
  return pending("BR-RULESET-048")
end
function M.delayed_status_duration(duration_ms: number, delay_ms: number): number -- BR-RULESET-057
  return pending("BR-RULESET-057")
end

return M
