--!nonstrict
-- The status layer of ruleset.renewal: the attribute stage lists (attrs_re), the status definitions (defs/*) and the aggregator.
--
--   local S = import("scripts/status/init")
--   S.calc("flee", 120, unit, { blessing = {10, 5, 0, 0} })     -- status_calc_flee equivalent
--   S.states(unit, active)                                       -- status_calc_state equivalent
local status = import("scripts/status/status")
local aggregate = import("scripts/status/aggregate")
local states = import("scripts/status/states")

local M = { status = status, aggregate = aggregate, states = states }

aggregate.install()
import("scripts/status/defs/a_c")
import("scripts/status/defs/d_i")
import("scripts/status/defs/i_r")
import("scripts/status/defs/s_z")
import("scripts/status/defs/states")

M.calc = aggregate.calc

function M.states_of(unit, active) return states.compute(unit, active) end

return M
