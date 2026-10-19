--!nonstrict
-- Replays the golden STATUS calculation cases (conformance/golden_gen/gen_status_cases.py, recorded from the real legacy
-- status_calc_* functions of a PRE build) through the Luau aggregator and compares the result of every case with the recorded one.
-- A case: one function, one unit (pc / mob + the unit fields that function reads), one active status set {NAME = {val1..val4}}, a base
-- value, battle.conf values; the legacy output is one integer (or, for fn = state, the twelve sc.cant flags).
local status = import("scripts/status/index")
local states = import("scripts/status/states")
local aggregate = import("scripts/status/aggregate")
local fake_ctx = import("tests/support/fake_ctx")

local M = {}

local MAPS = { [0] = "normal", [1] = "gvg", [2] = "bg" }
local CANT = { "move", "cast", "chat", "attack", "warp", "pickup", "drop", "equip", "unequip", "consume", "deathpenalty", "interact" }
M.CANT = CANT

function M.load() return import("build/golden_status_pre") end

-- The legacy status code computes in C++ int32: a product beyond +-2^31 is undefined behaviour there, so such a case would not be a
-- reference. The ctx used here refuses any int32 overflow (a generator slip shows up as an error, never as a quiet mismatch).
local base_ctx = fake_ctx.new({})
local ctx = {}
for k, v in pairs(base_ctx) do ctx[k] = v end
function ctx.idiv(a, b)
	if a > 2147483647 or a < -2147483648 then error({ code = "INT32_OVERFLOW", msg = "dividend " .. tostring(a) }) end
	return base_ctx.idiv(a, b)
end

-- unit snapshot of a case (the shape documented in scripts/status/aggregate.luau)
function M.unit_of(c, K)
	local u = c.unit or {}
	local pc = c.ut == "pc"
	return {
		type = c.ut,
		id = pc and 150000 or 200000, -- the ids the golden hook gives its units (compared with val4 / val2 of some statuses)
		level = u.level or 1,
		weapon = u.w or 0,
		lr_flag = (u.lr or 0) ~= 0,
		wlv_r = u.rwlv or 0,
		wlv_l = u.lwlv or 0,
		option = u.opt or 0,
		class_ = (u.job or 0) == 1 and K.MAPID_ASSASSIN or K.MAPID_SWORDMAN,
		sex = u.sex or 0,
		permanent_speed = (u.perm_speed or 0) ~= 0,
		bonus_speed_rate = u.bonus_speed_rate or 0,
		bonus_speed_add_rate = u.bonus_speed_add_rate or 0,
		bonus_hp = u.bonus_hp or 0,
		bonus_sp = u.bonus_sp or 0,
		skills = u.sk or {},
		skilltimer = (u.skilltimer or 0) ~= 0,
		skill_id = u.ud_skill and K[u.ud_skill] or 0,
		skill_lv = u.ud_lv or 0,
		map = MAPS[u.map or 0],
	}
end

function M.active_of(c)
	local active = {}
	for name, v in pairs(c.sc) do active[name] = { val1 = v[1], val2 = v[2], val3 = v[3], val4 = v[4] } end
	return active
end

