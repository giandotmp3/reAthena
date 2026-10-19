--!nonstrict
-- The RENEWAL attribute definitions: for every attribute the legacy code computes with a status_calc_* function, the ORDERED STAGE LIST that
-- folds its contributions (the ruleset-supplied aggregation of engine-core's AttributeSystem, attrs.hpp: value = left fold of the stage list
-- over the contribution list, starting from 0; the engine never interprets a tag).
--
-- A contribution is { source = <status name>, tag = "<stage>|<at>|<op>|<arg>", value = <int> }; the unit's facts are contributions with the
-- stage "fact" (tag "fact|0|<name>|"), the base value one with the stage "base". A stage function is pure: fn(acc, contributions) -> acc.
-- Ops belong to the stage that folds them (`ops` in each definition); a status definition (scripts/status/defs) uses the ops by name.
--
-- Every stage is a hand-written transcription of status.cpp (the line of the legacy statement is in the comment). The `#ifdef RENEWAL` and
-- `#ifdef RENEWAL_ASPD` branches are the ones compiled in; the `#ifndef` branches do not exist in this ruleset.
local num = import("scripts/status/num")
local consts = import("scripts/status/consts")

local M = {}

-- ---- reading the contribution list -------------------------------------------------------------------------------------------------
local parsed = {}
local function parse(tag)
	local p = parsed[tag]
	if p == nil then
		local stage, at, op, arg = tag:match("^([^|]*)|([^|]*)|([^|]*)|(.*)$")
		p = { stage = stage, at = tonumber(at), op = op, arg = arg }
		parsed[tag] = p
	end
	return p
end

-- the contributions of one stage, in ascending `at` (ties keep the insertion order)
local function collect(contribs, stage)
	local out, n = nil, 0
	for i, c in ipairs(contribs) do
		local p = parse(c.tag)
		if p.stage == stage then
			n += 1
			out = out or {}
			out[n] = { at = p.at, op = p.op, arg = p.arg, value = c.value, source = c.source, idx = i }
		end
	end
	if out and n > 1 then
		table.sort(out, function(a, b) if a.at ~= b.at then return a.at < b.at end return a.idx < b.idx end)
	end
	return out or {}
end

local function facts_of(contribs)
	local F = {}
	for _, c in ipairs(contribs) do
		local p = parse(c.tag)
		if p.stage == "fact" then F[p.op] = c.value end
	end
	return F
end

local function has_op(contribs, op)
	for _, c in ipairs(contribs) do
		if parse(c.tag).op == op then return true end
	end
	return false
end

local function find_op(contribs, op)
	for _, c in ipairs(contribs) do
		if parse(c.tag).op == op then return c end
	end
	return nil
end

-- the value of the contribution with this op (nil when absent)
local function op_value(contribs, op)
	local c = find_op(contribs, op)
	return c and c.value or nil
end

local function base_stage(acc, contribs)
	for _, c in ipairs(contribs) do
		if parse(c.tag).stage == "base" then acc += c.value end
	end
	return acc
end

M.helpers = { collect = collect, facts_of = facts_of, has_op = has_op, op_value = op_value, parse = parse }

