--!nonstrict
-- The recalculation of the battle status from the base status and the active statuses: status_calc_bl_main (status.cpp:5858-6460, PRE
-- branch) for the stats this package covers. Together with the attribute calculators it is the Luau counterpart of the engine
-- attrs graph: a changed attribute marks the attributes derived from it dirty (STR -> BATK, AGI -> FLEE, VIT -> DEF2 / MDEF2, DEX -> BATK /
-- HIT, LUK -> BATK / CRI / FLEE2), and only the dirty ones are recomputed (BR-RULESET-077); everything else keeps the value it had.
--
--   recalc(ctx, unit, base, battle, active, cfg, flags) -> new battle status
--     base, battle: { str agi vit int dex luk batk rhw_atk rhw_atk2 lhw_atk lhw_atk2 rhw_ele lhw_ele hit flee flee2 cri def def2 mdef mdef2
--                     speed amotion adelay aspd_rate max_hp max_sp def_ele ele_lv matk_min matk_max }  (integers)
--     flags: set { STR = true, ... } of the status_calc_bl flags to recalculate (names of enum e_scb_flag without SCB_)
-- Every field is stored with the width of its status_data member (int16 for the stats, so a calculated 65535 is stored as -1; signed char
-- for def / mdef; uint16 for atk, speed, matk), the way the C++ assignment narrows it.
-- Not modelled: ASPD / DSPD / MAXHP / MAXSP of players (job tables, BR-MAP-004 / 019 / 020 of the map-rules library) and homunculus,
-- mercenary and elemental branches. For players the caller must not ask for flags that propagate to them (AGI, DEX -> ASPD; VIT -> MAXHP;
-- INT -> MAXSP): they are rejected.
local aggregate = import("scripts/status/aggregate")
local ops = import("scripts/status/ops")
local K = import("scripts/status/consts")
local maxhp = import("scripts/status/maxhp")

local M = {}

local function i16(x) return ops.narrow(x, 16, true) end
local function u16(x) return ops.narrow(x, 16, false) end
local function i8(x) return ops.narrow(x, 8, true) end
local function u8(x) return ops.narrow(x, 8, false) end

local RANGED_BASE = { [K.W_BOW] = true, [K.W_MUSICAL] = true, [K.W_WHIP] = true, [K.W_REVOLVER] = true, [K.W_RIFLE] = true,
	[K.W_GATLING] = true, [K.W_SHOTGUN] = true, [K.W_GRENADE] = true }

-- status_base_atk (status.cpp:2433-2490), PRE: STR + (STR/10)^2 + DEX/5 + LUK/5 (STR and DEX swap for ranged weapons of players)
local function base_atk(e, st)
	local u = e.unit
	local mask = u.type == "pc" and K.BL_PC or u.type == "mob" and K.BL_MOB or 0
	if ops.band(e.cfg.enable_baseatk, mask) == 0 then return 0 end -- 2437
	local str, dex = st.str, st.dex
	if u.type == "pc" and RANGED_BASE[u.weapon] then str, dex = st.dex, st.str end -- 2444-2458
	local dstr = e.idiv(str, 10)
	str = str + dstr * dstr
	if u.type == "hom" then return ops.cap(str, 0, K.USHRT_MAX) end -- (homunculus: no DEX / LUK terms)
	str = str + e.idiv(dex, 5) + e.idiv(st.luk, 5)
	return ops.cap(str, 0, K.USHRT_MAX)
end

local function copy(t)
	local o = {}
	for k, v in pairs(t) do o[k] = v end
	return o
end

local PC_FORBIDDEN = { AGI = true, DEX = true, VIT = true, INT = true, ASPD = true, DSPD = true, MAXHP = true, MAXSP = true }

