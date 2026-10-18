--!nonstrict
-- Per-rule slices of the golden data. A converted rule test (conformance/cases/damage.luau) is NOT a hand-written
-- expectation: its `golden` field names one recorded legacy case, and `given` / `expected` are cut from that record by the
-- functions below (inputs from the case, expected values from the legacy output and the legacy stage probes).
-- The acceptance test re-derives both from the record and fails when the case file drifts from the data.
-- `find` documents (and checks) why the record illustrates the rule; `tools/print_rule_cases` prints the case literals.
local TABLES = import("data/damage_tables")

local M = {}

local function stage(rec, name)
	for _, s in ipairs(rec.out.stages) do
		if s[1] == name then return s end
	end
	error("record " .. rec.id .. " has no stage probe " .. name)
end

local function has_stage(rec, name)
	for _, s in ipairs(rec.out.stages) do
		if s[1] == name then return true end
	end
	return false
end

local function count(t)
	local n = 0
	for _ in pairs(t or {}) do n = n + 1 end
	return n
end

local function first_pair(t)
	local k, v = next(t)
	return k, v
end

local function bonus(u) return u.bonus or {} end

-- BR-RULESET-001: total below 1 is a MISS, players report div 1
M["BR-RULESET-001"] = {
	find = function(r)
		return r.cat == "base_zero_atk" and r.a.kind == "pc" and r.out.dmg_lv == 3 and has_stage(r, "final")
			and stage(r, "final")[2] + stage(r, "final")[3] == 0
	end,
	given = function(r) local f = stage(r, "final"); return { attack_type = "weapon", is_player = true, damage = f[2], damage2 = f[3], dmg_lv = 5, div = 1 } end,
	expected = function(r) return { dmg_lv = r.out.dmg_lv, div = r.out.div } end,
}

-- BR-RULESET-003: lucky dodge, drawn only when flee2 ~= 0
M["BR-RULESET-003"] = {
	find = function(r) return r.out.dmg_lv == 1 end,
	given = function(r) return { basic_attack = true, flee2 = r.t.flee2, draws = { r.out.draws[1] } } end,
	expected = function(r) return { avoided = r.out.type == 11 } end,
}

-- BR-RULESET-005: crit chance after target LUK and crit-defense scaling, then the draw
M["BR-RULESET-005"] = {
	find = function(r)
		return r.out.type == 10 and r.a.kind == "pc" and r.t.kind == "pc" and (bonus(r.t).crit_def or 0) > 0 and (r.t.flee2 or 0) == 0
			and not r.a.arrow and not bonus(r.a).critadd_race and r.a.cri ~= 0
	end,
	given = function(r)
		return { basic_attack = true, attacker = "pc", target = "pc", cri = r.a.cri, target_luk = r.t.luk, crit_def = bonus(r.t).crit_def,
			draws = { r.out.draws[1] } }
	end,
	expected = function(r) return { crit = r.out.type == 10, chance = stage(r, "cri")[2] } end,
}

-- BR-RULESET-006: hit rate 80 + hit - flee clamped to 5..100, then the draw
M["BR-RULESET-006"] = {
	find = function(r)
		return r.out.type == 0 and has_stage(r, "hitrate") and stage(r, "hitrate")[2] == 100 and (r.t.targeted or 0) == 0
			and r.a.kind == "pc" and not r.a.arrow and (r.t.flee2 or 0) == 0 and r.a.cri == 0 and r.out.dmg_lv == 5
			and next(r.cfg) == nil and 80 + r.a.hit - r.t.flee > 100 and not bonus(r.a).perfect_hit
	end,
	given = function(r) return { hit = r.a.hit, flee = r.t.flee, min = 5, max = 100, draws = { r.out.draws[1] } } end,
	expected = function(r) return { hit = r.out.dmg_lv ~= 2, hitrate = stage(r, "hitrate")[2] } end,
}

