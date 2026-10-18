--!nonstrict
-- Unit snapshot normaliser (BR-RULESET-117 immutable snapshots): fills the defaults the renewal weapon pipeline relies on.
-- Returns a new table; bonus maps and status tables are shared with the input, they are only read.
local M = {}

local ZERO_FIELDS = { "level", "str", "agi", "vit", "int", "dex", "luk", "batk", "hit", "cri", "flee", "flee2", "def", "def2",
	"size", "race", "element", "targeted", "wtype1", "wtype2", "patk", "crate", "eatk", "res", "mres" }

local function hand(h)
	local out = { atk = 0, atk2 = 0, ele = 0, wlv = 0, range = 1 }
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
	if out.damagetaken == nil then out.damagetaken = 100 end
	out.boss = out.boss == true
	out.arrow = out.arrow == true
	out.shield = out.shield == true
	out.ammo = out.ammo or 0
	out.rhw = hand(u.rhw)
	if u.lhw then out.lhw = hand(u.lhw) end
	-- status.cpp:4368: watk / watk2 are the uint16 sums of weapon ATK and refine ATK of each hand
	out.watk = (out.rhw.atk + out.rhw.atk2) % 65536
	out.watk2 = ((out.lhw and out.lhw.atk or 0) + (out.lhw and out.lhw.atk2 or 0)) % 65536
	return out
end

return M