function M.recalc(ctx, unit, base, battle, active, cfg, flagset)
	local e = aggregate.env(ctx, unit, active, cfg)
	local pc = unit.type == "pc"
	local b = base
	local st = copy(battle)
	local flag = copy(flagset)
	if pc then
		for name in pairs(flag) do
			if PC_FORBIDDEN[name] then error("chain: flag " .. name .. " of a player needs the job tables (not modelled)", 2) end
		end
	end
	local function calc(attr, v, env) return aggregate.calc_env(env or e, attr, v) end

	-- ---- the stats (5885-5990), each marks the stats derived from it
	if flag.STR then st.str = i16(calc("str", b.str)); flag.BATK = true end
	if flag.AGI then st.agi = i16(calc("agi", b.agi)); flag.FLEE = true; if pc then flag.ASPD = true; flag.DSPD = true end end
	if flag.VIT then st.vit = i16(calc("vit", b.vit)); flag.DEF2 = true; flag.MDEF2 = true; if pc then flag.MAXHP = true end end
	if flag.INT then st.int = i16(calc("int", b.int)); flag.MATK = true; flag.MDEF2 = true; if pc then flag.MAXSP = true end end
	if flag.DEX then st.dex = i16(calc("dex", b.dex)); flag.BATK = true; flag.HIT = true; if pc then flag.ASPD = true end end
	if flag.LUK then st.luk = i16(calc("luk", b.luk)); flag.BATK = true; flag.CRI = true; flag.FLEE2 = true end

	-- ---- BATK (5994-6003): the base attack follows the stats, the base batk keeps its own delta
	if flag.BATK and b.batk ~= 0 then
		st.batk = base_atk(e, st)
		st.batk = st.batk + (b.batk - base_atk(e, b))
		st.batk = ops.narrow(calc("batk", st.batk), 32, true)
	end

	-- ---- WATK (5999-6015): right hand (lr_flag clear), left hand (lr_flag set); non-players also calculate atk2
	if flag.WATK then
		local right = e
		st.rhw_atk = u16(calc("watk", b.rhw_atk, right))
		if not pc then st.rhw_atk2 = u16(calc("watk", b.rhw_atk2, right)) end
		if pc and unit.bonus_weapon_atk_rate and unit.bonus_weapon_atk_rate ~= 0 then -- 6004
			st.rhw_atk = u16(st.rhw_atk + e.idiv(st.rhw_atk * unit.bonus_weapon_atk_rate, 100))
		end
		if b.lhw_atk ~= 0 then
			if pc then
				local lu = copy(unit); lu.lr_flag = true
				local left = aggregate.env(ctx, lu, active, cfg)
				st.lhw_atk = u16(calc("watk", b.lhw_atk, left))
			else
				st.lhw_atk = u16(calc("watk", b.lhw_atk, e))
				st.lhw_atk2 = u16(calc("watk", b.lhw_atk2, e))
			end
		end
	end

	-- ---- HIT, FLEE (6026-6051): the base value follows DEX / AGI
	if flag.HIT then
		if st.dex == b.dex then st.hit = i16(calc("hit", b.hit)) else st.hit = i16(calc("hit", b.hit + (st.dex - b.dex))) end
	end
	if flag.FLEE then
		if st.agi == b.agi then st.flee = i16(calc("flee", b.flee)) else st.flee = i16(calc("flee", b.flee + (st.agi - b.agi))) end
	end

	-- ---- DEF, DEF2, MDEF, MDEF2 (6054-6100)
	if flag.DEF then st.def = i8(calc("def", b.def)) end
	if flag.DEF2 then
		if st.vit == b.vit then st.def2 = i16(calc("def2", b.def2)) else st.def2 = i16(calc("def2", b.def2 + (st.vit - b.vit))) end
	end
	if flag.MDEF then st.mdef = i8(calc("mdef", b.mdef)) end
	if flag.MDEF2 then
		if st.int == b.int and st.vit == b.vit then st.mdef2 = i16(calc("mdef2", b.mdef2))
		else st.mdef2 = i16(calc("mdef2", b.mdef2 + (st.int - b.int) + e.idiv(st.vit - b.vit, 2))) end
	end

	-- ---- SPEED (6102-6112, 6138: players are floored at max_walk_speed unless their speed is permanent)
	if flag.SPEED then
		st.speed = u16(calc("speed", b.speed))
		if pc and not unit.permanent_speed and st.speed < e.cfg.max_walk_speed then st.speed = e.cfg.max_walk_speed end
	end

	-- ---- CRI, FLEE2 (6143-6168): the base value follows LUK; the base must be non-zero
	if flag.CRI and b.cri ~= 0 then
		if st.luk == b.luk then st.cri = i16(calc("cri", b.cri))
		else st.cri = i16(calc("cri", b.cri + e.idiv((st.luk - b.luk) * 10, 3))) end
		if pc and unit.weapon == K.W_KATAR then st.cri = i16(st.cri * 2) end -- 6163
	end
	if flag.FLEE2 and b.flee2 ~= 0 then
		if st.luk == b.luk then st.flee2 = i16(calc("flee2", b.flee2)) else st.flee2 = i16(calc("flee2", b.flee2 + (st.luk - b.luk))) end
	end

	-- ---- element (6170-6180)
	if flag.ATK_ELE then
		st.rhw_ele = u8(calc("atk_element", b.rhw_ele))
		st.lhw_ele = u8(calc("atk_element", b.lhw_ele))
	end
	if flag.DEF_ELE then
		st.def_ele = u8(calc("element", b.def_ele))
		st.ele_lv = u8(calc("element_lv", b.ele_lv))
	end

	-- ---- MAXHP, MAXSP (6220-6250), non-players
	if flag.MAXHP and not pc then st.max_hp = maxhp.calc(e, "hp", b.max_hp) end
	if flag.MAXSP and not pc then st.max_sp = maxhp.calc(e, "sp", b.max_sp) end

	-- ---- MATK (6252-6296, PRE): INT + (INT/7)^2 .. INT + (INT/5)^2, Recognized Spell, the flat bonuses, Magic Power
	if flag.MATK then
		local i = st.int
		local mn = i + e.idiv(i, 7) * e.idiv(i, 7)
		local mx = i + e.idiv(i, 5) * e.idiv(i, 5)
		mn, mx = u16(mn), u16(mx)
		if pc then mn, mx = mn + (unit.bonus_ematk or 0), mx + (unit.bonus_ematk or 0) end
		if e.sc.RECOGNIZEDSPELL ~= nil then mn = math.max(mn, mx) end
		mn = calc("matk", mn)
		mx = calc("matk", mx)
		local mp = e.sc.MAGICPOWER
		if mp ~= nil and mp.active ~= 0 then
			mn = mn + e.idiv(mn * mp.matk_pct, 100)
			mx = mx + e.idiv(mx * mp.matk_pct, 100)
		end
		st.matk_min = ops.cap(mn, 0, K.USHRT_MAX)
		st.matk_max = ops.cap(mx, 0, K.USHRT_MAX)
	end

	-- ---- ASPD of non-players (6439-6452): amotion from the base amotion and aspd_rate, adelay never lengthened by a slow status
	if flag.ASPD and not pc then
		st.aspd_rate = i16(calc("aspd_rate", b.aspd_rate))
		local amotion = e.idiv(b.amotion * st.aspd_rate, 1000)
		amotion = calc("fix_aspd", amotion)
		st.amotion = ops.cap(amotion, 100, K.MIN_ASPD) -- MAX_ASPD_NOPC / AMOTION_DIVIDER_NOPC .. MIN_ASPD / AMOTION_DIVIDER_NOPC
		local temp = e.idiv(b.adelay * math.min(st.aspd_rate, 1000), 1000)
		st.adelay = ops.cap(temp, st.amotion, K.MIN_ASPD)
	end
	return st
end

return M
