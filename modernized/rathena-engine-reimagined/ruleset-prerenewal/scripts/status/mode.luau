--!nonstrict
-- status_calc_mode (status.cpp:8895-8908): Mode Change sets (val2), adds (val3) and removes (val4) mode bits.
local ops = import("scripts/status/ops")
local M = {}

function M.calc(e, base)
	local K = e.K
	local mode = base
	local mc = e.sc.MODECHANGE
	if mc then -- 8899
		if mc.mode_set ~= 0 then mode = ops.bor(ops.band(mode, ops.bnot(K.MD_MASK)), mc.mode_set) end -- 8901 set mode
		if mc.mode_add ~= 0 then mode = ops.bor(mode, mc.mode_add) end -- 8903 add mode
		if mc.mode_del ~= 0 then mode = ops.band(mode, ops.bnot(mc.mode_del)) end -- 8905 del mode
	end
	return ops.cap(mode, K.MD_NONE, K.INT_MAX) -- 8907
end

return M
