--!nonstrict
-- The pure Luau aggregator of the status calculation (BR-RULESET-065..069, 071, 075, 112): given a unit snapshot and an active status set
-- it builds the ordered contribution list of one attribute from the status definitions (scripts/status/defs) and folds it with the
-- attribute's stage list (scripts/status/attrs). No state, no RNG, no engine access beyond the injected ctx:
--
--   ctx.idiv(a, b)  -> a / b truncated toward zero (C++ int semantics; the only division the status code uses)
--
--   calc(ctx, attr, base, unit, active, cfg) -> integer
--     attr    "str" "agi" ... "speed" (see scripts/status/attrs)
--     base    the value the legacy function receives (e.g. b_status->agi)
--     unit    { type = "pc" | "mob" | "hom" ..., level, weapon (status.weapon), lr_flag (bool), wlv_r / wlv_l (weapon levels of the
--               equipped weapons, 0 = none), option (status_change::option bits), class_ (MAPID), sex, permanent_speed,
--               bonus_speed_rate, bonus_speed_add_rate, skills = { NAME = lv } (pc_checkskill), skilltimer / skill_id / skill_lv
--               (an active cast), map = "normal" | "gvg" | "bg" }
--     active  { NAME = { val1 = n, val2 = n, val3 = n, val4 = n } } (legacy SC_ suffix as key), raw values
--     cfg     battle.conf values the functions read { gvg_flee_penalty, bg_flee_penalty, rental_mount_speed_boost, ... }
local define = import("scripts/status/define")
local attrs = import("scripts/status/attrs")
local ops = import("scripts/status/ops")
local K = import("scripts/status/consts")

local M = {}
M.cover = nil

-- ---- the environment handed to when / v functions ------------------------------------------------------------------
function M.env(ctx, unit, active, cfg)
	local sc = {}
	for name, raw in pairs(active or {}) do sc[name] = define.instance(name, raw) end
	local e = { ctx = ctx, idiv = ctx.idiv, mul_div = ctx.mul_div, sc = sc, unit = unit, cfg = cfg or {}, K = K }
	local skills = unit.skills or {}
	-- pc_checkskill(sd, id): 0 for non-players, and for skills that are not in the PRE skill database
	function e.skill(name)
		if unit.type ~= "pc" or not K.SKILL_IN_PRE_DB[name] then return 0 end
		return skills[name] or 0
	end
	return e
end

-- ---- emission: the ordered list of the active contributions of one attribute --------------------------------------
function M.emit(attr, e)
	local idx = define.owner_entries(attr)
	local cand, seen = {}, {}
	for name in pairs(e.sc) do
		local list = idx[name]
		if list then
			for _, ent in ipairs(list) do
				if not seen[ent] then
					seen[ent] = true
					cand[#cand + 1] = ent
				end
			end
		end
	end
	for _, ent in ipairs(define.rule_entries(attr)) do cand[#cand + 1] = ent end
	table.sort(cand, function(a, b)
		if a.at ~= b.at then return a.at < b.at end
		return a.ord < b.ord
	end)
	local out = {}
	for _, ent in ipairs(cand) do
		local s = ent.owner and e.sc[ent.owner] or nil
		if ent.when == nil or ent.when(s, e) then
			local cv = M.cover -- test support: counts how often every entry fires (golden coverage report)
			if cv then cv[ent] = (cv[ent] or 0) + 1 end
			local c = { src = ent.owner, at = ent.at, op = ent.op, group = ent.group, stage = ent.stage }
			if ent.v ~= nil then
				if type(ent.v) == "function" then c.v = ent.v(s, e) else c.v = ent.v end
			end
			if ent.v2 ~= nil then
				if type(ent.v2) == "function" then c.v2 = ent.v2(s, e) else c.v2 = ent.v2 end
			end
			out[#out + 1] = c
		end
	end
	return out
end

-- ---- the operations (pure functions of the accumulator and 1..2 operands) ---------------------------------------
local OPS = {
	add = function(e, acc, v) return acc + v end,
	mul_pct = function(e, acc, v) return acc + e.idiv(acc * v, 100) end,
	mul_frac = function(e, acc, v, v2) return acc + e.idiv(acc * v, v2) end,
	scale = function(e, acc, v, v2) return e.idiv(acc * v, v2) end,
	div = function(e, acc, v) return e.idiv(acc, v) end,
	set = function(e, acc, v) return v end,
	max = function(e, acc, v) if v > acc then return v end return acc end,
	pct_over = function(e, acc, v, v2) return acc + e.idiv((acc - v) * v2, 100) end,
	sub_pct2 = function(e, acc, v, v2) return acc - (e.idiv(acc * v, 100) + e.idiv(acc * v2, 100)) end,
}
M.OPS = OPS

local function clamp(cfg, x)
	if cfg.clamp then return ops.cap(x, cfg.clamp[1], cfg.clamp[2]) end
	return x
end

-- Folds a contribution list with the standard stage list of `attr`. Returns the attribute's value.
function M.fold(attr, base, contribs, e)
	local cfg = attrs[attr]
	local acc = base
	local pre, over, ordered = {}, {}, {}
	for _, c in ipairs(contribs) do
		if c.stage == "pre" then pre[#pre + 1] = c
		elseif c.stage == "override" or define.TERMINAL[c.op] then over[#over + 1] = c
		elseif OPS[c.op] then ordered[#ordered + 1] = c end -- group ops belong to the custom evaluators
	end
	for _, c in ipairs(pre) do acc = OPS[c.op](e, acc, c.v, c.v2) end
	for _, c in ipairs(over) do
		local op = c.op
		if op == "final" then return ops.narrow(c.v, cfg.ret[1], cfg.ret[2])
		elseif op == "final_div" then return ops.narrow(e.idiv(acc, c.v), cfg.ret[1], cfg.ret[2])
		elseif op == "final_clamp" then return clamp(cfg, c.v)
		elseif op == "stop" then return clamp(cfg, acc)
		else acc = OPS[op](e, acc, c.v, c.v2) end
	end
	for _, c in ipairs(ordered) do acc = OPS[c.op](e, acc, c.v, c.v2) end
	return clamp(cfg, acc)
end

-- Group values of a contribution list: gset assigns, gmax raises, in legacy order, starting from 0.
function M.groups(contribs)
	local g = {}
	for _, c in ipairs(contribs) do
		if c.op == "gset" then g[c.group] = c.v
		elseif c.op == "gmax" then
			local cur = g[c.group] or 0
			if c.v > cur then cur = c.v end
			g[c.group] = cur
		end
	end
	return g
end

-- ---- entry points -------------------------------------------------------------------------------------------------
function M.calc_env(e, attr, base)
	local cfg = attrs[attr]
	if cfg == nil then error("status.calc: unknown attribute " .. tostring(attr), 2) end
	if cfg.custom then return import(cfg.custom).calc(e, base) end
	return M.fold(attr, base, M.emit(attr, e), e)
end

function M.calc(ctx, attr, base, unit, active, cfg)
	return M.calc_env(M.env(ctx, unit, active, cfg), attr, base)
end

return M
