--!nonstrict
-- Pre-renewal basic weapon attack (no skill): BR-RULESET-001..008, 014..016, 020, 021, 027..031, 033, 036,
-- 039..041, in the order of the legacy battle_calc_weapon_attack (battle.cpp:5418-5822, !RENEWAL).
--
-- Stage order (see pipeline.ORDER.weapon; the golden traces fix it):
--   init, base_attack, skill_ratio, const_add, skill_atk_bonus, defense, post_defense, element, element_bonus,
--   card_attacker, card_target, div_hands, target_modifiers, map_rates, final
--
-- ====================================================================================================================
-- ctx interface (everything the pipeline needs from the host; nothing else is touched)
--
--   ctx.rng.rnd()           -> integer in [0, 2^31-1]   legacy rnd()                       (one draw)
--   ctx.rng.value(lo, hi)   -> integer in [lo, hi]      legacy rnd_value(lo, hi)           (one draw; lo <= hi)
--   ctx.idiv(a, b)          -> int64 a / b truncated toward zero (C++ semantics; b ~= 0)
--   ctx.mul_div(v, m, d)    -> int64 v * m / d, the product kept in int64, then truncated toward zero
--   ctx.acc(v)              -> int64 accumulator (the "damage lane"), methods:
--                                :get()          current value
--                                :set(v)
--                                :add(v)         value = value + v
--                                :mul_pct(p)     value = value * p / 100          (trunc)   [M0 host.cpp: mul_pct]
--                                :add_pct(p)     value = value + value * p / 100  (trunc)   [new: ATK_ADDRATE]
--                                :clamp_min(lo)  value = max(value, lo)                     [M0 host.cpp: min1 = clamp_min(1)]
--   The M0 host (tooling/m0/host.cpp) already provides get/set/add/mul_pct/mul/idiv/min1; this file additionally
--   needs add_pct, clamp_min, and the scalar idiv / mul_div. Luau `//` and `/` are never used on signed values
--   (they floor); `%` is only applied to non-negative RNG output and to the non-negative modulus 65536.
--
-- Unit snapshot (BR-RULESET-117 "immutable unit snapshots"), plain tables:
--   kind "pc"|"mob"; level; str agi vit int dex luk; batk hit cri flee flee2 def def2 (soft DEF);
--   size (0 small, 1 medium, 2 large); race; element (defending element); ele_lv; boss (bool); targeted (attackers on it)
--   rhw / lhw = { atk, atk2, ele, wlv }   wlv > 0 means a weapon is equipped in that hand (pc); mobs use atk..atk2
--   wtype1, wtype2 (weapon types), arrow (bool: ranged arrow attack), job ("thief" halves dual-wield hands), no_sizefix
--   skills = { as_right, as_left, ko_right, ko_left } (dual-wield passive levels; job "thief" or "kagerou")
--   bonus = { arrow_atk, crit_atk_rate, non_crit_atk_rate, short_atk, long_atk, near_def, long_def, crit_def,
--             crit_def_rate, overrefine_r, overrefine_l, perfect_hit, arrow_hit, arrow_cri, crit_range, skill_ratio,
--             add_race/add_ele/add_size/add_class (right hand), ladd_* (left hand), aadd_* (arrow),
--             sub_race/sub_ele/sub_size/sub_class/sub_defele, critadd_race }   (tables: index -> percent)
--   sc = { berserk = {val1..val4}, curse, provoke, concentration, edp, maximize, weaponperfection, overthrust, deluge,
--          volcano, violentgale, enchantarms, assumptio, soundofdestruction, shadow_clock, defender, adjustment,
--          smokepowder, water_barrier }
-- ====================================================================================================================
local C = import("scripts/damage/consts")
local TABLES = import("data/damage_tables")

local M = {}

local ALL = {} -- empty table for absent bonus maps

local function bit_set(mask, bit) -- mask & bit for a single-bit value; both are small non-negative integers
	return mask % (bit * 2) >= bit
end

local function wrap16(x) -- C++ narrowing to int16
	return (x + 32768) % 65536 - 32768
end

local function bl_type(u)
	return u.kind == "pc" and C.BL_PC or C.BL_MOB
end

local function sc_of(u, name)
	local sc = u.sc
	return sc and sc[name] or nil
end