-- BR-RULESET-008: PC base damage: min = DEX*(80+20*wlv)% capped at max = weapon ATK, roll never reaches the top
M["BR-RULESET-008"] = {
	find = function(r)
		return r.a.kind == "pc" and r.a.cri == 0 and not r.a.arrow and not r.a.sc and (r.t.flee2 or 0) == 0 and r.out.dmg_lv == 5
			and next(bonus(r.a)) == nil and has_stage(r, "basemm") and stage(r, "basemm")[3] > stage(r, "basemm")[2]
			and (r.a.rhw.wlv or 0) > 0 and not r.a.no_sizefix and not r.a.lhw and #r.out.draws >= 2 and r.cat == "mix_pc_vs_any"
			and r.out.type == 0 and r.out.draws[2] ~= nil and type(r.out.draws[2]) == "number"
	end,
	given = function(r)
		return { weapon_atk = r.a.rhw.atk, weapon_lv = r.a.rhw.wlv, dex = r.a.dex, crit = false, maximize = false, size = r.t.size,
			wtype = r.a.wtype1, batk = r.a.batk, draws = { r.out.draws[2] } }
	end,
	expected = function(r) local mm = stage(r, "basemm"); return { min = mm[2], max = mm[3], damage = stage(r, "base")[2] } end,
}

-- BR-RULESET-014: PRE crit adds crit_atk_rate percent to the (max) base damage
M["BR-RULESET-014"] = {
	find = function(r)
		return r.out.type == 10 and r.a.kind == "pc" and (bonus(r.a).crit_atk_rate or 0) > 0 and not r.a.arrow and not bonus(r.a).overrefine_r
			and not r.a.no_sizefix and not (r.t.kind == "pc" and bonus(r.t).crit_def_rate) and not r.a.sc and not r.a.lhw
	end,
	given = function(r)
		local max = stage(r, "basemm")[3]
		local pre = math.floor(max * TABLES.size_pct[r.a.wtype1][r.t.size + 1] / 100) + r.a.batk
		return { damage = pre, crit = true, crit_atk_rate = bonus(r.a).crit_atk_rate }
	end,
	expected = function(r) return { damage = stage(r, "base")[2] } end,
}

-- BR-RULESET-015: skill ratio 100 + bonuses, Berserk +100 in PRE
M["BR-RULESET-015"] = {
	find = function(r)
		return r.a.sc and r.a.sc.berserk and not r.a.sc.overthrust and not bonus(r.a).skill_ratio and has_stage(r, "ratio")
			and stage(r, "ratio")[2] == stage(r, "base")[2] * 2 and stage(r, "base")[2] > 50 and not r.a.lhw
	end,
	given = function(r) return { damage = stage(r, "base")[2], berserk = true, bonus_skill_ratio = 0 } end,
	expected = function(r) return { ratio = 200, damage = stage(r, "ratio")[2] } end,
}

-- BR-RULESET-016: PRE status ATK% multiplies after the ratio (Curse -25 %)
M["BR-RULESET-016"] = {
	find = function(r)
		return r.a.sc and r.a.sc.curse and not r.a.sc.provoke and not r.a.sc.concentration and not r.a.lhw and stage(r, "ratio")[2] >= 50
			and stage(r, "ratio")[2] ~= stage(r, "sc_bonus")[2]
	end,
	given = function(r) return { damage = stage(r, "ratio")[2], sc = { curse = {} } } end,
	expected = function(r) return { damage = stage(r, "sc_bonus")[2] } end,
}

-- BR-RULESET-020: attacker card fix multiplies (E-3: race +20 %, size +15 % on 1000 -> 1380)
M["BR-RULESET-020"] = {
	find = function(r) return r.cat == "worked_example" and bonus(r.a).add_race ~= nil end,
	given = function(r)
		local rk, rv = first_pair(r.a.bonus.add_race)
		local sk, sv = first_pair(r.a.bonus.add_size)
		return { damage = stage(r, "pre_card")[2], race_id = rk, race_pct = rv, size_id = sk, size_pct = sv }
	end,
	expected = function(r) return { damage = stage(r, "card_atk")[2] } end,
}