-- ---- the generic chain: `x += ...; x -= x * pct / 100; ...` in code order, then cap_value() -------------------------------------------
-- op -> function(acc, value, arg) -> acc [, stop]; stop = "ret" (the statement is `return <value>`, no clamp) or "end" (clamp, then done)
local OPS = {
	add = function(a, v) return num.i32(a + v) end,                                  -- x += v
	sub = function(a, v) return num.i32(a - v) end,                                  -- x -= v
	pct_add = function(a, v) return num.i32(a + num.mul_div(a, v, 100)) end,         -- x += x * v / 100
	pct_sub = function(a, v) return num.i32(a - num.mul_div(a, v, 100)) end,         -- x -= x * v / 100
	scale = function(a, v) return num.mul_div(a, v, 100) end,                        -- x = x * v / 100
	div = function(a, v) return num.tdiv(a, v) end,                                  -- x /= v
	div_sub = function(a, v) return num.i32(a - num.tdiv(a, v)) end,                 -- x -= x / v
	pct_add_off = function(a, v, arg) return num.i32(a + num.tdiv(num.i32((a - arg) * v), 100)) end, -- x += (x - arg) * v / 100
	pct_sub2 = function(a, v, arg) return num.i32(a - num.i32(num.mul_div(a, v, 100) + num.mul_div(a, arg, 100))) end, -- x -= x*v/100 + x*arg/100
	set = function(_, v) return v end,                                               -- x = v
	bor = function(a, v) return num.bor(a, v) end,                                   -- x |= v
	band_not = function(a, v) return num.band(a, num.bnot(v)) end,                   -- x &= ~v
	set_masked = function(a, v) return num.bor(num.band(a, num.bnot(consts.MD_MASK)), v) end, -- x = (x & ~MD_MASK) | v
	ret = function(_, v) return v, "ret" end,                                        -- return v;
	div_ret = function(a, v) return num.tdiv(a, v), "ret" end,                       -- return x /= v;
	sub_end = function(a, v) return num.i32(a - v), "end" end,                       -- x -= v; return cap_value(x)
	set_end = function(_, v) return v, "end" end,                                    -- x = v; (then the normal cap)
}
M.GENERIC_OPS = OPS

local function stage_of_ops(conv)
	return function(acc, contribs)
		for _, c in ipairs(collect(contribs, "ops")) do
			local f = OPS[c.op]
			local arg = c.arg ~= "" and tonumber(c.arg) or nil
			local stop
			acc, stop = f(acc, c.value, arg)
			if stop == "ret" then return conv(acc) end -- an early return narrows to the function's return type, it is not capped
			if stop == "end" then break end
		end
		return acc
	end
end

-- the first statement in code order that stops the fold: "ret" (return <value>) or "end" (cap and finish); nil when none applies
local function first_stop(contribs)
	for _, c in ipairs(collect(contribs, "ops")) do
		if c.op == "ret" or c.op == "div_ret" or c.op == "sub_end" or c.op == "set_end" then return c.op == "div_ret" and "ret" or c.op end
	end
	return nil
end

local function stage_finish(lo, hi, conv)
	return function(acc, contribs)
		if first_stop(contribs) == "ret" then return acc end -- the early return already produced the final value
		if lo ~= nil then acc = num.cap(acc, lo, hi) end
		return conv(acc)
	end
end

local ident = function(v) return v end
local conv = { u16 = num.wrap_u16, i16 = num.wrap_i16, u8 = num.wrap_u8, i32 = ident }

local function generic_ops_map()
	local m = {}
	for k in pairs(OPS) do m[k] = "ops" end
	return m
end

