--!nonstrict
-- Conformance of the RENEWAL basic weapon attack against numbers recorded from the REAL legacy renewal battle code
-- (conformance/golden_gen). Every case replays the recorded RNG draws in order and compares damage, damage2, type, div, flag,
-- dmg_lv and the number/kind/bounds of the draws. Nothing here is tolerant: one differing field fails the category.
local T = import("tests/support/harness")
local runner = import("tests/support/golden_runner")
local pipeline = import("scripts/pipeline")
local fake_ctx = import("tests/support/fake_ctx")

local TAG = "GOLDEN-WEAPON-RE"
local golden = runner.load()

-- ---- one test per category (all cases of the category must match)
local order, by_cat = {}, {}
for _, c in ipairs(golden) do
	if not by_cat[c.cat] then
		by_cat[c.cat] = {}
		order[#order + 1] = c.cat
	end
	local list = by_cat[c.cat]
	list[#list + 1] = c
end
table.sort(order)
local cat_ok = {}
for _, cat in ipairs(order) do
	local list = by_cat[cat]
	T.run(TAG, "category " .. cat .. " (" .. #list .. " cases)", false, function()
		local bad, first = 0, nil
		for _, c in ipairs(list) do
			local r = runner.run(c)
			if not r.ok then
				bad = bad + 1
				first = first or ("case " .. c.id .. " [" .. tostring(r.class) .. "] " .. tostring(r.detail))
			end
		end
		cat_ok[cat] = bad == 0
		if bad > 0 then error({ code = "MISMATCH", rule = TAG, msg = bad .. " of " .. #list .. " differ; first: " .. first }) end
	end)
end

-- ---- per BR-RULESET rule: every golden category that exercises the rule must match completely
local RULE_CATS = {
	["BR-RULESET-001"] = { "miss_forced", "zero_atk", "hit_flee_sweep" },
	["BR-RULESET-003"] = { "lucky_dodge" },
	["BR-RULESET-005"] = { "crit_player", "crit_mob", "crit_vs_luk", "crit_off_or_negative", "crit_critaddrace" },
	["BR-RULESET-006"] = { "hit_flee_sweep", "hit_rate_clamp", "hit_skills", "hit_perfect_hit", "miss_forced" },
	["BR-RULESET-008"] = { "mix_mob_vs_any", "crit_mob", "zero_atk", "mob_ranged" },
	["BR-RULESET-009"] = { "mix_pc_vs_any", "weapon_roll", "weapon_roll_dex_nonarrow", "parts_batk_eatk_atkrate", "mastery_atk", "unarmed",
		"shield_draw", "weapon_perfection", "weapon_max_or_crit", "size_fix", "overrefine_wdr", "gun_wdr_alias" },
	["BR-RULESET-010"] = { "weapon_roll", "weapon_roll_dex_nonarrow", "bow_gun_dex", "weapon_atk_u16", "weapon_max_or_crit",
		"weapon_perfection", "size_fix", "overrefine_wdr" },
	["BR-RULESET-011"] = { "bow_gun_dex", "element_arrow" },
	["BR-RULESET-014"] = { "crit_player", "atk_rates_short_long", "rates_with_ratio" },
	["BR-RULESET-015"] = { "status_atk_ratio", "status_atk_floor", "rates_with_ratio" },
	["BR-RULESET-016"] = { "status_atk_ratio", "status_atk_floor", "status_atk_cap" },
	["BR-RULESET-020"] = { "cards_both", "cards_vs_player_pc", "cards_vs_player_mob", "cards_extreme", "cards_mastery_status" },
	["BR-RULESET-021"] = { "cards_both", "cards_vs_player_pc", "cards_vs_player_mob", "cards_extreme", "cards_mastery_status" },
	["BR-RULESET-024"] = { "mastery_atk", "cards_mastery_status" },
	["BR-RULESET-025"] = { "res", "res_nonpositive" },
	["BR-RULESET-027"] = { "def_edge", "def_softdef_gt_damage", "def_weapon_defense_type", "crowd_penalty", "crowd_mob_target",
		"crowd_steelbody", "crit_def_applies" },
	["BR-RULESET-028"] = { "def_softdef_gt_damage", "dual_wield_min1", "zero_atk" },
	["BR-RULESET-029"] = { "element_endow", "element_arrow" },
	["BR-RULESET-030"] = { "element_mob_attr_none", "element_endow", "element_table", "element_mob_crit" },
	["BR-RULESET-031"] = { "element_status_ratio", "element_table" },
	["BR-RULESET-032"] = { "element_negative_ratio", "element_mob_negative" },
	["BR-RULESET-034"] = { "crit_player", "crit_mob", "crit_def_applies", "element_mob_crit", "element_pc_crit" },
	["BR-RULESET-037"] = { "dual_wield", "dual_wield_min1", "dual_wield_skills" },
	["BR-RULESET-039"] = { "target_status_map", "damagetaken" },
	["BR-RULESET-041"] = { "map_gvg_rates", "target_status_map" },
	["BR-RULESET-102"] = { "size_fix", "overrefine_wdr", "gun_wdr_alias" },
}
local rules = {}
for r in pairs(RULE_CATS) do rules[#rules + 1] = r end
table.sort(rules)
for _, rule in ipairs(rules) do
	local cats = RULE_CATS[rule]
	T.run(rule, "golden RE: categories " .. table.concat(cats, ", "), false, function()
		local n = 0
		for _, cat in ipairs(cats) do
			assert(by_cat[cat], "no golden category " .. cat)
			assert(cat_ok[cat], "category " .. cat .. " has mismatches")
			n = n + #by_cat[cat]
		end
		assert(n >= 1)
	end)
end

-- ---- stage probes agree too (they localise a regression; a mismatch here would mean a stage moved or changed its inputs)
T.run(TAG, "stage probes of every case equal the legacy probes (order and values)", false, function()
	local rep = runner.run_all(golden)
	if rep.stage_mismatch > 0 then
		error({ code = "MISMATCH", rule = TAG, msg = rep.stage_mismatch .. " cases differ in their stage trace" })
	end
	assert(rep.matched == rep.total, rep.total - rep.matched .. " cases differ")
	assert(rep.total >= 2000, "fewer than 2000 golden cases: " .. rep.total)
end)

-- ---- BR-RULESET-019: the stage order observed in the legacy probes is consistent with pipeline.ORDER.weapon
T.run("BR-RULESET-019", "legacy stage probes occur in the order of pipeline.ORDER.weapon", false, function()
	local index = {}
	for i, name in ipairs(pipeline.ORDER.weapon) do index[name] = i end
	-- probe name -> the stage that has just completed when the legacy probe fires
	local probe_stage = {
		base = "base_attack", p_status = "base_attack", p_weapon = "base_attack", p_equip = "base_attack", p_mastery = "base_attack",
		c_status = "sc_bonus", c_weapon = "sc_bonus", c_equip = "sc_bonus", c_mastery = "sc_bonus",
		sum = "sum", ratio = "skill_ratio", const_add = "const_add", katar = "adv_katar_mastery", res = "res",
		defred = "defense", postdef = "post_defense", defense = "element", ele = "element", attr = "element", element = "element",
		crit = "crit_multiplier", pre_card = "flat_additions", card_atk = "card_target", card_tgt = "card_target", div = "div_hands",
		hands = "div_hands", map = "map_rates", final = "final",
	}
	local checked, saw = 0, {}
	for _, c in ipairs(golden) do
		local last = 0
		for _, s in ipairs(c.out.stages) do
			local st = probe_stage[s[1]]
			if st then
				assert(index[st], "ORDER lacks stage " .. st)
				assert(index[st] >= last, "case " .. c.id .. ": probe " .. s[1] .. " (" .. st .. ") occurs after a later ORDER stage")
				last = index[st]
				saw[st] = true
			end
		end
		checked = checked + 1
	end
	assert(checked == #golden)
	for _, name in ipairs({ "base_attack", "sc_bonus", "sum", "skill_ratio", "res", "defense", "post_defense", "element", "crit_multiplier", "card_target", "div_hands", "map_rates" }) do
		assert(saw[name], "no golden probe observed stage " .. name)
	end
end)

-- ---- worked examples of BR_RULESET.md section 7, reproduced end to end by the legacy code AND by the Luau pipeline
local function nth_worked(n)
	local i = 0
	for _, c in ipairs(golden) do
		if c.cat == "worked_example" then
			i = i + 1
			if i == n then return c end
		end
	end
	error("worked example record missing")
end
local function both(rule, title, n, want, want2)
	T.run(rule, title, false, function()
		local c = nth_worked(n)
		assert(c.out.damage == want, "legacy recorded " .. c.out.damage)
		local res = runner.run(c).result
		assert(res.damage == want, "luau " .. tostring(res.damage))
		if want2 then assert(res.damage2 == want2) end
	end)
end
both("BR-RULESET-009", "control: a lone weapon ATK part of 1000 passes the whole RE pipeline unchanged (legacy and Luau)", 1, 1000)
both("BR-RULESET-031", "E-1 RE: Water 1000 vs Fire lv1 with Deluge 15: ratio 150 + 15 = 165 -> 1650 (legacy and Luau)", 2, 1650)
both("BR-RULESET-025", "E-2: RES 100 on 1000 -> 840 (legacy and Luau)", 3, 840)
both("BR-RULESET-025", "E-2: RES 100 with 50% ignored -> 912 (legacy and Luau)", 4, 912)
both("BR-RULESET-020", "E-3 RE: race +20% and size +15% on the weapon part multiply -> 1380 (legacy and Luau)", 5, 1380)
T.run("BR-RULESET-010", "E-9: weapon ATK 100, lv 4, STR 80 rolls in [120, 160]; a crit takes 160 (legacy and Luau)", false, function()
	local c = nth_worked(6)
	assert(c.out.draws[2][1] == 120 and c.out.draws[2][2] == 160, "legacy bounds")
	local c2 = nth_worked(7)
	assert(c2.out.damage == 224, "crit recorded " .. c2.out.damage) -- 160 * 1.4
	assert(runner.run(c).ok and runner.run(c2).ok)
end)
both("BR-RULESET-034", "crit of a crate 0 player: 1000 x 1.4 = 1400 (legacy and Luau)", 8, 1400)
both("BR-RULESET-034", "crit of a crate 37 player: 1000 x (1.4 + 0.37) in float32 = 1770 (legacy and Luau)", 9, 1770)

-- ---- the fake ctx has C++ int64 semantics (the lint the numerics note asks for: no floor division on signed values)
T.run(TAG, "fake ctx truncates toward zero, guards the integer-safe range", false, function()
	local ctx = fake_ctx.new({})
	assert(ctx.idiv(-7, 2) == -3 and ctx.idiv(7, -2) == -3 and ctx.idiv(-7, -2) == 3 and ctx.idiv(7, 2) == 3)
	assert(ctx.mul_div(-1, 99, 100) == 0 and ctx.mul_div(-101, 99, 100) == -99)
	local ok = pcall(ctx.mul_div, 9007199254740991, 3, 1)
	assert(not ok, "overflow beyond 2^53 must be refused, not silently rounded")
end)

-- ---- inputs outside the covered space are refused loudly instead of producing a plausible wrong number
T.run(TAG, "inputs outside the golden-covered space raise instead of guessing", false, function()
	local weapon = import("scripts/damage/weapon")
	local unit = import("scripts/damage/unit")
	local cfg = import("data/damage_cfg")
	local ctx = fake_ctx.new(import("tests/support/scripted_rng").lcg(1))
	local pc = unit.normalize({ kind = "pc", wtype1 = 1, rhw = { atk = 10, atk2 = 0, ele = 0, wlv = 1 }, bonus = { double_rate = 5 } })
	local tgt = unit.normalize({ kind = "mob" })
	assert(not pcall(weapon.calc_attack, ctx, cfg, pc, tgt, {}), "double attack must be refused (skill rule, not covered)")
	pc = unit.normalize({ kind = "pc", wtype1 = 16, rhw = { atk = 10, atk2 = 0, ele = 0, wlv = 1 } })
	assert(not pcall(weapon.calc_attack, ctx, cfg, pc, tgt, {}), "katar must be refused")
end)
