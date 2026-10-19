--!nonstrict
-- Loads the status definitions (generated first draft defs/*.luau, hand-written defs_hand.luau, status records) once and exposes the
-- calculation API:
--   status = import("scripts/status/index")
--   status.calc(ctx, "agi", base, unit, active, cfg)      -- scripts/status/aggregate.luau header documents the arguments
--   status.maxhp(ctx, base, unit, active, cfg) / status.maxsp(...)       -- non-players
--   status.define / status.extend / status.rule / status.get              -- the front end (scripts/status/define.luau)
local define = import("scripts/status/define")
import("scripts/status/defs/defs_a_f")
import("scripts/status/defs/defs_g_l")
import("scripts/status/defs/defs_m_r")
import("scripts/status/defs/defs_s_z")
import("scripts/status/defs/rules_gen")
import("scripts/status/defs_hand")
define.load_records(import("data/status_records"))
local aggregate = import("scripts/status/aggregate")
local maxhp = import("scripts/status/maxhp")

local M = {}
for k, v in pairs(define) do M[k] = v end
M.calc, M.env, M.emit, M.fold, M.calc_env = aggregate.calc, aggregate.env, aggregate.emit, aggregate.fold, aggregate.calc_env

function M.maxhp(ctx, base, unit, active, cfg) return maxhp.calc(aggregate.env(ctx, unit, active, cfg), "hp", base) end
function M.bonus(ctx, which, kind, unit, active, cfg) return maxhp.bonus(aggregate.env(ctx, unit, active, cfg), which, kind) end
function M.maxsp(ctx, base, unit, active, cfg) return maxhp.calc(aggregate.env(ctx, unit, active, cfg), "sp", base) end

return M