-- a plain chain attribute
local function chain(name, spec)
	local c = conv[spec.conv]
	local stages = { { id = "base", fn = base_stage } }
	for _, st in ipairs(spec.pre or {}) do stages[#stages + 1] = st end
	stages[#stages + 1] = { id = "ops", fn = stage_of_ops(c) }
	stages[#stages + 1] = { id = "finish", fn = stage_finish(spec.lo, spec.hi, c) }
	M.attributes[name] = { name = name, stages = stages, ops = spec.ops or generic_ops_map(), facts = spec.facts or {},
		lo = spec.lo, hi = spec.hi, ret = spec.conv }
end

M.attributes = {}
local C = consts

-- ---- statistics (status.cpp:6773-7307): cap_value(x, 0, USHRT_MAX) -------------------------------------------------------------------
for _, a in ipairs({ "str", "agi", "vit", "int", "dex", "luk", "pow", "sta", "wis", "spl", "con", "crt" }) do
	chain(a, { lo = 0, hi = C.USHRT_MAX, conv = "u16" })
end

-- ---- batk (7316): int32 result, no cap ---------------------------------------------------------------------------------------------
chain("batk", { conv = "i32" })

-- ---- watk (7360): cap 0..USHRT_MAX -------------------------------------------------------------------------------------------------
chain("watk", { lo = 0, hi = C.USHRT_MAX, conv = "u16" })

-- ---- flat MATK of skills (status_calc_pseudobuff_matk, 7452): the Transcendence passive is read before the `sc == nullptr` test ------
chain("matk_flat", {
	lo = 0, hi = C.USHRT_MAX, conv = "u16", facts = { "skill.NV_TRANSCENDENCE" },
	pre = { { id = "transcendence", fn = function(acc, contribs) -- 7454-7456: matk += 15 * lv + (lv > 4 ? 25 : 0)
		local lv = facts_of(contribs)["skill.NV_TRANSCENDENCE"] or 0
		if lv > 0 then acc = acc + 15 * lv + (lv > 4 and 25 or 0) end
		return acc
	end } },
})

chain("cri", { lo = 10, hi = C.SHRT_MAX, conv = "i16" })    -- 7525
chain("hit", { lo = 1, hi = C.SHRT_MAX, conv = "i16" })     -- 7565
chain("flee2", { lo = 10, hi = C.SHRT_MAX, conv = "i16" })  -- 7745
chain("def", { lo = C.DEFTYPE_MIN, hi = C.DEFTYPE_MAX, conv = "i16" })    -- 7769
chain("def2", { lo = C.SHRT_MIN, hi = C.SHRT_MAX, conv = "i16" })         -- 7878 (RENEWAL: negative soft DEF allowed)
chain("mdef", { lo = C.DEFTYPE_MIN, hi = C.DEFTYPE_MAX, conv = "i16" })   -- 7944
chain("mdef2", { lo = C.SHRT_MIN, hi = C.SHRT_MAX, conv = "i16" })        -- 8002
for _, a in ipairs({ "patk", "smatk", "res", "mres", "hplus", "crate" }) do   -- 8569-8724: cap_value(x, 0, SHRT_MAX)
	chain(a, { lo = 0, hi = C.SHRT_MAX, conv = "i16" })
end

-- ---- flee (7633): the GvG / battleground penalty comes first, for players, even without any status ---------------------------------
chain("flee", {
	lo = 1, hi = C.SHRT_MAX, conv = "i16", facts = { "is_pc", "map", "cfg_gvg_flee_penalty", "cfg_bg_flee_penalty" },
	pre = { { id = "map_penalty", fn = function(acc, contribs) -- 7635-7641
		local F = facts_of(contribs)
		if F.is_pc == 1 then
			if F.map == 1 then acc = num.i32(acc - num.mul_div(acc, F.cfg_gvg_flee_penalty, 100))
			elseif F.map == 2 then acc = num.i32(acc - num.mul_div(acc, F.cfg_bg_flee_penalty, 100)) end
		end
		return acc
	end } },
})

-- ---- element / mode (8790-8908): the first applicable statement returns; unsigned char results wrap ----------------------------------
chain("def_ele", { lo = 0, hi = C.UCHAR_MAX, conv = "u8" })     -- status_calc_element 8790
chain("ele_lv", { lo = 1, hi = 4, conv = "u8" })                -- status_calc_element_lv 8824
chain("atk_ele", { lo = 0, hi = C.UCHAR_MAX, conv = "u8" })     -- status_calc_attack_element 8857
chain("mode", { lo = C.MD_NONE, hi = C.INT_MAX, conv = "i32" }) -- status_calc_mode 8895

-- ---- ASPD (RENEWAL_ASPD) --------------------------------------------------------------------------------------------------------------
-- percentage group (status_calc_aspd(bl, sc, false), 8308-8382): a sum, narrowed to int16; the two passive skills of players come last
chain("aspd_pct", {
	conv = "i16", facts = { "is_pc", "sc_empty", "skill.BA_MUSICALLESSON", "skill.RG_PLAGIARISM" },
	pre = { { id = "skills", fn = function(acc, contribs) -- 8373-8381: the passive skills of players; `if (sc == nullptr || sc->empty()) return 0` (8262) comes first
		local F = facts_of(contribs)
		if F.is_pc == 1 and F.sc_empty == 0 then
			local lv = F["skill.BA_MUSICALLESSON"] or 0
			if lv > 0 then acc = acc + lv end
			lv = F["skill.RG_PLAGIARISM"] or 0
			if lv > 0 then acc = acc + lv end
		end
		return acc
	end } },
})

-- fixed group (status_calc_aspd(bl, sc, true), 8265-8307): the best of the quicken family (no Quagmire), Assassin Cross, Madness/Berserk, potion
M.attributes.aspd_fixed = {
	name = "aspd_fixed", ret = "i16", facts = {},
	ops = { quicken7 = "quicken", quicken6 = "quicken", quicken5 = "quicken", no_quicken = "quicken", assncros = "assncros",
		madness = "madness", berserk = "madness", potion = "potion" },
	stages = {
		{ id = "base", fn = base_stage },
		{ id = "quicken", fn = function(acc, contribs) -- 8268-8276: bonus is 0 here, so every `bonus < N` holds
			if has_op(contribs, "no_quicken") then return acc end
			if has_op(contribs, "quicken7") then return 7 end
			if has_op(contribs, "quicken6") then return 6 end
			if has_op(contribs, "quicken5") then return 5 end
			return acc
		end },
		{ id = "assncros", fn = function(acc, contribs) -- 8278-8280 (RENEWAL: no weapon test): if (ASSNCROS && bonus < val2) bonus += val2
			local v = op_value(contribs, "assncros")
			if v ~= nil and acc < v then acc = acc + v end
			return acc
		end },
		{ id = "madness", fn = function(acc, contribs) -- 8301-8304: if (bonus < 20 && Madness Cancel) bonus = 20; else if (bonus < 15 && Berserk) bonus = 15
			if acc < 20 and has_op(contribs, "madness") then return 20 end
			if acc < 15 and has_op(contribs, "berserk") then return 15 end
			return acc
		end },
		{ id = "potion", fn = function(acc, contribs) -- 8306-8307: the first present of ASPDPOTION3 / 2 / 1 / 0 adds its val1
			local best
			for _, c in ipairs(collect(contribs, "potion")) do
				if best == nil or tonumber(c.arg) < tonumber(best.arg) then best = c end
			end
			if best then acc = acc + best.value end
			return acc
		end },
		{ id = "finish", fn = function(acc) return num.wrap_i16(acc) end },
	},
}

-- fix_aspd (8397): amotion in 1/10 ms units, cap_value(x, 1, MIN_ASPD)
M.attributes.fix_aspd = {
	name = "fix_aspd", ret = "i16", facts = {}, lo = 1, hi = C.MIN_ASPD,
	ops = generic_ops_map(),
	stages = { { id = "base", fn = base_stage }, { id = "ops", fn = stage_of_ops(num.wrap_i16) }, { id = "finish", fn = stage_finish(1, C.MIN_ASPD, num.wrap_i16) } },
}

-- aspd_rate (8427): 1000 = 100 %; the best "max" bonus first, then Berserk / Madness, the potion, then plain adds, cap_value(x, 0, SHRT_MAX)
M.attributes.aspd_rate = {
	name = "aspd_rate", ret = "i16", facts = { "is_pc", "ranged_weapon" }, lo = 0, hi = C.SHRT_MAX,
	ops = { max_assign = "max", max_cand = "max", assncros_max = "max", berserk = "rate", madness = "rate", potion = "potion", add = "ops", sub = "ops" },
	stages = {
		{ id = "base", fn = base_stage },
		{ id = "max", fn = function(acc, contribs) -- 8434-8489: int max = 0; Star Comfort assigns, the others raise it when larger
			local m = 0
			local star = op_value(contribs, "max_assign")
			if star ~= nil then m = star end
			for _, c in ipairs(collect(contribs, "max")) do
				if c.op == "max_cand" and m < c.value then m = c.value end
				if c.op == "assncros_max" then -- 8474-8489: non-players always, players unless the weapon is ranged
					local F = facts_of(contribs)
					if m < c.value and (F.is_pc == 0 or F.ranged_weapon == 0) then m = c.value end
				end
			end
			return acc - m
		end },
		{ id = "rate", fn = function(acc, contribs) -- 8492-8495: Berserk -300, else Madness Cancel -200
			if has_op(contribs, "berserk") then return acc - 300 end
			if has_op(contribs, "madness") then return acc - 200 end
			return acc
		end },
		{ id = "potion", fn = function(acc, contribs) -- 8497-8501: the first present of ASPDPOTION3 / 2 / 1 / 0 lowers the rate by its val2
			local best
			for _, c in ipairs(collect(contribs, "potion")) do
				if best == nil or tonumber(c.arg) < tonumber(best.arg) then best = c end
			end
			if best then acc = acc - best.value end
			return acc
		end },
		{ id = "ops", fn = stage_of_ops(ident) },
		{ id = "finish", fn = stage_finish(0, C.SHRT_MAX, num.wrap_i16) },
	},
}

-- ---- movement speed ------------------------------------------------------------------------------------------------------------------
-- speed_rate: the percentage that status_calc_speed builds in its local `speed_rate` (8042-8227); an attribute of its own so that `speed`
-- (the product) reads it as a fact. Statuses contribute to it with the ops below, the unit's facts (mounts, skills, items) enter as facts.
local function freecast(F) -- 8061: a skill being cast and (Free Cast known or Exceed Break)
	return F.is_pc == 1 and F.skilltimer == 1 and ((F["skill.SA_FREECAST"] or 0) > 0 or F.exeedbreak == 1)
end
M.attributes.speed_rate = {
	name = "speed_rate", ret = "i32",
	facts = { "is_pc", "skilltimer", "exeedbreak", "ud_skill_lv", "riding", "wug_rider", "sex", "bonus_speed_rate", "bonus_speed_add_rate",
		"cfg_rental_mount_speed_boost", "second_assassin", "skill.SA_FREECAST", "skill.RA_WUGRIDER", "skill.RG_TUNNELDRIVE", "skill.TF_MISS",
		"skill.BA_MUSICALLESSON", "skill.DC_DANCINGLESSON" },
	ops = { haste2_fusion = "haste2", haste2_allriding = "haste2", slow_hiding = "slow", slow_chasewalk_neg = "slow", slow_fatigue = "slow",
		slow_dance = "slow", dance_bard = "slow", slow = "slow", cap_marsh = "slow_cap", haste = "haste1" },
	stages = {
		{ id = "base", fn = base_stage },
		{ id = "freecast", fn = function(acc, contribs) -- 8061-8065
			local F = facts_of(contribs)
			if not freecast(F) then return acc end
			if F.exeedbreak == 1 then return 160 - 10 * F.ud_skill_lv end
			return 175 - 5 * (F["skill.SA_FREECAST"] or 0)
		end },
		{ id = "haste2", fn = function(acc, contribs) -- GetMoveHasteValue2 8069-8080: Fusion, else (players) riding / dragon, wug rider, rental mount
			local F = facts_of(contribs)
			if freecast(F) then return acc end
			local val = 0
			if has_op(contribs, "haste2_fusion") then val = op_value(contribs, "haste2_fusion")
			elseif F.is_pc == 1 then
				if F.riding == 1 then val = 25
				elseif F.wug_rider == 1 then val = 15 + 5 * (F["skill.RA_WUGRIDER"] or 0)
				elseif has_op(contribs, "haste2_allriding") then val = F.cfg_rental_mount_speed_boost end
			end
			return acc - val
		end },
		{ id = "slow", fn = function(acc, contribs) -- GetMoveSlowValue 8082-8161
			local F = facts_of(contribs)
			if freecast(F) then return acc end
			local val
			if has_op(contribs, "slow_hiding") then
				val = op_value(contribs, "slow_hiding")                 -- 8083: Hiding with Tunnel Drive (players): 120 - 6 * lv
			elseif has_op(contribs, "slow_chasewalk_neg") then
				val = op_value(contribs, "slow_chasewalk_neg")          -- 8085: Chase Walk with a negative val3 (players)
			else
				val = 0
				-- 8091-8099: Ensemble Fatigue; otherwise (players) the dance penalty. The spirit link (Bard / Dancer) is a flag.
				local fat = op_value(contribs, "slow_fatigue")
				if fat ~= nil then
					val = num.max(val, fat)
				elseif has_op(contribs, "slow_dance") and F.is_pc == 1 then
					local lesson = F.sex ~= 0 and (F["skill.BA_MUSICALLESSON"] or 0) or (F["skill.DC_DANCINGLESSON"] or 0)
					val = num.max(val, 500 - (40 + 10 * (has_op(contribs, "dance_bard") and 1 or 0)) * lesson)
				end
				for _, c in ipairs(contribs) do
					if parse(c.tag).op == "slow" then val = num.max(val, c.value) end
				end
				local item = F.bonus_speed_rate + F.bonus_speed_add_rate -- 8158: permanent item-based slow (players)
				if F.is_pc == 1 and item > 0 then val = num.max(val, item) end
			end
			return acc + val
		end },
		{ id = "slow_cap", fn = function(acc, contribs) -- 8164: Marsh of Abyss caps the slow at 150 %
			local F = facts_of(contribs)
			if freecast(F) then return acc end
			if has_op(contribs, "cap_marsh") and acc > 150 then return 150 end
			return acc
		end },
		{ id = "haste1", fn = function(acc, contribs) -- GetMoveHasteValue1 8167-8221, then the floor of 40 (8223-8226)
			local F = facts_of(contribs)
			if freecast(F) then return acc end
			local val = 0
			for _, c in ipairs(contribs) do
				if parse(c.tag).op == "haste" then val = num.max(val, c.value) end
			end
			if F.is_pc == 1 and F.second_assassin == 1 and (F["skill.TF_MISS"] or 0) > 0 then val = num.max(val, F["skill.TF_MISS"]) end -- 8176
			local item = F.bonus_speed_rate + F.bonus_speed_add_rate
			if F.is_pc == 1 and item < 0 then val = num.max(val, -item) end -- 8220: permanent item-based speed-up
			acc = acc - val
			if acc < 40 then acc = 40 end
			return acc
		end },
	},
}

local function skip(F) return F.nosc == 1 or (F.is_pc == 1 and (F.perm_speed == 1 or F.mado == 1)) end -- 8044 / 8047: early returns

-- speed (8039-8245): base speed * speed_rate / 100, with the special cases of the players' mount and cart
M.attributes.speed = {
	name = "speed", ret = "u16", lo = C.MIN_WALK_SPEED, hi = C.MAX_WALK_SPEED,
	facts = { "is_pc", "nosc", "perm_speed", "mado", "skill.NC_MADOLICENCE", "skill.MC_PUSHCART" },
	needs = { speed_rate = { base = 100 } },
	ops = { acceleration = "mado", cart = "cart", paralyse_slow = "paralyse", steelbody = "steelbody", min200 = "min200", walkspeed = "walkspeed" },
	stages = {
		{ id = "base", fn = base_stage },
		{ id = "mado", fn = function(acc, contribs) -- 8047-8058: Madogear ignores every other status
			local F = facts_of(contribs)
			if F.nosc == 1 or (F.is_pc == 1 and F.perm_speed == 1) or not (F.is_pc == 1 and F.mado == 1) then return acc end
			local lv, val = F["skill.NC_MADOLICENCE"] or 0, 0
			if lv < 5 then val = 50 - 10 * lv else val = val - 25 end
			if has_op(contribs, "acceleration") then val = val - 25 end
			return num.i32(acc + num.mul_div(acc, val, 100))
		end },
		{ id = "cart", fn = function(acc, contribs) -- 8230: players with a cart
			local F = facts_of(contribs)
			if skip(F) then return acc end
			if F.is_pc == 1 and has_op(contribs, "cart") then
				acc = num.i32(acc + num.mul_div(acc, 50 - 5 * (F["skill.MC_PUSHCART"] or 0), 100))
			end
			return acc
		end },
		{ id = "paralyse", fn = function(acc, contribs) -- 8232: Paralysis (val3 == 1) +50 %
			local F = facts_of(contribs)
			if skip(F) then return acc end
			if has_op(contribs, "paralyse_slow") then acc = num.i32(acc + num.mul_div(acc, 50, 100)) end
			return acc
		end },
		{ id = "rate", fn = function(acc, contribs) -- 8234: if (speed_rate != 100) speed = speed * speed_rate / 100
			local F = facts_of(contribs)
			if skip(F) then return acc end
			local rate = F.speed_rate
			if rate ~= 100 then acc = num.mul_div(acc, rate, 100) end
			return acc
		end },
		{ id = "steelbody", fn = function(acc, contribs) -- 8236: Steel Body 200
			local F = facts_of(contribs)
			if skip(F) then return acc end
			if has_op(contribs, "steelbody") then return 200 end
			return acc
		end },
		{ id = "min200", fn = function(acc, contribs) -- 8238-8241: Defender / Armor
			local F = facts_of(contribs)
			if skip(F) then return acc end
			if has_op(contribs, "min200") then return num.max(acc, 200) end
			return acc
		end },
		{ id = "walkspeed", fn = function(acc, contribs) -- 8242: Walk Speed (val1 > 0)
			local F = facts_of(contribs)
			if skip(F) then return acc end
			local v = op_value(contribs, "walkspeed")
			if v ~= nil and v > 0 then acc = num.mul_div(acc, 100, v) end
			return acc
		end },
		{ id = "finish", fn = function(acc) return num.wrap_u16(num.cap(acc, C.MIN_WALK_SPEED, C.MAX_WALK_SPEED)) end },
	},
}

-- ---- maximum HP / SP / AP (8733-8781, status_get_hpbonus 3169, status_get_spbonus 3329, status_get_apbonus 3457) ---------------------------
-- fixed and rate bonuses are attributes of their own; the apply stage works in uint64 like the legacy code (a negative fixed bonus larger than
-- the base wraps around and is then capped to UINT_MAX).
local u64 = import("scripts/status/u64")

local function apply_stage(lo)
	return function(acc, contribs)
		local F = facts_of(contribs)
		local fix, rate = F.fix, F.rate
		local v = u64.from(acc)
		v = u64.add_i32(v, fix)                             -- maxhp += status_get_hpbonus(bl, STATUS_BONUS_FIX)
		local r = 100 + rate                                -- rate += status_get_hpbonus(bl, STATUS_BONUS_RATE)
		if r ~= 100 then v = u64.mul_div_small(v, r, 100) end
		return u64.cap(v, lo, C.UINT_MAX)                   -- cap_value(maxhp, 1, UINT_MAX)
	end
end

local function hp_sp_ap(prefix, apply_lo, fixfacts, ratefacts, fixfn, ratefn)
	local sum_ops = { add = "ops", sub = "ops" }
	M.attributes[prefix .. "_fix"] = {
		name = prefix .. "_fix", ret = "i32", facts = fixfacts, ops = sum_ops, reads_unit_sc = true,
		stages = { { id = "base", fn = base_stage }, { id = "unit", fn = fixfn }, { id = "ops", fn = stage_of_ops(ident) } },
	}
	M.attributes[prefix .. "_rate"] = {
		name = prefix .. "_rate", ret = "i32", facts = ratefacts, ops = sum_ops, reads_unit_sc = true,
		stages = { { id = "base", fn = base_stage }, { id = "unit", fn = ratefn }, { id = "ops", fn = stage_of_ops(ident) },
			{ id = "cap", fn = function(acc) return num.cap(acc, -100, C.INT_MAX) end } },     -- "Max rate reduce is -100%" (3288 / 3415 / 3495)
	}
	M.attributes["max" .. prefix] = {
		name = "max" .. prefix, ret = "u32", facts = {}, reads_unit_sc = true, needs = { [prefix .. "_fix"] = { as = "fix" }, [prefix .. "_rate"] = { as = "rate" } },
		ops = {}, stages = { { id = "base", fn = base_stage }, { id = "apply", fn = apply_stage(apply_lo) } },
	}
end

local function pc_only(fn) -- the unit part of a bonus exists for players only
	return function(acc, contribs)
		if facts_of(contribs).is_pc ~= 1 then return acc end
		return fn(acc, facts_of(contribs))
	end
end
local function sk(F, name) return F["skill." .. name] or 0 end

hp_sp_ap("hp", 1,
	{ "is_pc", "bonus_hp", "summoner_sea", "skill.CR_TRUST", "skill.SU_SPRITEMABLE", "skill.SU_POWEROFSEA", "skill.NV_BREAKTHROUGH", "skill.NV_TRANSCENDENCE" },
	{ "is_pc" },
	pc_only(function(acc, F) -- 3176-3194
		acc = acc + F.bonus_hp
		if sk(F, "CR_TRUST") > 0 then acc = acc + sk(F, "CR_TRUST") * 200 end
		if sk(F, "SU_SPRITEMABLE") > 0 then acc = acc + 1000 end
		if sk(F, "SU_POWEROFSEA") > 0 then
			acc = acc + 1000
			if F.summoner_sea >= 20 then acc = acc + 3000 end
		end
		local lv = sk(F, "NV_BREAKTHROUGH")
		if lv > 0 then acc = acc + 350 * lv + (lv > 4 and 250 or 0) end
		lv = sk(F, "NV_TRANSCENDENCE")
		if lv > 0 then acc = acc + 350 * lv + (lv > 4 and 250 or 0) end
		return acc
	end),
	function(acc) return acc end)

hp_sp_ap("sp", 1,
	{ "is_pc", "bonus_sp", "summoner_sea", "skill.SL_KAINA", "skill.RA_RESEARCHTRAP", "skill.WM_LESSON", "skill.SU_SPRITEMABLE", "skill.SU_POWEROFSEA",
		"skill.NV_BREAKTHROUGH", "skill.NV_TRANSCENDENCE" },
	{ "is_pc", "sex", "skill.HP_MEDITATIO", "skill.HW_SOULDRAIN", "skill.BA_MUSICALLESSON", "skill.DC_DANCINGLESSON" },
	pc_only(function(acc, F) -- 3336-3358
		acc = acc + F.bonus_sp
		if sk(F, "SL_KAINA") > 0 then acc = acc + 30 * sk(F, "SL_KAINA") end
		if sk(F, "RA_RESEARCHTRAP") > 0 then acc = acc + 200 + 20 * sk(F, "RA_RESEARCHTRAP") end
		if sk(F, "WM_LESSON") > 0 then acc = acc + 30 * sk(F, "WM_LESSON") end
		if sk(F, "SU_SPRITEMABLE") > 0 then acc = acc + 100 end
		if sk(F, "SU_POWEROFSEA") > 0 then
			acc = acc + 100
			if F.summoner_sea >= 20 then acc = acc + 300 end
		end
		local lv = sk(F, "NV_BREAKTHROUGH")
		if lv > 0 then acc = acc + 30 * lv + (lv > 4 and 50 or 0) end
		lv = sk(F, "NV_TRANSCENDENCE")
		if lv > 0 then acc = acc + 30 * lv + (lv > 4 and 50 or 0) end
		return acc
	end),
	pc_only(function(acc, F) -- 3380-3394 (RENEWAL: the Dancing/Musical Lesson rate)
		if sk(F, "HP_MEDITATIO") > 0 then acc = acc + sk(F, "HP_MEDITATIO") end
		if sk(F, "HW_SOULDRAIN") > 0 then acc = acc + 2 * sk(F, "HW_SOULDRAIN") end
		local lesson = F.sex ~= 0 and sk(F, "BA_MUSICALLESSON") or sk(F, "DC_DANCINGLESSON")
		if lesson > 0 then acc = acc + lesson end
		return acc
	end))

hp_sp_ap("ap", 0, { "is_pc", "bonus_ap" }, { "is_pc" },
	pc_only(function(acc, F) return acc + F.bonus_ap end), -- 3468
	function(acc) return acc end)

return M
