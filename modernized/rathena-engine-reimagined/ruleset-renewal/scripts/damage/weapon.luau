--!nonstrict
-- Renewal basic weapon attack (no skill): BR-RULESET-001..003, 005, 006, 009, 010, 011, 014..016, 019..021, 025, 027..031, 033,
-- 034, 036, 037, 039..041 in the order of the legacy battle_calc_weapon_attack (battle.cpp:5418-5822, #ifdef RENEWAL branches).
--
-- Stage order (see pipeline.ORDER.weapon; the golden stage probes fix it, tests/acceptance/weapon_golden_test checks it):
--   init, base_attack, card_attacker, card_target_parts, sc_bonus, sum, p_atk, mastery, crit_rate, skill_ratio, const_add,
--   adv_katar_mastery, res, defense, post_defense, skill_atk_bonus, element, crit_multiplier, flat_additions, card_target,
--   div_hands, target_modifiers, map_rates, final
--
-- The stage bodies live in M.STAGES (one function per stage name, operating on the attack state S). M.register(engine) hands them
-- to the engine with pipeline.stage(); M.calc_attack builds S and calls pipeline.run("weapon", S, ...), which executes the
-- EFFECTIVE order (this package's order plus whatever other mods inserted, replaced, cancelled or moved). Prelude of every attack
-- (BR-RULESET-003/005/006, shared with the pre-renewal ruleset in structure): lucky dodge, crit roll, hit roll. Stages in
-- HIT_ONLY are skipped when the attack does not hit.
--
-- legacy_quirks (cfg.legacy_quirks, default true; the host's engine.config.legacy_quirks from mod.toml [ruleset]): two legacy
-- defects are reproduced exactly while it is true (goldens). With false the corrected behaviour applies:
--   * a monster's critical hit multiplies ONCE by 1.4f (crit_multiplier stage, as for players); legacy also multiplies the base
--     damage by 14/10 in battle_calc_base_damage (battle.cpp:2635), so a monster crit is x1.96
--   * weapon_damage_rate is read from the slot of the weapon's own type; legacy indexes a 16 slot array by weapon types up to 23
--     (pc.hpp:577), so guns and grenade launchers read arrow_addele[type - 16] instead
-- Everything else is identical in both modes.
--
-- ctx interface (identical to the pre-renewal package; the host needs nothing new):
--   ctx.rng.rnd()           -> integer in [0, 2^31-1]   legacy rnd()                       (one draw)
--   ctx.rng.value(lo, hi)   -> integer in [lo, hi]      legacy rnd_value(lo, hi)           (one draw; lo <= hi)
--   ctx.idiv(a, b)          -> int64 a / b truncated toward zero (C++ semantics; b ~= 0)
--   ctx.mul_div(v, m, d)    -> int64 v * m / d, the product kept in int64, then truncated toward zero
--   ctx.acc(v)              -> int64 accumulator (the damage "lane"): get set add mul_pct add_pct clamp_min
-- Luau `//` and `/` are never used on signed values (they floor); `%` is only applied to non-negative RNG output and to wrap-arounds
-- of narrowing conversions. Floating point steps go through scripts/damage/fp.luau.
--
-- Unit snapshot (BR-RULESET-117), plain tables; the fields beyond the pre-renewal set are marked RE:
--   kind "pc"|"mob"; level; str agi vit int dex luk; batk hit cri flee flee2 def def2 (soft DEF); size race element ele_lv boss targeted
--   RE: patk crate eatk res mres damagetaken (mob_db DamageTaken, default 100)
--   rhw / lhw = { atk, atk2, ele, wlv, range }   wlv > 0 means a weapon is equipped in that hand (pc); mobs use atk..atk2;
--   RE: range > 3 makes a monster's basic attack ranged; shield (bool: a non-weapon item in the left hand; its weapon roll still draws)
--   wtype1, wtype2, arrow (bool), job ("thief"|"kagerou"), no_sizefix, RE: ammo (0 none, 1 arrow, 2 cannonball)
--   skills = { as_right, as_left, ko_right, ko_left, RE: vulture, weapon_research, sm_sword, sm_twohand, mace, spear, axe, demonbane }
--   bonus = { arrow_atk, crit_atk_rate, non_crit_atk_rate, short_atk, long_atk, near_def, long_def, crit_def, crit_def_rate,
--             overrefine_r, overrefine_l, perfect_hit, arrow_hit, arrow_cri, crit_range, skill_ratio,
--             RE: atk_rate, wdr_r, wdr_l, arrow_ele, ignore_res (race map),
--             add_race/add_ele/add_size/add_class, ladd_*, aadd_*, sub_race/sub_ele/sub_size/sub_class/sub_defele, critadd_race }
--   sc = { berserk curse provoke concentration edp maximize weaponperfection overthrust deluge volcano violentgale enchantarms
--          assumptio soundofdestruction shadow_clock defender adjustment smokepowder water_barrier
--          RE: a_telum potent_venom kaite raid steelbody groundgravity }  each { val1..val4 }
local C = import("scripts/damage/consts")
local TABLES = import("data/damage_tables")
local fp = import("scripts/damage/fp")
local pipeline = import("scripts/pipeline")

local run = nil -- engine.pipeline.run, bound by M.register

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
	for _, u in ipairs({ src, tgt }) do -- status_data stores these in int16 fields: out-of-range input would be silently narrowed by the host
		for _, k in ipairs({ "eatk", "patk", "crate", "res", "mres", "hit", "flee", "cri", "flee2", "def", "def2" }) do
			assert(u[k] >= -32768 and u[k] <= 32767, "field " .. k .. " outside int16")
		end
	end
	if src.kind == "mob" then
		assert(not src.lhw or (src.lhw.atk or 0) == 0, "mob left hand not covered")
	end
	if src.kind == "pc" then
		assert(not (src.wtype1 == C.W_FIST and (src.wtype2 or 0) ~= C.W_FIST), "left-hand-only fist not covered")
		assert(src.wtype1 ~= C.W_KATAR, "katar not covered")
		if src.arrow then assert(src.wtype1 == C.W_BOW or src.wtype1 >= C.W_REVOLVER and src.wtype1 <= C.W_GRENADE, "arrow attacks are covered for bows and guns only") end
	end
end

-- --------------------------------------------------------------------------------------------------------------------
-- hands (battle.cpp:2960-3002, skill_id == 0)
-- --------------------------------------------------------------------------------------------------------------------
local function left_handed(src)
	if src.kind == "pc" and src.wtype1 == C.W_FIST and (src.wtype2 or 0) ~= C.W_FIST then return true end
	return src.lhw ~= nil and (src.lhw.atk or 0) ~= 0
end

-- is_skill_using_arrow (battle.cpp:2929): a player firing arrows, or a monster whose weapon range exceeds 3
local function uses_arrow(src)
	if src.kind == "pc" then return src.arrow == true end
	return src.rhw.range > 3
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

-- battle_min_damage(wd, src, skill 0, min): right hand capped at `min`, left hand at 0
local function min_damage(S, min)
	S.dmg:clamp_min(min)
	if S.left then S.dmg2:clamp_min(0) end
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-029: weapon element of a hand (skill 0 -> the weapon's element; arrow element; an endow status overrides it)
-- --------------------------------------------------------------------------------------------------------------------
function M.weapon_element(src, hand)
	local e
	if hand == "l" then e = src.lhw and src.lhw.ele or 0 else e = src.rhw.ele end
	local b = bonus_of(src)
	if src.kind == "pc" and uses_arrow(src) and (b.arrow_ele or 0) ~= 0 and hand ~= "l" then e = b.arrow_ele end
	local endow = sc_of(src, "enchantarms")
	if endow then e = endow.val1 end
	return e
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-003 / 005 / 006: lucky dodge, crit chance and roll, hit chance and roll (battle.cpp:5454, 3021, 3234)
-- --------------------------------------------------------------------------------------------------------------------
function M.lucky_dodge(ctx, tgt)
	return tgt.flee2 ~= 0 and ctx.rng.rnd() % 1000 < tgt.flee2
end

function M.crit_roll(ctx, src, tgt, S)
	if src.cri == 0 then return false, nil end -- `if (sstatus->cri)`: no draw at all
	local b = bonus_of(src)
	local cri = src.cri
	if src.kind == "pc" then
		cri = cri + at(b.critadd_race, tgt.race) + at(b.critadd_race, C.RC_ALL)
		if uses_arrow(src) then cri = cri + (b.arrow_cri or 0) + (b.crit_range or 0) end
	end
	local luk_mult = (src.kind ~= "pc" and tgt.kind == "pc") and 3 or 2
	cri = wrap16(cri - tgt.luk * luk_mult) -- raw value, stored in an int16 as legacy
	local cd = tgt.kind == "pc" and (bonus_of(tgt).crit_def or 0) or 0
	if cd ~= 0 then cri = ctx.mul_div(cri, 100 - cd, 100) end
	if S then probe(S, "cri", cri) end
	return ctx.rng.rnd() % 1000 < cri, cri -- drawn even when cri <= 0
end

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
	local hitrate = 0 + src.hit - flee -- RE: the base hit rate is 0 (80 in pre-renewal)
	if src.kind == "pc" and uses_arrow(src) then hitrate = hitrate + (b.arrow_hit or 0) end
	local sk = src.skills or ALL
	if src.kind == "pc" then
		hitrate = hitrate + (sk.vulture or 0) -- RE: Vulture's Eye is a hidden hit bonus
		local wr = sk.weapon_research or 0
		if wr > 0 then hitrate = hitrate + ctx.mul_div(hitrate, 2 * wr, 100) end
	end
	if hitrate < cfg.min_hitrate then hitrate = cfg.min_hitrate end
	if hitrate > cfg.max_hitrate then hitrate = cfg.max_hitrate end
	if S then probe(S, "hitrate", hitrate, flee) end
	return ctx.rng.rnd() % 100 < hitrate, hitrate -- drawn even when hitrate <= 0
end

-- --------------------------------------------------------------------------------------------------------------------
-- RE attribute fix (battle_attr_fix, battle.cpp:515): statuses add to the RATIO, and the result is damage - damage*(100-ratio)/100
-- flag1: allow negative ratios to survive attr_recover == 0 (the element stage passes 1, the ATK parts pass 0)
-- --------------------------------------------------------------------------------------------------------------------
function M.attr_fix(ctx, cfg, src, tgt, damage, atk_elem, flag1)
	local ratio = TABLES.element_pct[tgt.ele_lv][atk_elem][tgt.element]
	if atk_elem == 3 then ratio = ratio + ((sc_of(src, "volcano") or ALL).val3 or 0)
	elseif atk_elem == 4 then ratio = ratio + ((sc_of(src, "violentgale") or ALL).val3 or 0)
	elseif atk_elem == 1 then ratio = ratio + ((sc_of(src, "deluge") or ALL).val3 or 0) end
	if cfg.attr_recover == 0 and not flag1 and ratio < 0 then ratio = 0 end
	-- in renewal reductions are always rounded down so damage can never reach 0 unless the ratio is 0
	return damage - ctx.mul_div(damage, 100 - ratio, 100)
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-010: renewal weapon ATK of one hand (battle_calc_base_weapon_attack, battle.cpp:2447): float32 variance and base
-- stat bonus, a uint16 range, one rnd_value draw (even for a zero-width range), then over-refine, weapon_damage_rate, size fix.
-- --------------------------------------------------------------------------------------------------------------------
local DEX_WEAPONS = { [C.W_BOW] = true, [C.W_MUSICAL] = true, [C.W_WHIP] = true, [C.W_REVOLVER] = true, [C.W_RIFLE] = true,
	[C.W_GATLING] = true, [C.W_SHOTGUN] = true, [C.W_GRENADE] = true }

-- weapon ATK range [min, max] as the legacy float32 arithmetic produces it (uint16 narrowing included)
function M.weapon_atk_range(watk, atk, wlv, base_stat)
	local variance = fp.div(fp.mul(fp.mul(5.0, atk), wlv), 100.0)
	local bonus = fp.div(atk * base_stat, 200.0) -- `wa->atk * base_stat` is an int product, then / 200.0f
	local lo = fp.trunc(fp.add(fp.sub(watk, variance), bonus))
	local hi = fp.trunc(fp.add(fp.add(watk, variance), bonus))
	if lo < 0 then lo = 0 end
	lo = lo % 65536 -- uint16 atkmin = max(0, (int32)...)
	if hi > 65535 then hi = 65535 end -- min(UINT16_MAX, ...)
	return lo, hi
end

-- legacy_quirks of an attack: S is nil for the pure helpers called without one (they keep the legacy behaviour)
local function legacy_quirks(S)
	return S == nil or S.cfg == nil or S.cfg.legacy_quirks ~= false
end

-- weapon_damage_rate lookup of battle_add_weapon_damage; legacy defect: the array has 16 slots but weapon types go up to 23,
-- so types 16..23 read the following member, indexed_bonus.arrow_addele[type - 16]. With legacy_quirks off every type reads
-- the slot of its own type (the snapshot's wdr_r / wdr_l).
local function weapon_damage_rate(src, wtype, hand, legacy)
	local b = bonus_of(src)
	if legacy and wtype >= C.WDR_SLOTS then return at(b.aadd_ele, wtype - C.WDR_SLOTS) end
	if hand == "l" then
		if wtype == src.wtype1 then return b.wdr_r or 0 end -- one slot per weapon type: both hands share it
		return b.wdr_l or 0
	end
	return b.wdr_r or 0
end

function M.weapon_atk_part(ctx, src, tgt, hand, crit, S)
	local b = bonus_of(src)
	local wa = hand == "l" and (src.lhw or { atk = 0, atk2 = 0, wlv = 0 }) or src.rhw
	local watk = hand == "l" and src.watk2 or src.watk
	local wtype = hand == "l" and (src.wtype2 or 0) or src.wtype1
	local equipped
	if hand == "l" then equipped = (src.lhw ~= nil and src.lhw.wlv > 0) or src.shield else equipped = src.rhw.wlv > 0 end
	local atkmin, atkmax = watk, watk
	local damage = watk
	if equipped then
		local base_stat = src.str
		if DEX_WEAPONS[src.wtype1] then base_stat = src.dex end -- (Soul Attack, the STR exception, is a summoner skill: not covered)
		atkmin, atkmax = M.weapon_atk_range(watk, wa.atk, wa.wlv, base_stat)
		if S then probe(S, "watk_mm", atkmin, atkmax) end
		if sc_of(src, "maximize") or crit then
			damage = atkmax
		else
			local lo, hi = atkmin, atkmax
			if lo > hi then lo, hi = hi, lo end -- rnd_value swaps
			damage = ctx.rng.value(lo, hi) -- drawn even when lo == hi
		end
	end
	-- battle_add_weapon_damage: over-refine rnd, then weapon_damage_rate
	local over = hand == "l" and (b.overrefine_l or 0) or (b.overrefine_r or 0)
	if over ~= 0 then damage = damage + ctx.rng.rnd() % over + 1 end
	local wdr = weapon_damage_rate(src, wtype, hand, legacy_quirks(S))
	if wdr ~= 0 then damage = damage + ctx.mul_div(damage, wdr, 100) end
	-- battle_calc_sizefix (BR-RULESET-102): not for Weapon Perfection / no_sizefix; the table of THIS hand's weapon type
	if not (src.no_sizefix or sc_of(src, "weaponperfection")) then
		damage = ctx.mul_div(damage, TABLES.size_pct[wtype][tgt.size + 1], 100)
	end
	return damage
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-008 (non-players): monster base damage, renewal flavour (battle_calc_base_damage, battle.cpp:2519): the draw is
-- rnd() % (max - min); a crit takes max; `+ batk`; RE multiplies a critical result by 14/10 right here
-- --------------------------------------------------------------------------------------------------------------------
function M.mob_base_damage(ctx, src, hand, crit, S)
	local wa = hand == "l" and src.lhw or src.rhw
	local atkmin, atkmax = wa.atk, wa.atk2
	if atkmin > atkmax then atkmin = atkmax end
	if S then probe(S, "basemm", atkmin, atkmax) end
	if sc_of(src, "maximize") then atkmin = atkmax end
	local damage
	if not crit then
		damage = atkmin
		if atkmax > atkmin then damage = damage + ctx.rng.rnd() % (atkmax - atkmin) end -- never returns atkmax (Q2)
	else
		damage = atkmax
	end
	damage = damage + src.batk
	-- legacy defect (battle.cpp:2635): the base damage of a critical hit is multiplied by 14/10 here, and the crit_multiplier stage
	-- multiplies by 1.4f again; with legacy_quirks off only that stage applies
	if crit and legacy_quirks(S) then damage = ctx.mul_div(damage, 14, 10) end
	return damage
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-009 / 024: mastery ATK of one hand (battle_addmastery, RE: starts at 0): element-free passive bonuses by weapon type
-- --------------------------------------------------------------------------------------------------------------------
function M.mastery_atk(ctx, cfg, src, tgt, hand)
	local sk = src.skills or ALL
	local wtype = hand == "l" and (src.wtype2 or 0) or src.wtype1
	local v = 0
	local lv = sk.demonbane or 0
	if lv > 0 and tgt.kind == "mob" then
		local undead = tgt.element == C.ELE_UNDEAD
		if cfg.undead_detect_type ~= 0 then undead = false end -- (race-based detection not covered; asserted below)
		if undead or tgt.race == C.RC_DEMON then
			v = v + fp.trunc(lv * (fp.ddiv(src.level, 20.0) + 3.0)) -- skill * (base_level / 20.0 + 3.0), in double
		end
	end
	v = v + 2 * (sk.weapon_research or 0) -- RE: applies to every weapon
	if wtype == C.W_1HSWORD then
		v = v + 3 * (sk.axe or 0) -- RE: Axe Mastery counts for one-hand swords, then falls through to the dagger case
		v = v + 4 * (sk.sm_sword or 0)
	elseif wtype == C.W_DAGGER then
		v = v + 4 * (sk.sm_sword or 0)
	elseif wtype == C.W_2HSWORD then
		v = v + 4 * (sk.sm_twohand or 0)
	elseif wtype == C.W_1HSPEAR or wtype == C.W_2HSPEAR then
		v = v + 4 * (sk.spear or 0) -- (riding raises it to 5/lv: not covered)
	elseif wtype == C.W_1HAXE or wtype == C.W_2HAXE then
		v = v + 3 * (sk.axe or 0)
	elseif wtype == C.W_MACE or wtype == C.W_2HMACE then
		v = v + 3 * (sk.mace or 0)
	end
	return v
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

-- attacker side, one hand. RE differences from the pre-renewal function: no Adv. Katar Mastery step and no short/long attack rate
-- steps (those are percent additions on the summed ATK in RE: stage crit_rate)
function M.cardfix_attacker(ctx, cfg, src, tgt, damage, hand)
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
	return apply_cardfix(ctx, damage, hand == "l" and cardfix_ or cardfix)
end

-- target side, one hand. ignore_ele: the NK_IGNOREELEMENT variant the ATK parts use for status ATK and mastery ATK (no element step)
function M.cardfix_target(ctx, src, tgt, damage, hand, flag, rele, lele, ignore_ele)
	if damage == 0 or tgt.kind ~= "pc" then return 0 end
	local t = bonus_of(tgt)
	local cardfix = 1000
	if not ignore_ele then
		cardfix = step(ctx, cardfix, -(at(t.sub_ele, rele) + at(t.sub_ele, C.ELE_ALL)))
		if hand == "l" and lele ~= rele then
			cardfix = step(ctx, cardfix, -(at(t.sub_ele, lele) + at(t.sub_ele, C.ELE_ALL)))
		end
		cardfix = step(ctx, cardfix, -(at(t.sub_defele, src.element) + at(t.sub_defele, C.ELE_ALL)))
	end
	cardfix = step(ctx, cardfix, -(at(t.sub_size, src.size) + at(t.sub_size, C.SZ_ALL)))
	cardfix = step(ctx, cardfix, -(at(t.sub_race, src.race) + at(t.sub_race, C.RC_ALL)))
	local sclass = src.boss and 1 or 0
	cardfix = step(ctx, cardfix, -(at(t.sub_class, sclass) + at(t.sub_class, C.CLASS_ALL)))
	if bit_set(flag, C.BF_SHORT) then cardfix = step(ctx, cardfix, -(t.near_def or 0))
	else cardfix = step(ctx, cardfix, -(t.long_def or 0)) end
	return apply_cardfix(ctx, damage, cardfix)
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-016 / 015: ATK% of statuses SEEDS the skill ratio in RE (battle_get_atkpercent, battle.cpp:4361)
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

-- skill ratio of a plain attack: ATK% + bonus.skill_ratio (players) + Overthrust + Berserk (+200 in RE)
function M.skill_ratio(src)
	local r = M.atk_percent(src)
	if src.kind == "pc" then r = r + (bonus_of(src).skill_ratio or 0) end
	local v = sc_of(src, "overthrust")
	if v then r = r + v.val3 end
	if sc_of(src, "berserk") then r = r + 200 end
	return r
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-025: RES (battle.cpp:5611): reduction = res / (res + 400) * 80 %, the attacker's ignore-RES percent is capped
-- --------------------------------------------------------------------------------------------------------------------
function M.res_reduction(damage, res, ignore_pct, cap)
	if ignore_pct > cap then ignore_pct = cap end
	res = wrap16(fp.trunc(res - fp.ddiv(ignore_pct * res, 100.0))) -- (int16)(res - ignore_res * res / 100.0)
	local q = fp.f32(fp.ddiv(res, res + 400.0)) -- static_cast<float>(res / (res + 400.0))
	local cut = fp.trunc(fp.ddiv(q * 80.0, 100.0) * damage) -- (int64)(q * 80.0 / 100.0 * (double)damage)
	return damage - cut
end

local function res_stage_value(S, damage)
	local src, tgt, cfg = S.src, S.tgt, S.cfg
	local b = bonus_of(src)
	local ignore = 0
	if src.kind == "pc" then ignore = at(b.ignore_res, tgt.race) + at(b.ignore_res, C.RC_ALL) end
	local tel = sc_of(src, "a_telum")
	if tel then ignore = ignore + tel.val2 end
	local ven = sc_of(src, "potent_venom")
	if ven then ignore = ignore + ven.val2 end
	return M.res_reduction(damage, tgt.res, ignore, cfg.max_res_mres_ignored)
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-027: DEF (battle_calc_defense_reduction, battle.cpp:4726). RE: damage * (4000 + DEF) / (4000 + 10 * DEF) - softDEF,
-- no RNG at all (the soft DEF of both player and monster targets is def2), a crit does not ignore DEF
-- --------------------------------------------------------------------------------------------------------------------
function M.defense_values(ctx, cfg, tgt)
	local def1 = tgt.def
	if def1 < C.DEF_MIN then def1 = C.DEF_MIN end
	if def1 > C.DEF_MAX then def1 = C.DEF_MAX end
	local def2 = tgt.def2
	if cfg.vit_penalty_type ~= 0 and bit_set(cfg.vit_penalty_target, bl_type(tgt)) then
		local cap = ctx.idiv(100, cfg.vit_penalty_num) + (cfg.vit_penalty_count - 1)
		local n = tgt.targeted or 0
		if n > cap then n = cap end
		if n >= cfg.vit_penalty_count then
			if not sc_of(tgt, "steelbody") then
				def1 = wrap16(M.crowd_penalty(ctx, def1, n, cfg.vit_penalty_type, cfg.vit_penalty_count, cfg.vit_penalty_num))
			end
			def2 = wrap16(M.crowd_penalty(ctx, def2, n, cfg.vit_penalty_type, cfg.vit_penalty_count, cfg.vit_penalty_num))
		end
		if def2 < 1 then def2 = 1 end -- applies to every penalised target type, even below the crowd threshold
	end
	local vit = def2 -- RE: soft DEF = def2 for players and monsters alike
	return vit, def1, def2
end

function M.def_reduce(ctx, damage, def1, vit, weapon_defense_type)
	if weapon_defense_type ~= 0 then
		vit = wrap16(vit + def1 * weapon_defense_type)
		def1 = 0
	end
	if def1 == -400 then def1 = -399 end -- -400 would divide by zero
	return ctx.mul_div(damage, 4000 + def1, 4000 + 10 * def1) - vit
end

-- --------------------------------------------------------------------------------------------------------------------
-- BR-RULESET-039 / 040 / 041: target modifiers (battle_calc_damage) and map-mode rates (gvg). RE differences: Assumptio is not a
-- damage reduction (it raises DEF in status_calc), Kaite x4 vs melee, Raid +30 % (+15 % vs boss), Steel Body divides by 10.
-- --------------------------------------------------------------------------------------------------------------------
function M.target_modifiers(ctx, cfg, tgt, damage, flag)
	if damage == 0 then return 0 end
	local sc = tgt.sc
	if sc then
		local short, long = bit_set(flag, C.BF_SHORT), bit_set(flag, C.BF_LONG)
		if sc.kaite and short then damage = damage * 4 end
		if sc.groundgravity then damage = damage + ctx.mul_div(damage, 10, 100) end
		if sc.raid then
			if tgt.boss then damage = damage + ctx.mul_div(damage, 15, 100) else damage = damage + ctx.mul_div(damage, 30, 100) end
		end
		if sc.soundofdestruction then damage = damage * 2 end
		if sc.shadow_clock then damage = ctx.mul_div(damage, 85, 100) end
		if sc.defender and long then damage = damage - ctx.mul_div(damage, sc.defender.val2, 100) end
		if sc.adjustment and long then damage = damage - ctx.mul_div(damage, 20, 100) end
		if sc.smokepowder then
			if short then damage = damage - ctx.mul_div(damage, 15, 100)
			elseif long then damage = damage - ctx.mul_div(damage, 50, 100) end
		end
		if sc.water_barrier then damage = ctx.mul_div(damage, 80, 100) end
		if sc.steelbody then
			if damage > 10 then damage = ctx.idiv(damage, 10) else damage = 1 end
		end
		if damage == 0 then return 0 end
	end
	if tgt.kind == "mob" and tgt.damagetaken ~= 100 then
		damage = ctx.mul_div(damage, tgt.damagetaken, 100)
		if damage < 1 then damage = 1 end
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
-- BR-RULESET-037: dual wield. Assassin line: right 50+10*AS_RIGHT, left 30+10*AS_LEFT; Kagerou/Oboro: 70+10*KO_RIGHT, 50+10*KO_LEFT.
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

-- BR-RULESET-001 tail (battle_calc_attack): total below 1 is a miss (ATK_DEF becomes ATK_MISS), players report one hit
function M.finish(is_player, d1, d2, dmg_lv, div)
	if d1 + d2 < 1 then
		if dmg_lv == C.ATK_DEF then dmg_lv = C.ATK_MISS end
		if is_player then div = 1 end
	else
		dmg_lv = C.ATK_DEF
	end
	return dmg_lv, div
end

-- ====================================================================================================================
-- STAGES: one function per stage name; S is the attack state built by calc_attack
-- ====================================================================================================================
local STAGES = {}
M.STAGES = STAGES

-- stages that only run when the attack hits (battle.cpp: the `else if(!(infdef = ...))` block)
M.HIT_ONLY = {
	base_attack = true, card_attacker = true, card_target_parts = true, sc_bonus = true, sum = true, p_atk = true, mastery = true,
	crit_rate = true, skill_ratio = true, const_add = true, adv_katar_mastery = true, res = true, defense = true,
	post_defense = true, skill_atk_bonus = true,
}

local function parts_zero()
	return { s = 0, s2 = 0, w = 0, w2 = 0, e = 0, e2 = 0, m = 0, m2 = 0, p = 0, p2 = 0 }
end

function STAGES.init(S) -- BR-RULESET-002: the prelude (lucky dodge, crit, hit) has run; flags, hands and elements were set up
end

-- BR-RULESET-009: the ATK parts of a player (battle_calc_damage_parts); monsters use the base damage path
function STAGES.base_attack(S)
	local ctx, cfg, src, tgt = S.ctx, S.cfg, S.src, S.tgt
	local P = parts_zero()
	S.parts = P
	if src.kind ~= "pc" then
		S.dmg:set(M.mob_base_damage(ctx, src, "r", S.crit, S))
		if S.left then S.dmg2:set(M.mob_base_damage(ctx, src, "l", S.crit, S)) end
	else
		-- status ATK is considered neutral on normal attacks, the right hand's is doubled after the element step
		local sa = M.attr_fix(ctx, cfg, src, tgt, src.batk, C.ELE_NEUTRAL, false)
		local sa2 = sa
		P.s = sa * 2
		P.s2 = sa2
		P.w = M.weapon_atk_part(ctx, src, tgt, "r", S.crit, S)
		P.w2 = M.weapon_atk_part(ctx, src, tgt, "l", S.crit, S) -- drawn even without a left weapon when a shield is worn
		P.w = M.attr_fix(ctx, cfg, src, tgt, P.w, S.rele, false)
		P.w2 = M.attr_fix(ctx, cfg, src, tgt, P.w2, S.lele, false)
		-- BR-RULESET-011: equip ATK = eatk (+ arrow ATK while ammo other than a cannonball is equipped)
		local eatk = src.eatk
		local b = bonus_of(src)
		if uses_arrow(src) and (src.ammo or 0) ~= 0 and src.ammo ~= 2 then eatk = eatk + (b.arrow_atk or 0) end
		P.e = M.attr_fix(ctx, cfg, src, tgt, eatk, S.rele, false)
		P.e2 = M.attr_fix(ctx, cfg, src, tgt, eatk, S.lele, false)
		-- AtkRate: a static percent of (weapon + equip) ATK
		local ar = b.atk_rate or 0
		if ar ~= 0 then
			P.p = ctx.mul_div(P.w + P.e, ar, 100)
			P.p2 = ctx.mul_div(P.w2 + P.e2, ar, 100)
		end
		-- mastery ATK has no elemental properties; the left value only exists for a left-handed attack
		P.m = M.mastery_atk(ctx, cfg, src, tgt, "r")
		if S.left then P.m2 = M.mastery_atk(ctx, cfg, src, tgt, "l") end
	end
	probe(S, "base", S.dmg:get(), S.dmg2:get())
	probe(S, "p_status", P.s, P.s2)
	probe(S, "p_weapon", P.w, P.w2)
	probe(S, "p_equip", P.e, P.e2)
	probe(S, "p_mastery", P.m, P.p)
end

-- BR-RULESET-020: attacker cards apply to weapon and equip ATK only (not status or mastery ATK)
function STAGES.card_attacker(S)
	local ctx, cfg, src, tgt, P = S.ctx, S.cfg, S.src, S.tgt, S.parts
	if src.kind ~= "pc" then return end
	P.w = P.w + M.cardfix_attacker(ctx, cfg, src, tgt, P.w, "r")
	P.e = P.e + M.cardfix_attacker(ctx, cfg, src, tgt, P.e, "r")
	if S.left then
		P.w2 = P.w2 + M.cardfix_attacker(ctx, cfg, src, tgt, P.w2, "l")
		P.e2 = P.e2 + M.cardfix_attacker(ctx, cfg, src, tgt, P.e2, "l")
	end
end

-- BR-RULESET-021 (RE): when the target is a player its cards apply to EVERY part right here (status and mastery ATK without
-- the element step)
function STAGES.card_target_parts(S)
	local ctx, src, tgt, P = S.ctx, S.src, S.tgt, S.parts
	if tgt.kind ~= "pc" then return end
	local f, r, l = S.flag, S.rele, S.lele
	P.s = P.s + M.cardfix_target(ctx, src, tgt, P.s, "r", f, r, l, true)
	P.w = P.w + M.cardfix_target(ctx, src, tgt, P.w, "r", f, r, l, false)
	P.e = P.e + M.cardfix_target(ctx, src, tgt, P.e, "r", f, r, l, false)
	P.m = P.m + M.cardfix_target(ctx, src, tgt, P.m, "r", f, r, l, true)
	if S.left then
		P.s2 = P.s2 + M.cardfix_target(ctx, src, tgt, P.s2, "l", f, r, l, true)
		P.w2 = P.w2 + M.cardfix_target(ctx, src, tgt, P.w2, "l", f, r, l, false)
		P.e2 = P.e2 + M.cardfix_target(ctx, src, tgt, P.e2, "l", f, r, l, false)
		P.m2 = P.m2 + M.cardfix_target(ctx, src, tgt, P.m2, "l", f, r, l, true)
	end
end

-- status ATK bonuses that cards do not affect (battle_attack_sc_bonus): of the statuses in the covered set only EDP acts on the
-- parts (weapon and equip ATK x (2.5 + 0.3 * level))
function STAGES.sc_bonus(S)
	local ctx, src, P = S.ctx, S.src, S.parts
	local edp = sc_of(src, "edp")
	if edp then
		local rate = 250 + edp.val1 * 30
		P.w = ctx.mul_div(P.w, rate, 100)
		P.e = ctx.mul_div(P.e, rate, 100)
		if S.left then
			P.w2 = ctx.mul_div(P.w2, rate, 100)
			P.e2 = ctx.mul_div(P.e2, rate, 100)
		end
	end
	probe(S, "c_status", P.s, P.s2)
	probe(S, "c_weapon", P.w, P.w2)
	probe(S, "c_equip", P.e, P.e2)
	probe(S, "c_mastery", P.m, P.m2)
end

-- the damage of a player is the sum of its parts; the sum stage also leaves the lanes ready for P.ATK, mastery and the rates
function STAGES.sum(S)
	local src, P = S.src, S.parts
	if src.kind == "pc" then
		S.dmg:set(P.s + P.w + P.e + P.p)
		if S.left then S.dmg2:set(P.s2 + P.w2 + P.e2 + P.p2) end
	end
end

-- P.ATK: floor((float)(damage * (100 + patk) / 100)) per lane
function STAGES.p_atk(S)
	local ctx, src = S.ctx, S.src
	if src.kind ~= "pc" then return end
	local function lane(acc)
		acc:set(fp.floor_f32_of_int(ctx.mul_div(acc:get(), 100 + src.patk, 100)))
	end
	lane(S.dmg)
	if S.left then lane(S.dmg2) end
end

function STAGES.mastery(S)
	local src, P = S.src, S.parts
	if src.kind ~= "pc" then return end
	S.dmg:add(P.m)
	if S.left then S.dmg2:add(P.m2) end
end

-- crit_atk_rate on a crit, non_crit_atk_rate otherwise, then the short / long attack rates (percent additions on the sum in RE)
local function crit_rate_pc(S)
	local ctx, src = S.ctx, S.src
	local b = bonus_of(src)
	if S.crit then
		local function lane(acc)
			acc:add(fp.floor_f32_of_int(ctx.mul_div(acc:get(), b.crit_atk_rate or 0, 100)))
		end
		lane(S.dmg)
		if S.left then lane(S.dmg2) end
	else
		atk_add_rate(S, b.non_crit_atk_rate or 0)
	end
	if bit_set(S.flag, C.BF_SHORT) then atk_add_rate(S, b.short_atk or 0) end
	if bit_set(S.flag, C.BF_LONG) then atk_add_rate(S, b.long_atk or 0) end
end

function STAGES.crit_rate(S)
	if S.src.kind == "pc" then crit_rate_pc(S) end
	probe(S, "sum", S.dmg:get(), S.dmg2:get()) -- (the legacy probe sits after the whole `if (sd)` block: sum, P.ATK, mastery, rates)
end

function STAGES.skill_ratio(S)
	atk_rate(S, M.skill_ratio(S.src))
	probe(S, "ratio", S.dmg:get(), S.dmg2:get())
end

function STAGES.const_add(S) -- BR-RULESET-017: a plain attack has no constant addition
	probe(S, "const_add", S.dmg:get(), S.dmg2:get())
end

function STAGES.adv_katar_mastery(S) -- katar is not covered; the probe marks the position
	probe(S, "katar", S.dmg:get(), S.dmg2:get())
end

-- BR-RULESET-025
function STAGES.res(S)
	local tgt = S.tgt
	if S.dmg:get() + S.dmg2:get() ~= 0 and tgt.res > 0 then
		S.dmg:set(res_stage_value(S, S.dmg:get()))
		S.dmg2:set(res_stage_value(S, S.dmg2:get()))
	end
	probe(S, "res", S.dmg:get(), S.dmg2:get())
end

function STAGES.defense(S)
	-- battle.cpp: `if (wd.damage + wd.damage2) { defense; post_defense }` -- the guard is evaluated once, before the reduction
	S.has_damage = S.dmg:get() + S.dmg2:get() ~= 0
	if not S.has_damage then return end
	local ctx, tgt = S.ctx, S.tgt
	local vit, def1, def2 = M.defense_values(ctx, S.cfg, tgt)
	probe(S, "defs", def1, def2)
	probe(S, "vitdef", vit, def1)
	local wdt = S.cfg.weapon_defense_type
	S.dmg:set(M.def_reduce(ctx, S.dmg:get(), def1, vit, wdt))
	if S.left then S.dmg2:set(M.def_reduce(ctx, S.dmg2:get(), def1, vit, wdt)) end
	probe(S, "defred", S.dmg:get(), S.dmg2:get())
end

-- RE post-defense: Aura Blade and friends are skills/statuses (not covered); the minimum of 1 (0 for the left hand) remains
function STAGES.post_defense(S)
	if not S.has_damage then return end
	min_damage(S, 1)
	probe(S, "postdef", S.dmg:get(), S.dmg2:get())
end

function STAGES.skill_atk_bonus(S) -- pc_skillatk_bonus applies to skills only
end

-- element: players apply the table per ATK part already; only non-players use it here, and basic attacks of the types in
-- attack_attr_none (monsters by default) skip it (and the attr_recover cap that follows it)
function STAGES.element(S)
	local ctx, cfg, src, tgt = S.ctx, S.cfg, S.src, S.tgt
	probe(S, "defense", S.dmg:get(), S.dmg2:get())
	probe(S, "ele", S.rele, S.lele)
	if S.dmg:get() > 0 or S.dmg2:get() > 0 then
		if bit_set(cfg.attack_attr_none, bl_type(src)) then
			probe(S, "element", S.dmg:get(), S.dmg2:get())
			return
		end
		if src.kind ~= "pc" then
			S.dmg:set(M.attr_fix(ctx, cfg, src, tgt, S.dmg:get(), S.rele, true))
			if S.left then S.dmg2:set(M.attr_fix(ctx, cfg, src, tgt, S.dmg2:get(), S.lele, true)) end
		end
	end
	probe(S, "attr", S.dmg:get(), S.dmg2:get())
	if cfg.attr_recover == 0 then min_damage(S, 0) end -- BR-RULESET-032
	probe(S, "element", S.dmg:get(), S.dmg2:get())
end

-- BR-RULESET-034: crit damage, applied AFTER the element step in RE: players x(1.4f + 0.01f * crate) in float32, others x1.4f,
-- then the target's crit_def_rate
function STAGES.crit_multiplier(S)
	local src, tgt = S.src, S.tgt
	if S.crit then
		local function mul(acc, mult)
			acc:set(math.floor(fp.mul(fp.f32(acc:get()), mult)))
		end
		if src.kind == "pc" then
			local mult = fp.add(fp.f32(1.4), fp.mul(fp.f32(0.01), src.crate))
			mul(S.dmg, mult)
			if S.left then mul(S.dmg2, mult) end
		else
			mul(S.dmg, fp.f32(1.4)) -- (with legacy_quirks the base damage of a monster crit already got x14/10: the legacy double multiplier)
		end
		local cdr = tgt.kind == "pc" and (bonus_of(tgt).crit_def_rate or 0) or 0
		if cdr ~= 0 then atk_add_rate(S, -cdr) end
	end
	probe(S, "crit", S.dmg:get(), S.dmg2:get())
end

function STAGES.flat_additions(S) -- BR-RULESET-035: skill-only additions
	probe(S, "pre_card", S.dmg:get(), S.dmg2:get())
end

-- BR-RULESET-021 (RE): target card fix on the total, only for non-player attackers (players applied it per part)
function STAGES.card_target(S)
	local ctx, src, tgt = S.ctx, S.src, S.tgt
	probe(S, "card_atk", S.dmg:get(), S.dmg2:get())
	if tgt.kind == "pc" and src.kind ~= "pc" then
		S.dmg:add(M.cardfix_target(ctx, src, tgt, S.dmg:get(), "r", S.flag, S.rele, S.lele, false))
		if S.left then S.dmg2:add(M.cardfix_target(ctx, src, tgt, S.dmg2:get(), "l", S.flag, S.rele, S.lele, false)) end
	end
	probe(S, "card_tgt", S.dmg:get(), S.dmg2:get())
end

function STAGES.div_hands(S)
	local ctx, src = S.ctx, S.src
	probe(S, "div", S.dmg:get(), S.dmg2:get()) -- div is 1 for a plain attack: DAMAGE_DIV_FIX leaves the damage unchanged
	if src.kind == "pc" and S.left then
		local d1, d2 = M.dual_wield(ctx, src, S.dmg:get(), S.dmg2:get())
		S.dmg:set(d1)
		S.dmg2:set(d2)
	end
	if not S.left and S.dmg2:get() ~= 0 then S.dmg2:set(0) end
	probe(S, "hands", S.dmg:get(), S.dmg2:get())
end

function STAGES.target_modifiers(S)
	if S.dmg:get() + S.dmg2:get() == 0 then return end
	local ctx, cfg, tgt = S.ctx, S.cfg, S.tgt
	local d1, d2 = S.dmg:get(), S.dmg2:get()
	if d2 == 0 then
		d1 = M.target_modifiers(ctx, cfg, tgt, d1, S.flag)
	elseif d1 == 0 then
		d2 = M.target_modifiers(ctx, cfg, tgt, d2, S.flag)
	else
		d1 = M.target_modifiers(ctx, cfg, tgt, d1, S.flag)
		d2 = M.target_modifiers(ctx, cfg, tgt, d2, S.flag)
	end
	S.dmg:set(d1)
	S.dmg2:set(d2)
end

function STAGES.map_rates(S)
	if S.dmg:get() + S.dmg2:get() ~= 0 and S.map == "gvg" then
		local ctx, cfg = S.ctx, S.cfg
		local d1, d2 = S.dmg:get(), S.dmg2:get()
		if d2 == 0 then
			d1 = M.gvg_damage(ctx, cfg, d1, S.flag)
		elseif d1 == 0 then
			d2 = M.gvg_damage(ctx, cfg, d2, S.flag)
		else
			d1 = M.gvg_damage(ctx, cfg, d1, S.flag)
			d2 = M.gvg_damage(ctx, cfg, d2, S.flag)
		end
		S.dmg:set(d1)
		S.dmg2:set(d2)
	end
	local d1, d2 = S.dmg:get(), S.dmg2:get()
	if d1 ~= 0 and d2 ~= 0 and d1 > 1 and d2 < 1 then S.dmg2:set(1) end -- two-hand totals: the left hand never rounds down to 0
	probe(S, "map", S.dmg:get(), S.dmg2:get())
end

function STAGES.final(S)
	probe(S, "final", S.dmg:get(), S.dmg2:get())
end

-- --------------------------------------------------------------------------------------------------------------------
-- Hands the stage bodies to the engine (pipeline.stage) and binds what the stages need from it. Called by register(engine) after
-- pipeline.define. `init` and `final` are protected: a mod needs the protected-stage permission to replace, cancel or move them.
-- --------------------------------------------------------------------------------------------------------------------
-- what register() bound (tests that register the package with another engine restore it afterwards)
function M.binding() return { run = run, num = fp.binding() } end
function M.bind(b)
	run = b.run
	if b.num then fp.bind(b.num) end
end

function M.register(engine)
	fp.bind(engine.num)
	run = engine.pipeline.run
	for _, name in ipairs(pipeline.ORDER.weapon) do
		engine.pipeline.stage("weapon", name, STAGES[name], { protected = pipeline.PROTECTED[name] == true })
	end
end

-- --------------------------------------------------------------------------------------------------------------------
-- the whole attack: battle_calc_attack(BF_WEAPON, src, tgt, 0, 0, 0)
-- opts.trace: table that receives { name, damage, damage2 } stage probes; opts.map: "normal" | "pvp" | "gvg"
-- result: { damage, damage2, type, div, flag, dmg_lv }; a mod that cancelled the attack (ctx:cancel from its stage) yields
-- zero damage, dmg_lv ATK_BLOCK and `cancelled = true`
-- --------------------------------------------------------------------------------------------------------------------
local NO_SKIP = {}

function M.calc_attack(ctx, cfg, src, tgt, opts)
	opts = opts or ALL
	validate(src, tgt)
	local S = {
		ctx = ctx, cfg = cfg, src = src, tgt = tgt, trace = opts.trace, map = opts.map or "normal",
		dmg = ctx.acc(0), dmg2 = ctx.acc(0),
		type = C.DMG_NORMAL, div = 1, dmg_lv = C.ATK_DEF, crit = false, hit = false,
		parts = parts_zero(),
	}
	-- BR-RULESET-002: init. A plain attack is BF_WEAPON|BF_NORMAL, long when it fires arrows
	S.flag = C.BF_WEAPON + C.BF_NORMAL + (uses_arrow(src) and C.BF_LONG or C.BF_SHORT)
	S.left = left_handed(src)
	S.rele, S.lele = M.weapon_element(src, "r"), M.weapon_element(src, "l")

	local lucky = M.lucky_dodge(ctx, tgt)
	if lucky then
		S.type, S.dmg_lv = C.DMG_LUCY_DODGE, C.ATK_LUCKY
	else
		local crit = M.crit_roll(ctx, src, tgt, S)
		if crit then S.type, S.crit = C.DMG_CRITICAL, true end
		S.hit = M.hit_roll(ctx, cfg, src, tgt, crit, S)
		if not S.hit then S.dmg_lv = C.ATK_FLEE end
		-- the engine runs the effective order; `lane` and `view` are what stages of OTHER mods see of this attack
		local res = run("weapon", S, { skip = S.hit and NO_SKIP or M.HIT_ONLY, lane = S.dmg, view = S })
		if res and res.cancelled then
			S.dmg:set(0)
			S.dmg2:set(0)
			return { damage = 0, damage2 = 0, type = S.type, div = S.div, flag = S.flag, dmg_lv = C.ATK_BLOCK, cancelled = true }
		end
	end
	-- ---- BR-RULESET-001: total below 1 is a miss; players report a single hit
	local d1, d2 = S.dmg:get(), S.dmg2:get()
	S.dmg_lv, S.div = M.finish(src.kind == "pc", d1, d2, S.dmg_lv, S.div)
	return { damage = d1, damage2 = d2, type = S.type, div = S.div, flag = S.flag, dmg_lv = S.dmg_lv }
end

return M