local function probe(S, name, a, b)
	local t = S.trace
	if t then t[#t + 1] = { name, a, b or 0 } end
end

local function bonus_of(u)
	return u.bonus or ALL
end

local function at(map, idx) -- bonus map lookup, absent = 0
	return map and map[idx] or 0
end

-- --------------------------------------------------------------------------------------------------------------------
-- unsupported input guard: the golden data covers a closed input space; anything outside it is refused loudly
-- --------------------------------------------------------------------------------------------------------------------
local function validate(src, tgt)
	assert(src.kind == "pc" or src.kind == "mob", "src.kind")
	assert(tgt.kind == "pc" or tgt.kind == "mob", "tgt.kind")
	local b = bonus_of(src)
	assert((b.double_rate or 0) == 0, "double attack (BR-RULESET-004) is a skill rule, not covered")
	if src.kind == "mob" then
		assert(not src.lhw or (src.lhw.atk or 0) == 0, "mob left hand not covered")
	end
	if src.kind == "pc" then
		assert(not (src.wtype1 == C.W_FIST and (src.wtype2 or 0) ~= C.W_FIST), "left-hand-only fist not covered")
		assert(src.wtype1 ~= C.W_KATAR, "katar not covered")
		if src.arrow then assert(src.wtype1 == C.W_BOW, "arrow attacks are covered for bows only") end
	end
end

-- --------------------------------------------------------------------------------------------------------------------
-- hands (battle.cpp:2960-3002, skill_id == 0)
-- --------------------------------------------------------------------------------------------------------------------
local function left_handed(src)
	if src.kind == "pc" and src.wtype1 == C.W_FIST and (src.wtype2 or 0) ~= C.W_FIST then return true end
	return src.lhw ~= nil and (src.lhw.atk or 0) ~= 0
end

-- --------------------------------------------------------------------------------------------------------------------
-- lane macros (ATK_RATE, ATK_ADDRATE, ATK_ADD): the left lane follows only when the attack is left handed
-- --------------------------------------------------------------------------------------------------------------------
local function atk_rate(S, p)
	S.dmg:mul_pct(p)
	if S.left then S.dmg2:mul_pct(p) end
end

local function atk_add_rate(S, p)
	S.dmg:add_pct(p)
	if S.left then S.dmg2:add_pct(p) end
end

local function atk_add2(S, a, b)
	S.dmg:add(a)
	if S.left then S.dmg2:add(b) end
end

-- battle_min_damage(wd, src, skill 0, min): right hand capped at `min`, left hand at 0
local function min_damage(S, min)
	S.dmg:clamp_min(min)
	if S.left then S.dmg2:clamp_min(0) end
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-029: weapon element of a hand (skill 0 -> the weapon's element; an endow status overrides it)
-- --------------------------------------------------------------------------------------------------------------------
function M.weapon_element(src, hand)
	local e
	if hand == "l" then e = src.lhw and src.lhw.ele or 0 else e = src.rhw.ele end
	local endow = sc_of(src, "enchantarms")
	if endow then e = endow.val1 end
	return e
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-003: lucky dodge (a status flee2 of 0 skips the draw)
-- --------------------------------------------------------------------------------------------------------------------
function M.lucky_dodge(ctx, tgt)
	return tgt.flee2 ~= 0 and ctx.rng.rnd() % 1000 < tgt.flee2
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-005 / BR-MAP-010: crit chance (0.1 % units) and the draw. Returns crit, final_cri.
-- --------------------------------------------------------------------------------------------------------------------
function M.crit_roll(ctx, src, tgt, S)
	if src.cri == 0 then return false, nil end -- `if (sstatus->cri)`: no draw at all
	local b = bonus_of(src)
	local cri = src.cri
	if src.kind == "pc" then
		cri = cri + at(b.critadd_race, tgt.race) + at(b.critadd_race, C.RC_ALL)
		if src.arrow then cri = cri + (b.arrow_cri or 0) + (b.crit_range or 0) end
	end
	local luk_mult = (src.kind ~= "pc" and tgt.kind == "pc") and 3 or 2
	cri = wrap16(cri - tgt.luk * luk_mult) -- raw value, stored in an int16 as legacy
	local cd = tgt.kind == "pc" and (bonus_of(tgt).crit_def or 0) or 0
	if cd ~= 0 then cri = ctx.mul_div(cri, 100 - cd, 100) end
	if S then probe(S, "cri", cri) end
	return ctx.rng.rnd() % 1000 < cri, cri -- drawn even when cri <= 0
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-006 / BR-MAP-011, 012: hit rate and the draw. `crit` short-circuits to a hit (no draw).
-- --------------------------------------------------------------------------------------------------------------------
function M.crowd_penalty(ctx, value, attackers, ptype, count, num)
	if ptype == 0 or attackers < count then return value end
	local extra = attackers - (count - 1)
	if ptype == 1 then return ctx.mul_div(value, 100 - extra * num, 100) end
	return value - extra * num
end

function M.hit_roll(ctx, cfg, src, tgt, crit, S)
	if crit then return true end
	local b = bonus_of(src)
	if src.kind == "pc" and (b.perfect_hit or 0) > 0 and ctx.rng.rnd() % 100 < b.perfect_hit then return true end
	local flee = tgt.flee
	if cfg.agi_penalty_type ~= 0 and bit_set(cfg.agi_penalty_target, bl_type(tgt)) then
		local n = tgt.targeted or 0
		if n >= cfg.agi_penalty_count then
			flee = wrap16(M.crowd_penalty(ctx, flee, n, cfg.agi_penalty_type, cfg.agi_penalty_count, cfg.agi_penalty_num))
			if flee < 1 then flee = 1 end
		end
	end
	local hitrate = 80 + src.hit - flee
	if src.kind == "pc" and src.arrow then hitrate = hitrate + (b.arrow_hit or 0) end
	if hitrate < cfg.min_hitrate then hitrate = cfg.min_hitrate end
	if hitrate > cfg.max_hitrate then hitrate = cfg.max_hitrate end
	if S then probe(S, "hitrate", hitrate, flee) end
	return ctx.rng.rnd() % 100 < hitrate, hitrate -- drawn even when hitrate <= 0
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-008: base damage of one hand (battle_calc_base_damage, battle.cpp:2519-2639).
-- flags: crit, arrow, nosize. Draw order: weapon roll, arrow_atk roll, over-refine roll.
-- --------------------------------------------------------------------------------------------------------------------
function M.base_range(ctx, src, hand, crit, arrow)
	local wa = hand == "l" and src.lhw or src.rhw
	local atkmin, atkmax = 0, 0
	if src.kind ~= "pc" then
		atkmin, atkmax = wa.atk, wa.atk2
		if atkmin > atkmax then atkmin = atkmax end
	else
		atkmax = wa.atk
		if not crit or arrow then
			atkmin = src.dex
			if (wa.wlv or 0) > 0 then atkmin = ctx.idiv(atkmin * (80 + wa.wlv * 20), 100) end
			if atkmin > atkmax then atkmin = atkmax end
			if arrow then
				atkmin = ctx.idiv(atkmin * atkmax, 100)
				if atkmin > atkmax then atkmax = atkmin end
			end
		end
	end
	return atkmin, atkmax
end

function M.base_damage(ctx, src, hand, tgt_size, crit, arrow, nosize, S)
	local b = bonus_of(src)
	local atkmin, atkmax = M.base_range(ctx, src, hand, crit, arrow)
	if S then probe(S, "basemm", atkmin, atkmax) end
	if sc_of(src, "maximize") then atkmin = atkmax end
	local damage
	if not crit then
		damage = atkmin
		if atkmax > atkmin then damage = damage + ctx.rng.rnd() % (atkmax - atkmin) end -- never returns atkmax (Q2)
	else
		damage = atkmax
	end
	if src.kind == "pc" then
		local aa = b.arrow_atk or 0
		if arrow and aa ~= 0 then
			if crit then damage = damage + aa else damage = damage + ctx.rng.rnd() % aa end
		end
		if not (src.no_sizefix or nosize) then
			local w = hand == "l" and (src.wtype2 or 0) or src.wtype1
			damage = ctx.mul_div(damage, TABLES.size_pct[w][tgt_size + 1], 100)
		end
	end
	damage = damage + src.batk
	if src.kind == "pc" then -- battle_add_weapon_damage: over-refine bonus is part of base ATK
		local over = hand == "l" and (b.overrefine_l or 0) or (b.overrefine_r or 0)
		if over ~= 0 then damage = damage + ctx.rng.rnd() % over + 1 end
	end
	return damage
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-014: percent added to the base damage after the roll: crit_atk_rate on a crit, non_crit_atk_rate otherwise
-- (players only; only positive values count). The target's crit_def_rate is applied separately (see calc_attack).
function M.base_rate_pct(src, tgt, crit)
	if src.kind ~= "pc" then return 0 end
	local b = bonus_of(src)
	if crit then
		if (b.crit_atk_rate or 0) > 0 then return b.crit_atk_rate end
	elseif (b.non_crit_atk_rate or 0) > 0 then
		return b.non_crit_atk_rate
	end
	return 0
end

-- ATK% of statuses (battle_get_atkpercent): PRE multiplies the damage after the ratio (BR-RULESET-016)
-- --------------------------------------------------------------------------------------------------------------------
function M.atk_percent(src)
	local p = 100
	if sc_of(src, "curse") then p = p - 25 end
	local v = sc_of(src, "provoke")
	if v then p = p + v.val2 end
	v = sc_of(src, "concentration")
	if v then p = p + v.val2 end
	if p < 0 then p = 0 end
	if p > 65535 then p = 65535 end
	return p
end

-- skill ratio of a plain attack (BR-RULESET-015): 100 + bonuses; Berserk +100 in PRE
function M.skill_ratio(src)
	local r = 100 + (src.kind == "pc" and (bonus_of(src).skill_ratio or 0) or 0)
	local v = sc_of(src, "overthrust")
	if v then r = r + v.val3 end
	if sc_of(src, "berserk") then r = r + 100 end
	return r
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-027 / 028: DEF reduction (battle_calc_defense_reduction) and post-defense (refine bonus, min 1)
-- --------------------------------------------------------------------------------------------------------------------
-- soft DEF ("vit def") of the target, with its RNG draw. Returns vit_def, def1(after crowd), def2(after crowd)
function M.vit_def(ctx, cfg, tgt)
	local def1 = tgt.def
	if def1 < C.DEF_MIN then def1 = C.DEF_MIN end
	if def1 > C.DEF_MAX then def1 = C.DEF_MAX end
	local def2 = tgt.def2
	if cfg.vit_penalty_type ~= 0 and bit_set(cfg.vit_penalty_target, bl_type(tgt)) then
		local cap = ctx.idiv(100, cfg.vit_penalty_num) + (cfg.vit_penalty_count - 1)
		local n = tgt.targeted or 0
		if n > cap then n = cap end
		if n >= cfg.vit_penalty_count then
			def1 = M.crowd_penalty(ctx, def1, n, cfg.vit_penalty_type, cfg.vit_penalty_count, cfg.vit_penalty_num)
			def1 = (def1 + 128) % 256 - 128 -- defType = signed char
			def2 = wrap16(M.crowd_penalty(ctx, def2, n, cfg.vit_penalty_type, cfg.vit_penalty_count, cfg.vit_penalty_num))
		end
		if def2 < 1 then def2 = 1 end -- applies to every penalised target type, even below the crowd threshold
	end
	local vit
	if tgt.kind == "pc" then
		-- BR-MAP-015: [3*def2/10] + rnd(0, max(0, [def2^2/150] - [3*def2/10] - 1)) + [def2/2]
		local t = ctx.idiv(3 * def2, 10)
		local base = t + ctx.idiv(def2, 2)
		local spread = ctx.idiv(def2 * def2, 150) - t - 1
		if spread < 0 then spread = 0 end
		vit = base + ctx.rng.value(0, spread)
	else
		-- def2 + rnd() % ([def2/20]^2); the draw is skipped when the width is 0
		local q = ctx.idiv(def2, 20)
		local width = q * q
		vit = def2
		if width > 0 then vit = vit + ctx.rng.rnd() % width end
	end
	vit = wrap16(vit)
	return vit, def1, def2
end

-- one lane: hard DEF as a percent, then the soft DEF; a crit ignores both (BR-RULESET-027, PRE)
function M.def_reduce(ctx, damage, def1, vit, crit, weapon_defense_type)
	if weapon_defense_type ~= 0 then
		vit = vit + def1 * weapon_defense_type
		def1 = 0
	end
	if def1 > 100 then def1 = 100 end
	if crit then return damage end
	return ctx.mul_div(damage, 100 - def1, 100) - vit
end

local function defense_stage(S)
	local ctx, tgt = S.ctx, S.tgt
	local vit, def1, def2 = M.vit_def(ctx, S.cfg, tgt)
	probe(S, "defs", def1, def2)
	probe(S, "vitdef", vit, def1) -- BR-RULESET-027: the soft-DEF draw above happens even when a crit ignores DEF
	local wdt = S.cfg.weapon_defense_type
	S.dmg:set(M.def_reduce(ctx, S.dmg:get(), def1, vit, S.crit, wdt))
	if S.left then S.dmg2:set(M.def_reduce(ctx, S.dmg2:get(), def1, vit, S.crit, wdt)) end
end

-- BR-RULESET-028 (PRE post-defense): refine bonus added to the reduced damage, then a minimum of 1 (0 for the left hand)
function M.post_defense(damage, refine)
	damage = damage + refine
	if damage < 1 then damage = 1 end
	return damage
end

local function post_defense_stage(S)
	local src = S.src
	if src.kind == "pc" then -- refine bonus (atk2) is added after DEF; for mobs atk2 is the roll's upper bound
		atk_add2(S, src.rhw.atk2, src.lhw and src.lhw.atk2 or 0)
	end
	min_damage(S, 1) -- masteries are 0 for a plain attack (no skills), the second min is the Aura Blade one
	min_damage(S, 1)
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-030 / 031 / 033 / 032: element table, status percent first, then the table (PRE)
-- --------------------------------------------------------------------------------------------------------------------
function M.attr_fix(ctx, src, tgt, damage, atk_elem)
	local ratio = TABLES.element_pct[tgt.ele_lv][atk_elem][tgt.element]
	local pct = 0
	if atk_elem == 3 then pct = (sc_of(src, "volcano") or ALL).val3 or 0
	elseif atk_elem == 4 then pct = (sc_of(src, "violentgale") or ALL).val3 or 0
	elseif atk_elem == 1 then pct = (sc_of(src, "deluge") or ALL).val3 or 0 end
	if pct ~= 0 then damage = damage + ctx.mul_div(damage, pct, 100) end
	return ctx.mul_div(damage, ratio, 100)
end

-- BR-RULESET-030: one lane through the element table; basic attacks of the types in attack_attr_none skip it
function M.element_damage(ctx, cfg, src, tgt, damage, atk_elem)
	if bit_set(cfg.attack_attr_none, bl_type(src)) then return damage end
	return M.attr_fix(ctx, src, tgt, damage, atk_elem)
end

-- BR-RULESET-032: with attr_recover 0 a negative result (negative table ratio) is cut to 0
function M.recover_cap(cfg, damage)
	if cfg.attr_recover == 0 and damage < 0 then return 0 end
	return damage
end

local function element_stage(S)
	local ctx, src, tgt, cfg = S.ctx, S.src, S.tgt, S.cfg
	probe(S, "ele", S.rele, S.lele)
	if S.dmg:get() > 0 or S.dmg2:get() > 0 then
		if bit_set(cfg.attack_attr_none, bl_type(src)) then return end -- BR-RULESET-030: skips the rest of the stage too
		S.dmg:set(M.attr_fix(ctx, src, tgt, S.dmg:get(), S.rele))
		if S.left then S.dmg2:set(M.attr_fix(ctx, src, tgt, S.dmg2:get(), S.lele)) end
	end
	probe(S, "attr", S.dmg:get(), S.dmg2:get())
	-- element_bonus (BR-RULESET-033): EDP adds val3 percent after the table, right hand only
	if S.dmg:get() > 0 or S.dmg2:get() > 0 then
		local edp = sc_of(src, "edp")
		if edp then S.dmg:add_pct(edp.val3) end
	end
	if cfg.attr_recover == 0 then min_damage(S, 0) end -- BR-RULESET-032
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-020 / 021: card fixes. Each returns the damage DELTA like battle_calc_cardfix.
-- --------------------------------------------------------------------------------------------------------------------
local function apply_cardfix(ctx, damage, fix)
	if fix < 0 then fix = 0 end
	return -ctx.mul_div(damage, 1000 - fix, 1000) -- damage - (damage - trunc(damage*(1000-fix)/1000))
end

local function step(ctx, cardfix, pct)
	return ctx.mul_div(cardfix, 100 + pct, 100)
end

-- attacker side, one hand (left = 2 for the right hand, 3 for the left hand in the legacy call)
function M.cardfix_attacker(ctx, cfg, src, tgt, damage, hand, flag)
	if damage == 0 or src.kind ~= "pc" then return 0 end
	local b = bonus_of(src)
	local race, ele, size, class = tgt.race, tgt.element, tgt.size, tgt.boss and 1 or 0
	local cardfix, cardfix_ = 1000, 1000
	if src.arrow then
		cardfix = step(ctx, cardfix, at(b.add_race, race) + at(b.aadd_race, race) + at(b.add_race, C.RC_ALL) + at(b.aadd_race, C.RC_ALL))
		cardfix = step(ctx, cardfix, at(b.add_ele, ele) + at(b.aadd_ele, ele) + at(b.add_ele, C.ELE_ALL) + at(b.aadd_ele, C.ELE_ALL))
		cardfix = step(ctx, cardfix, at(b.add_size, size) + at(b.aadd_size, size) + at(b.add_size, C.SZ_ALL) + at(b.aadd_size, C.SZ_ALL))
		cardfix = step(ctx, cardfix, at(b.add_class, class) + at(b.aadd_class, class) + at(b.add_class, C.CLASS_ALL) + at(b.aadd_class, C.CLASS_ALL))
	elseif cfg.left_cardfix_to_right == 0 then
		cardfix = step(ctx, cardfix, at(b.add_race, race) + at(b.add_race, C.RC_ALL))
		cardfix = step(ctx, cardfix, at(b.add_ele, ele) + at(b.add_ele, C.ELE_ALL))
		cardfix = step(ctx, cardfix, at(b.add_size, size) + at(b.add_size, C.SZ_ALL))
		cardfix = step(ctx, cardfix, at(b.add_class, class) + at(b.add_class, C.CLASS_ALL))
		if hand == "l" then
			cardfix_ = step(ctx, cardfix_, at(b.ladd_race, race) + at(b.ladd_race, C.RC_ALL))
			cardfix_ = step(ctx, cardfix_, at(b.ladd_ele, ele) + at(b.ladd_ele, C.ELE_ALL))
			cardfix_ = step(ctx, cardfix_, at(b.ladd_size, size) + at(b.ladd_size, C.SZ_ALL))
			cardfix_ = step(ctx, cardfix_, at(b.ladd_class, class) + at(b.ladd_class, C.CLASS_ALL))
		end
	else -- both hands as one: element, race, size, class (note: not the order of the separate branch)
		cardfix = step(ctx, cardfix, at(b.add_ele, ele) + at(b.ladd_ele, ele) + at(b.add_ele, C.ELE_ALL) + at(b.ladd_ele, C.ELE_ALL))
		cardfix = step(ctx, cardfix, at(b.add_race, race) + at(b.ladd_race, race) + at(b.add_race, C.RC_ALL) + at(b.ladd_race, C.RC_ALL))
		cardfix = step(ctx, cardfix, at(b.add_size, size) + at(b.ladd_size, size) + at(b.add_size, C.SZ_ALL) + at(b.ladd_size, C.SZ_ALL))
		cardfix = step(ctx, cardfix, at(b.add_class, class) + at(b.ladd_class, class) + at(b.add_class, C.CLASS_ALL) + at(b.ladd_class, C.CLASS_ALL))
	end
	if bit_set(flag, C.BF_SHORT) then cardfix = step(ctx, cardfix, b.short_atk or 0) end
	if bit_set(flag, C.BF_LONG) then cardfix = step(ctx, cardfix, b.long_atk or 0) end
	return apply_cardfix(ctx, damage, hand == "l" and cardfix_ or cardfix)
end

-- target side, one hand: rele / lele are the attacker's weapon elements; the left hand adds its own element once
function M.cardfix_target(ctx, src, tgt, damage, hand, flag, rele, lele)
	if damage == 0 or tgt.kind ~= "pc" then return 0 end
	local t = bonus_of(tgt)
	local cardfix = 1000
	cardfix = step(ctx, cardfix, -(at(t.sub_ele, rele) + at(t.sub_ele, C.ELE_ALL)))
	if hand == "l" and lele ~= rele then
		cardfix = step(ctx, cardfix, -(at(t.sub_ele, lele) + at(t.sub_ele, C.ELE_ALL)))
	end
	cardfix = step(ctx, cardfix, -(at(t.sub_defele, src.element) + at(t.sub_defele, C.ELE_ALL)))
	cardfix = step(ctx, cardfix, -(at(t.sub_size, src.size) + at(t.sub_size, C.SZ_ALL)))
	cardfix = step(ctx, cardfix, -(at(t.sub_race, src.race) + at(t.sub_race, C.RC_ALL)))
	local sclass = src.boss and 1 or 0
	cardfix = step(ctx, cardfix, -(at(t.sub_class, sclass) + at(t.sub_class, C.CLASS_ALL)))
	if bit_set(flag, C.BF_SHORT) then cardfix = step(ctx, cardfix, -(t.near_def or 0))
	else cardfix = step(ctx, cardfix, -(t.long_def or 0)) end
	return apply_cardfix(ctx, damage, cardfix)
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-039 / 040 / 041: target modifiers (battle_calc_damage) and map-mode rates (gvg)
-- --------------------------------------------------------------------------------------------------------------------
function M.target_modifiers(ctx, cfg, tgt, damage, flag, vs_map)
	if damage == 0 then return 0 end
	local sc = tgt.sc
	if sc then
		if sc.soundofdestruction then damage = damage * 2 end
		if sc.assumptio then
			if vs_map then damage = ctx.mul_div(damage, 2, 3) else damage = ctx.idiv(damage, 2) end
		end
		if sc.shadow_clock then damage = ctx.mul_div(damage, 85, 100) end
		local long = bit_set(flag, C.BF_LONG)
		if sc.defender and long then damage = damage - ctx.mul_div(damage, sc.defender.val2, 100) end
		if sc.adjustment and long then damage = damage - ctx.mul_div(damage, 20, 100) end
		if sc.smokepowder then
			if bit_set(flag, C.BF_SHORT) then damage = damage - ctx.mul_div(damage, 15, 100)
			elseif long then damage = damage - ctx.mul_div(damage, 50, 100) end
		end
		if sc.water_barrier then damage = ctx.mul_div(damage, 80, 100) end
		if damage == 0 then return 0 end
	end
	return damage
end

function M.gvg_damage(ctx, cfg, damage, flag)
	if damage == 0 then return 0 end
	if bit_set(flag, C.BF_SHORT) then damage = ctx.mul_div(damage, cfg.gvg_short_damage_rate, 100) end
	if bit_set(flag, C.BF_LONG) then damage = ctx.mul_div(damage, cfg.gvg_long_damage_rate, 100) end
	if damage < 1 then damage = 1 end
	return damage
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-037: dual wield. Assassin line: right 50+10*AS_RIGHT, left 30+10*AS_LEFT; Kagerou/Oboro: 70+10*KO_RIGHT,
-- 50+10*KO_LEFT. Other classes keep both hands unscaled. Each hand that dealt damage is raised to at least 1.
-- --------------------------------------------------------------------------------------------------------------------
function M.dual_wield(ctx, src, d1, d2)
	local sk = src.skills or ALL
	if d1 ~= 0 then
		if src.job == "thief" then d1 = ctx.mul_div(d1, 50 + (sk.as_right or 0) * 10, 100)
		elseif src.job == "kagerou" then d1 = ctx.mul_div(d1, 70 + (sk.ko_right or 0) * 10, 100) end
		if d1 < 1 then d1 = 1 end
	end
	if d2 ~= 0 then
		if src.job == "thief" then d2 = ctx.mul_div(d2, 30 + (sk.as_left or 0) * 10, 100)
		elseif src.job == "kagerou" then d2 = ctx.mul_div(d2, 50 + (sk.ko_left or 0) * 10, 100) end
		if d2 < 1 then d2 = 1 end
	end
	return d1, d2
end

-- BR-RULESET-001 tail (battle_calc_attack): total below 1 is a miss (ATK_DEF becomes ATK_MISS), players report one hit;
-- any positive total is a connected attack. Returns dmg_lv, div.
function M.finish(is_player, d1, d2, dmg_lv, div)
	if d1 + d2 < 1 then
		if dmg_lv == C.ATK_DEF then dmg_lv = C.ATK_MISS end
		if is_player then div = 1 end
	else
		dmg_lv = C.ATK_DEF
	end
	return dmg_lv, div
end

-- --------------------------------------------------------------------------------------------------------------------
-- the whole attack: battle_calc_attack(BF_WEAPON, src, tgt, 0, 0, 0)
-- opts.trace: table that receives { name, damage, damage2 } stage probes; opts.map: "normal" | "pvp" | "gvg"
-- result: { damage, damage2, type, div, flag, dmg_lv }
-- --------------------------------------------------------------------------------------------------------------------
function M.calc_attack(ctx, cfg, src, tgt, opts)
	opts = opts or ALL
	validate(src, tgt)
	local S = {
		ctx = ctx, cfg = cfg, src = src, tgt = tgt, trace = opts.trace,
		dmg = ctx.acc(0), dmg2 = ctx.acc(0),
		type = C.DMG_NORMAL, div = 1, dmg_lv = C.ATK_DEF, crit = false,
	}
	local map = opts.map or "normal"
	-- BR-RULESET-002: init. A plain attack is BF_WEAPON|BF_NORMAL, long when it fires an arrow
	S.flag = C.BF_WEAPON + C.BF_NORMAL + ((src.kind == "pc" and src.arrow) and C.BF_LONG or C.BF_SHORT)
	S.left = left_handed(src)
	S.rele, S.lele = M.weapon_element(src, "r"), M.weapon_element(src, "l")

	local function snap(name) probe(S, name, S.dmg:get(), S.dmg2:get()) end

	local lucky = M.lucky_dodge(ctx, tgt)
	if lucky then
		S.type, S.dmg_lv = C.DMG_LUCY_DODGE, C.ATK_LUCKY
	else
		local crit = M.crit_roll(ctx, src, tgt, S)
		if crit then S.type, S.crit = C.DMG_CRITICAL, true end
		if not M.hit_roll(ctx, cfg, src, tgt, crit, S) then
			S.dmg_lv = C.ATK_FLEE
		else
			-- ---- base_attack (BR-RULESET-008, 014)
			local nosize = sc_of(src, "weaponperfection") ~= nil
			local arrow = src.kind == "pc" and src.arrow == true
			S.dmg:set(M.base_damage(ctx, src, "r", tgt.size, S.crit, arrow, nosize, S))
			if S.left then
				S.dmg2:set(M.base_damage(ctx, src, "l", tgt.size, S.crit, arrow, nosize, S))
			end
			local rate = M.base_rate_pct(src, tgt, S.crit)
			if rate ~= 0 then atk_add_rate(S, rate) end
			if S.crit and tgt.kind == "pc" and (bonus_of(tgt).crit_def_rate or 0) ~= 0 then
				atk_add_rate(S, -bonus_of(tgt).crit_def_rate)
			end
			snap("base")
			-- ---- skill_ratio, const_add (BR-RULESET-015; no constant for a plain attack), skill_atk_bonus (none)
			atk_rate(S, M.skill_ratio(src))
			snap("ratio")
			-- ---- status ATK% after the ratio (BR-RULESET-016)
			atk_rate(S, M.atk_percent(src))
			snap("sc_bonus")
			-- ---- defense, post_defense
			if S.dmg:get() + S.dmg2:get() ~= 0 then
				defense_stage(S)
				snap("defred")
				post_defense_stage(S)
			end
		end
	end
	if not lucky then
		snap("defense")
		-- ---- element, element_bonus
		element_stage(S)
		snap("element")
		snap("pre_card") -- flat additions of skills (Weaponry Research &co) are 0 without skills
		-- ---- card_attacker (BR-RULESET-020)
		if src.kind == "pc" then
			S.dmg:add(M.cardfix_attacker(ctx, cfg, src, tgt, S.dmg:get(), "r", S.flag))
			if S.left then S.dmg2:add(M.cardfix_attacker(ctx, cfg, src, tgt, S.dmg2:get(), "l", S.flag)) end
		end
		snap("card_atk")
		-- ---- card_target (BR-RULESET-021)
		if tgt.kind == "pc" then
			S.dmg:add(M.cardfix_target(ctx, src, tgt, S.dmg:get(), "r", S.flag, S.rele, S.lele))
			if S.left then S.dmg2:add(M.cardfix_target(ctx, src, tgt, S.dmg2:get(), "l", S.flag, S.rele, S.lele)) end
		end
		snap("card_tgt")
		-- ---- div_hands: div is 1 for a plain attack (BR-RULESET-036 leaves damage unchanged), then dual wield
		snap("div")
		if src.kind == "pc" and S.left then
			local d1, d2 = M.dual_wield(ctx, src, S.dmg:get(), S.dmg2:get())
			S.dmg:set(d1)
			S.dmg2:set(d2)
		end
		if not S.left and S.dmg2:get() ~= 0 then S.dmg2:set(0) end
		snap("hands")
		-- ---- target_modifiers, map_rates (battle_calc_attack_gvg_bg)
		if S.dmg:get() + S.dmg2:get() ~= 0 then
			local vs, gvg = map ~= "normal", map == "gvg"
			local d1, d2 = S.dmg:get(), S.dmg2:get()
			if d2 == 0 then
				d1 = M.target_modifiers(ctx, cfg, tgt, d1, S.flag, vs)
				if gvg then d1 = M.gvg_damage(ctx, cfg, d1, S.flag) end
			elseif d1 == 0 then
				d2 = M.target_modifiers(ctx, cfg, tgt, d2, S.flag, vs)
				if gvg then d2 = M.gvg_damage(ctx, cfg, d2, S.flag) end
			else
				d1 = M.target_modifiers(ctx, cfg, tgt, d1, S.flag, vs)
				d2 = M.target_modifiers(ctx, cfg, tgt, d2, S.flag, vs)
				if gvg then
					d1 = M.gvg_damage(ctx, cfg, d1, S.flag)
					d2 = M.gvg_damage(ctx, cfg, d2, S.flag)
				end
				if d1 > 1 and d2 < 1 then d2 = 1 end
			end
			S.dmg:set(d1)
			S.dmg2:set(d2)
		end
		snap("map")
		snap("final")
	end
	-- ---- BR-RULESET-001: total below 1 is a miss; players report a single hit
	local d1, d2 = S.dmg:get(), S.dmg2:get()
	S.dmg_lv, S.div = M.finish(src.kind == "pc", d1, d2, S.dmg_lv, S.div)
	return { damage = d1, damage2 = d2, type = S.type, div = S.div, flag = S.flag, dmg_lv = S.dmg_lv }
end

return M