-- Runs one case. Returns the Luau result (number, or table of flags for state).
function M.compute(c, K, cfg_defaults)
	local K_all = import("scripts/status/consts")
	local cfg = {}
	for k, v in pairs(cfg_defaults or {}) do cfg[k] = v end -- battle.conf defaults of the legacy build ...
	for k, v in pairs(c.cfg or {}) do cfg[k] = v end -- ... overridden by the case
	local unit, active = M.unit_of(c, K), M.active_of(c)
	local fn = c.fn
	if fn == "maxhp" then return status.maxhp(ctx, c.base, unit, active, cfg) end
	if fn == "maxsp" then return status.maxsp(ctx, c.base, unit, active, cfg) end
	if fn == "hp_fix" or fn == "hp_rate" or fn == "sp_fix" or fn == "sp_rate" then
		return status.bonus(ctx, fn:sub(1, 2), fn:sub(4), unit, active, cfg)
	end
	if fn == "script" then
		local scripted = import("scripts/status/scripted")
		local names = {}
		for name in pairs(active) do names[#names + 1] = name end
		return scripted.run(ctx, names, active, K_all)
	end
	if fn == "chain" then
		local chain = import("scripts/status/chain")
		local base, battle = {}, {}
		for k, v in pairs(c.b) do base[k] = v end
		for k, v in pairs(c.b) do battle[k] = c.s[k] ~= nil and c.s[k] or v end
		local flags = {}
		for _, f in ipairs(c.flags) do flags[f] = true end
		unit.bonus_weapon_atk_rate = (c.unit.bonus_weapon_atk_rate or 0)
		local out = chain.recalc(ctx, unit, base, battle, active, cfg, flags)
		return out
	end
	if fn == "state" then
		local cant = {}
		for _, k in ipairs(CANT) do cant[k] = (c.cant[k] or 0) ~= 0 end
		local out = states.calc(aggregate.env(ctx, unit, active, cfg), c.changed, c.start ~= 0, cant)
		local r = {}
		for _, k in ipairs(CANT) do r[k] = out[k] and 1 or 0 end
		return r
	end
	return status.calc(ctx, fn, c.base, unit, active, cfg)
end

local CHAIN_PC = { amotion = true, adelay = true, aspd_rate = true, max_hp = true, max_sp = true } -- not modelled for players (job tables)
local CHAIN_FIELDS = { "str", "agi", "vit", "int", "dex", "luk", "batk", "rhw_atk", "rhw_atk2", "lhw_atk", "lhw_atk2", "rhw_ele", "lhw_ele", "hit",
	"flee", "flee2", "cri", "def", "def2", "mdef", "mdef2", "speed", "amotion", "adelay", "aspd_rate", "max_hp", "max_sp", "def_ele", "ele_lv",
	"matk_min", "matk_max" }
M.CHAIN_FIELDS = CHAIN_FIELDS

local function same(a, b, c)
	if c and c.fn == "chain" then
		for _, k in ipairs(CHAIN_FIELDS) do
			if not (c.ut == "pc" and CHAIN_PC[k]) and a[k] ~= b[k] then return false end
		end
		return true
	end
	if c and c.fn == "script" then
		if #a ~= #b then return false end
		for i, rec in ipairs(a) do
			if #rec ~= #b[i] then return false end
			for j, v in ipairs(rec) do
				if v ~= b[i][j] then return false end
			end
		end
		return true
	end
	if type(b) == "table" then
		for _, k in ipairs(CANT) do
			if a[k] ~= b[k] then return false end
		end
		return true
	end
	return a == b
end

local function show(v)
	if type(v) == "table" and type(v[1]) == "table" then
		local t = {}
		for _, rec in ipairs(v) do t[#t + 1] = table.concat(rec, ",") end
		return "[" .. table.concat(t, ";") .. "]"
	end
	if type(v) == "table" and v.str ~= nil then
		local t = {}
		for _, k in ipairs(CHAIN_FIELDS) do t[#t + 1] = k .. "=" .. tostring(v[k]) end
		return "{" .. table.concat(t, ",") .. "}"
	end
	if type(v) == "table" then
		local t = {}
		for _, k in ipairs(CANT) do if v[k] == 1 then t[#t + 1] = k end end
		return "{" .. table.concat(t, ",") .. "}"
	end
	return tostring(v)
end
M.show = show

-- Runs every case: report { total, matched, by_fn = {fn -> {total, matched}}, by_status = {NAME -> {total, matched, single_total, single_matched}},
-- by_cat, mismatches = { {id, fn, got, want} ... } (first 50) }
function M.run_all(g, filter)
	local K = g.consts.K
	local rep = { total = 0, matched = 0, by_fn = {}, by_status = {}, by_cat = {}, mismatches = {}, errors = 0 }
	local function tally(t, key, ok, single)
		local x = t[key]
		if not x then x = { total = 0, matched = 0, single_total = 0, single_matched = 0 }; t[key] = x end
		x.total = x.total + 1
		if ok then x.matched = x.matched + 1 end
		if single then
			x.single_total = x.single_total + 1
			if ok then x.single_matched = x.single_matched + 1 end
		end
	end
	for _, c in ipairs(g.cases) do
		if not filter or filter(c) then
			local ok, got = pcall(M.compute, c, K, g.consts.cfg)
			local match = ok and same(got, c.out, c)
			if not ok then rep.errors = rep.errors + 1 end
			rep.total = rep.total + 1
			if match then rep.matched = rep.matched + 1 end
			tally(rep.by_fn, c.fn, match)
			tally(rep.by_cat, c.cat, match)
			local n, only = 0, nil
			for name in pairs(c.sc) do n = n + 1; only = name end
			for name in pairs(c.sc) do tally(rep.by_status, name, match, n == 1) end
			if c.fn == "state" then tally(rep.by_status, c.changed, match, false) end
			if not match and #rep.mismatches < 400 then
				rep.mismatches[#rep.mismatches + 1] = { id = c.id, fn = c.fn, cat = c.cat, got = ok and show(got) or ("ERROR " .. tostring(type(got) == "table" and (got.msg or got.code) or got)), want = show(c.out), case = c }
			end
		end
	end
	return rep
end

return M
