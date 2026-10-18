--!nonstrict
-- Unit snapshot normaliser: fills the defaults the weapon pipeline relies on (it reads `u.flee2 ~= 0`, `u.rhw.wlv`, ...).
-- Returns a new table; bonus maps and status tables are shared with the input, they are only read.
local M = {}

local ZERO_FIELDS = { "level", "str", "agi", "vit", "int", "dex", "luk", "batk", "hit", "cri", "flee", "flee2", "def", "def2",
	"size", "race", "element", "targeted", "wtype1", "wtype2" }

local function hand(h)
	local out = { atk = 0, atk2 = 0, ele = 0, wlv = 0 }
	if h then for k, v in pairs(h) do out[k] = v end end
	return out
end

function M.normalize(u)
	local out = {}
	for k, v in pairs(u) do out[k] = v end
	for _, k in ipairs(ZERO_FIELDS) do
		if out[k] == nil then out[k] = 0 end
	end
	if out.ele_lv == nil then out.ele_lv = 1 end
	out.boss = out.boss == true
	out.arrow = out.arrow == true
	out.rhw = hand(u.rhw)
	if u.lhw then out.lhw = hand(u.lhw) end
	return out
end

return M
