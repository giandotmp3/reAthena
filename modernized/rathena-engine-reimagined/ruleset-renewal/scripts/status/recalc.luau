--!nonstrict
-- Partial recalculation (BR-RULESET-077): the calc flags of a status select which attributes are recomputed; recomputing one stat sets the flags of the
-- stats derived from it (status_calc_bl_main, status.cpp:5880-6030, the RENEWAL branches); the player recalculation refuses to nest more than 10 deep
-- (status.cpp:3781) and a player with delayed damage holds the recalculation until the damage is applied (status.cpp:6548-6556).
local M = {}

-- the SCB_* flag of the record (calc_flags) that recomputes each attribute of scripts/status/attrs_re
M.FLAG_OF = {
	str = "STR", agi = "AGI", vit = "VIT", int = "INT", dex = "DEX", luk = "LUK", pow = "POW", sta = "STA", wis = "WIS", spl = "SPL", con = "CON", crt = "CRT",
	batk = "BATK", watk = "WATK", matk_flat = "MATK", cri = "CRI", hit = "HIT", flee = "FLEE", flee2 = "FLEE2", def = "DEF", def2 = "DEF2", mdef = "MDEF",
	mdef2 = "MDEF2", speed = "SPEED", speed_rate = "SPEED", aspd_pct = "ASPD", aspd_fixed = "ASPD", aspd_rate = "ASPD", fix_aspd = "ASPD", patk = "PATK",
	smatk = "SMATK", res = "RES", mres = "MRES", hplus = "HPLUS", crate = "CRATE", hp_fix = "MAXHP", hp_rate = "MAXHP", maxhp = "MAXHP", sp_fix = "MAXSP",
	sp_rate = "MAXSP", maxsp = "MAXSP", ap_fix = "MAXAP", ap_rate = "MAXAP", maxap = "MAXAP", def_ele = "DEF_ELE", ele_lv = "DEF_ELE", atk_ele = "ATK_ELE",
	mode = "MODE",
}

-- flag -> flags it sets for a unit kind ("pc" | "hom" | "mob"); each entry is { flag, kinds } (kinds nil = every kind)
local PC_HOM_MER_ELEM = { pc = true, hom = true, mer = true, elem = true }
local PC_HOM = { pc = true, hom = true }
local HOM = { hom = true }
M.CASCADE = {
	STR = { { "BATK" }, { "WATK", HOM } },                                              -- 5879-5882
	AGI = { { "FLEE" }, { "DEF2" }, { "ASPD", PC_HOM }, { "DSPD", PC_HOM } },           -- 5885-5897 (DEF2 is the RENEWAL line)
	VIT = { { "DEF2" }, { "MDEF2" }, { "MAXHP", PC_HOM_MER_ELEM }, { "DEF", HOM } },    -- 5899-5908
	INT = { { "MATK" }, { "MDEF2" }, { "MAXSP", PC_HOM_MER_ELEM }, { "MDEF", HOM } },   -- 5910-5919
	DEX = { { "BATK" }, { "HIT" }, { "MATK" }, { "MDEF2" }, { "ASPD", PC_HOM }, { "WATK", HOM } }, -- 5921-5934 (MATK, MDEF2: RENEWAL)
	LUK = { { "BATK" }, { "CRI" }, { "FLEE2" }, { "MATK" }, { "HIT" }, { "FLEE" } },    -- 5936-5946
	POW = { { "BATK" }, { "PATK" } },                                                   -- 5951-5954
	STA = { { "RES" } },
	WIS = { { "MRES" } },
	SPL = { { "MATK" }, { "SMATK" } },
	CON = { { "HIT" }, { "FLEE" }, { "PATK" }, { "SMATK" } },
	CRT = { { "HPLUS" }, { "CRATE" } },                                                 -- 5956-5986
}

-- the flags a recalculation of `flags` (a set) really recomputes for a unit kind
function M.closure(flags, kind)
	local out = {}
	for f in pairs(flags) do out[f] = true end
	for f in pairs(flags) do
		for _, rule in ipairs(M.CASCADE[f] or {}) do
			if rule[2] == nil or rule[2][kind] then out[rule[1]] = true end
		end
	end
	return out
end

-- flags of a status record as a set
function M.flag_set(list)
	local s = {}
	for _, f in ipairs(list or {}) do s[f] = true end
	return s
end

-- the attributes of the status layer that a recalculation with these flags recomputes
function M.attributes(flags, kind, attribute_names)
	local closure = M.closure(flags, kind)
	local out = {}
	for _, a in ipairs(attribute_names) do
		if closure[M.FLAG_OF[a]] then out[#out + 1] = a end
	end
	return out
end

-- Attributes a status contributes to whose recomputation its own record would not trigger (the legacy data has a few such gaps).
function M.uncovered(def, kind)
	local rec = def.record
	local closure = M.closure(M.flag_set(rec and rec.calc_flags), kind)
	local out = {}
	for attr in pairs(def.contributions) do
		if not closure[M.FLAG_OF[attr]] then out[#out + 1] = attr end
	end
	table.sort(out)
	return out
end

-- Re-entrance guard of status_calc_pc_sub (status.cpp:3777-3786, 5012): a static counter that every call raises (`if (++calculating > 10) return -1`),
-- that a call which runs to the end resets to ZERO (not "minus one"), and that an outer call tests after each item script (`if (!calculating) return 1`):
-- a nested recalculation that completes therefore makes the outer one abort. A call that is refused leaves the counter raised.
function M.new_guard()
	local g = { calculating = 0 }
	function g.enter() -- false: too many recursive calls (returns -1 in the legacy code)
		g.calculating += 1
		return g.calculating <= 10
	end
	function g.finish() g.calculating = 0 end
	function g.outer_must_abort() return g.calculating == 0 end
	return g
end

-- status_calc_bl_ (6548): a player with delayed damage holds the recalculation unless it is forced
function M.should_hold(unit, force)
	return unit.kind == "pc" and (unit.delayed_damage or 0) ~= 0 and not force
end

return M