-- BR-RULESET-021: defender card fix (a player target's element, size, race, class, near/far rates)
M["BR-RULESET-021"] = {
	find = function(r)
		return r.cat == "cards_vs_player" and r.a.kind == "mob" and stage(r, "card_tgt")[2] ~= stage(r, "card_atk")[2]
			and bonus(r.t).sub_ele and bonus(r.t).sub_race and bonus(r.t).sub_size and bonus(r.t).near_def and bonus(r.t).near_def > 0
			and stage(r, "card_tgt")[2] > 20 and r.out.flag == 529
	end,
	given = function(r)
		local ele = stage(r, "ele")
		return { damage = stage(r, "card_atk")[2], flag = r.out.flag, rele = ele[2], lele = ele[3],
			src = { kind = "mob", element = r.a.element, size = r.a.size, race = r.a.race, boss = r.a.boss or false },
			tgt = { kind = "pc", bonus = r.t.bonus } }
	end,
	expected = function(r) return { damage = stage(r, "card_tgt")[2] } end,
}

-- BR-RULESET-027: PRE crit ignores DEF (hard and soft); the soft-DEF draw still happens
M["BR-RULESET-027"] = {
	find = function(r)
		return r.out.type == 10 and r.a.kind == "mob" and #r.out.draws == 2 and r.t.def > 0 and r.t.kind == "mob" and (r.t.targeted or 0) == 0
			and stage(r, "defense")[2] == stage(r, "sc_bonus")[2] and stage(r, "sc_bonus")[2] >= 1 and not r.t.sc and r.cat == "def_crit_ignores"
			and type(r.out.draws[2]) == "number"
	end,
	given = function(r)
		return { crit = true, damage = stage(r, "sc_bonus")[2], tgt = { kind = r.t.kind, def = r.t.def, def2 = r.t.def2 }, draws = { r.out.draws[2] } }
	end,
	expected = function(r) return { damage = stage(r, "defred")[2], def_bypassed = true } end,
}

-- BR-RULESET-028: PRE post-defense: refine bonus is added to the reduced damage (which may be <= 0), then min 1
M["BR-RULESET-028"] = {
	find = function(r)
		return r.a.kind == "pc" and not r.a.lhw and has_stage(r, "defred") and stage(r, "defred")[2] + r.a.rhw.atk2 < 1
			and stage(r, "defense")[2] == 1 and r.cat == "def_softdef_gt_damage"
	end,
	given = function(r) return { damage = stage(r, "defred")[2], refine_bonus = r.a.rhw.atk2, is_player = true } end,
	expected = function(r) return { damage = stage(r, "defense")[2] } end,
}

-- BR-RULESET-029: an endow overrides the weapon element
M["BR-RULESET-029"] = {
	find = function(r)
		return r.a.sc and r.a.sc.enchantarms and r.a.sc.enchantarms.val1 ~= r.a.rhw.ele and r.cat == "element_endow" and has_stage(r, "ele")
	end,
	given = function(r) return { skill_element = "weapon", weapon_element = r.a.rhw.ele, endow = r.a.sc.enchantarms.val1 } end,
	expected = function(r) return { element = stage(r, "ele")[2] } end,
}

-- BR-RULESET-030: non-player basic attacks skip the element table while attack_attr_none covers their type
M["BR-RULESET-030"] = {
	find = function(r)
		if not (r.cat == "element_attr_none" and r.a.kind == "mob" and (r.cfg.attack_attr_none == nil or r.cfg.attack_attr_none == 14)) then return false end
		if not has_stage(r, "ele") then return false end
		local ratio = TABLES.element_pct[r.t.ele_lv or 1][r.a.rhw.ele][r.t.element or 0]
		return ratio ~= 100 and stage(r, "element")[2] == stage(r, "defense")[2] and stage(r, "defense")[2] > 0
	end,
	given = function(r)
		return { attacker = "mob", attack_attr_none = r.cfg.attack_attr_none or 14, damage = stage(r, "defense")[2], atk_element = stage(r, "ele")[2],
			def_element = r.t.element or 0, def_ele_lv = r.t.ele_lv or 1 }
	end,
	expected = function(r) return { damage = stage(r, "element")[2] } end,
}

-- BR-RULESET-031: PRE status percent first, then the table (E-1: Deluge 15 on 1000 water vs fire lv1 -> 1725)
M["BR-RULESET-031"] = {
	find = function(r) return r.cat == "worked_example" and r.a.sc and r.a.sc.deluge ~= nil end,
	given = function(r)
		return { atk_element = stage(r, "ele")[2], damage = stage(r, "defense")[2], def_element = r.t.element, def_ele_lv = r.t.ele_lv,
			deluge_val3 = r.a.sc.deluge.val3 }
	end,
	expected = function(r) return { damage = stage(r, "attr")[2] } end,
}

-- BR-RULESET-032: attr_recover 0 keeps a negative table result from healing (damage cut to 0)
M["BR-RULESET-032"] = {
	find = function(r)
		return r.cat == "element_negative_ratio" and r.cfg.attr_recover == 0 and has_stage(r, "attr") and stage(r, "attr")[2] < 0
			and stage(r, "element")[2] == 0
	end,
	given = function(r)
		return { damage = stage(r, "defense")[2], atk_element = stage(r, "ele")[2], def_element = r.t.element, def_ele_lv = r.t.ele_lv, attr_recover = 0 }
	end,
	expected = function(r) return { damage = stage(r, "element")[2] } end,
}

-- BR-RULESET-033: PRE EDP adds val3 percent after the table (right hand)
M["BR-RULESET-033"] = {
	find = function(r)
		return r.cat == "element_edp" and r.a.kind == "pc" and not r.a.lhw and stage(r, "element")[2] ~= stage(r, "attr")[2] and stage(r, "attr")[2] >= 10
	end,
	given = function(r) return { damage = stage(r, "attr")[2], edp_val3 = r.a.sc.edp.val3 } end,
	expected = function(r) return { damage = stage(r, "element")[2] } end,
}

-- BR-RULESET-037: dual wield (E-6: Assassin 5/5, both hands 1000 -> right 1000, left 800)
M["BR-RULESET-037"] = {
	find = function(r) return r.cat == "worked_example" and r.a.lhw ~= nil end,
	given = function(r)
		local d = stage(r, "div")
		return { right = d[2], left = d[3], right_lv = r.a.skills.as_right, left_lv = r.a.skills.as_left, job = "assassin" }
	end,
	expected = function(r) local h = stage(r, "hands"); return { right = h[2], left = h[3] } end,
}

-- BR-RULESET-039: PRE Assumptio halves the damage (2/3 on versus maps)
M["BR-RULESET-039"] = {
	find = function(r)
		return r.cat == "target_status_map" and r.map == "normal" and count(r.t.sc) == 1 and r.t.sc.assumptio and not r.a.lhw
			and stage(r, "hands")[2] >= 10 and r.out.flag == 529
	end,
	given = function(r) return { damage = stage(r, "hands")[2], assumptio = true, vs_map = false } end,
	expected = function(r) return { damage = stage(r, "map")[2] } end,
}

-- BR-RULESET-041: GvG basic attacks use the short / long damage rate (80 %), minimum 1
M["BR-RULESET-041"] = {
	find = function(r)
		return r.cat == "map_gvg_rates" and r.map == "gvg" and not r.t.sc and r.a.kind == "pc" and not r.a.lhw and not r.a.arrow
			and (r.cfg.gvg_short_damage_rate or 80) == 80 and stage(r, "hands")[2] >= 10 and stage(r, "map")[2] ~= stage(r, "hands")[2]
	end,
	given = function(r)
		return { map_mode = "gvg", range = r.out.flag == 577 and "long" or "short", damage = stage(r, "hands")[2],
			gvg_short_damage_rate = r.cfg.gvg_short_damage_rate or 80, gvg_long_damage_rate = r.cfg.gvg_long_damage_rate or 80 }
	end,
	expected = function(r) return { damage = stage(r, "map")[2] } end,
}

return M
