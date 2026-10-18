--!nonstrict
-- Conformance of the pre-renewal basic weapon attack against numbers recorded from the REAL legacy battle code
-- (conformance/golden_gen). Every case replays the recorded RNG draws in order and compares damage, damage2, type, div,
-- flag, dmg_lv and the number/kind/bounds of the draws. Nothing here is tolerant: one differing field fails the category.
local T = import("tests/support/harness")
local runner = import("tests/support/golden_runner")
local slices = import("tests/support/rule_slices")
local pipeline = import("scripts/pipeline")
local fake_ctx = import("tests/support/fake_ctx")
local cases_damage = import("conformance/cases/damage")
local function unit_or(t) return import("scripts/damage/unit").normalize(t) end

local TAG = "GOLDEN-WEAPON-PRE"
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
		if bad > 0 then error({ code = "MISMATCH", rule = TAG, msg = bad .. " of " .. #list .. " differ; first: " .. first }) end
	end)
end

-- ---- stage probes agree too (they localise a regression; a mismatch here would mean the stage order moved)
T.run(TAG, "stage probes of every case equal the legacy probes (order and values)", false, function()
	local rep = runner.run_all(golden)
	if rep.stage_mismatch > 0 then
		error({ code = "MISMATCH", rule = TAG, msg = rep.stage_mismatch .. " cases differ in their stage trace" })
	end
	assert(rep.matched == rep.total, rep.total - rep.matched .. " cases differ")
	assert(rep.total >= 2000, "fewer than 2000 golden cases: " .. rep.total)
end)

-- ---- worked examples of BR_RULESET.md section 7, reproduced end to end by the legacy code AND by the Luau pipeline
local function worked(bonus_key)
	for _, c in ipairs(golden) do
		if c.cat == "worked_example" and bonus_key(c) then return c end
	end
	error("worked example record missing")
end
T.run("BR-RULESET-031", "E-1 PRE: Water 1000 vs Fire lv1 with Deluge 15 -> 1150 -> x150% = 1725 (legacy and Luau)", false, function()
	local c = worked(function(r) return r.a.sc and r.a.sc.deluge end)
	assert(c.out.damage == 1725, "legacy recorded " .. c.out.damage)
	assert(runner.run(c).result.damage == 1725)
end)
T.run("BR-RULESET-020", "E-3: race +20% and size +15% on 1000 multiply -> 1380, not 1350 (legacy and Luau)", false, function()
	local c = worked(function(r) return r.a.bonus and r.a.bonus.add_race end)
	assert(c.out.damage == 1380, "legacy recorded " .. c.out.damage)
	assert(runner.run(c).result.damage == 1380)
end)
T.run("BR-RULESET-037", "E-6: Assassin AS_RIGHT 5 / AS_LEFT 5, both hands 1000 -> right 1000, left 800 (legacy and Luau)", false, function()
	local c = worked(function(r) return r.a.lhw end)
	assert(c.out.damage == 1000 and c.out.damage2 == 800, "legacy recorded " .. c.out.damage .. "/" .. c.out.damage2)
	local res = runner.run(c).result
	assert(res.damage == 1000 and res.damage2 == 800)
end)

-- ---- BR-RULESET-018: the stage order observed in the legacy probes is consistent with pipeline.ORDER.weapon
T.run("BR-RULESET-018", "legacy stage probes occur in the order of pipeline.ORDER.weapon", false, function()
	local index = {}
	for i, name in ipairs(pipeline.ORDER.weapon) do index[name] = i end
	local probe_stage = { base = "base_attack", ratio = "skill_ratio", sc_bonus = "skill_atk_bonus", defred = "defense", defense = "post_defense",
		attr = "element", element = "element_bonus", card_atk = "card_attacker", card_tgt = "card_target", hands = "div_hands",
		map = "map_rates", final = "final" }
	local checked = 0
	for _, c in ipairs(golden) do
		local last = 0
		for _, s in ipairs(c.out.stages) do
			local st = probe_stage[s[1]]
			if st then
				assert(index[st], "ORDER lacks stage " .. st)
				assert(index[st] >= last, "case " .. c.id .. ": probe " .. s[1] .. " (" .. st .. ") occurs after a later ORDER stage")
				last = index[st]
			end
		end
		checked = checked + 1
	end
	assert(checked == #golden)
end)

-- ---- converted rule cases are cut from the golden record they name (no hand-written expectations)
for _, case in ipairs(cases_damage) do
	local sl = slices[case.rule]
	if sl then
		T.run(case.rule, "case is derived from golden record #" .. tostring(case.golden), false, function()
			local rec
			for _, c in ipairs(golden) do
				if c.id == case.golden then
					rec = c
					break
				end
			end
			assert(rec, "golden record " .. tostring(case.golden) .. " not found")
			assert(sl.find(rec), "record does not satisfy the rule's find()")
			T.assert_deep_equal(case.given, sl.given(rec), case.rule)
			T.assert_deep_equal(case.expected, sl.expected(rec), case.rule)
		end)
	end
end

-- ---- the fake ctx has C++ int64 semantics (the lint the numerics note asks for: no floor division on signed values)
T.run(TAG, "fake ctx truncates toward zero, guards the integer-safe range", false, function()
	local ctx = fake_ctx.new({})
	assert(ctx.idiv(-7, 2) == -3 and ctx.idiv(7, -2) == -3 and ctx.idiv(-7, -2) == 3 and ctx.idiv(7, 2) == 3)
	assert(ctx.mul_div(-1, 99, 100) == 0 and ctx.mul_div(-101, 99, 100) == -99)
	assert(ctx.mul_div(1000, 1380 - 1000, 1000) == 380)
	local a = ctx.acc(-250)
	a:add_pct(-10)
	assert(a:get() == -225)
	a:mul_pct(50)
	assert(a:get() == -112)
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
	local ok = pcall(weapon.calc_attack, ctx, cfg, pc, tgt, {})
	assert(not ok, "double attack must be refused (skill rule, not covered)")
	pc = unit.normalize({ kind = "pc", wtype1 = 16, rhw = { atk = 10, atk2 = 0, ele = 0, wlv = 1 } })
	ok = pcall(weapon.calc_attack, ctx, cfg, pc, tgt, {})
	assert(not ok, "katar must be refused")
end)

-- ---- second oracle: the DEF captures of the map-rules golden vectors (tools/golden/capture.sh), replayed through the same code
T.run(TAG, "map-rules DEF captures (516 legacy rows): soft DEF with its RNG spread, then the PRE reduction", false, function()
	local vectors = import("build/golden_defense_vectors")
	local weapon = import("scripts/damage/weapon")
	local cfg = import("data/damage_cfg")
	local scripted = import("tests/support/scripted_rng").scripted
	local n = 0
	for _, kind in ipairs({ "pc", "mob" }) do
		for i, row in ipairs(vectors[kind]) do
			local draws, tgt = {}, { kind = kind, def = row.def1, def2 = row.def2, targeted = 0 }
			if kind == "pc" then
				local t = (3 * row.def2 - (3 * row.def2) % 10) / 10
				local top = (row.def2 * row.def2 - (row.def2 * row.def2) % 150) / 150 - t - 1
				if top < 0 then top = 0 end
				draws[1] = { 0, top, row.rnd }
			else
				local q = (row.def2 - row.def2 % 20) / 20
				if q * q > 0 then draws[1] = row.rnd end
			end
			local rng = scripted(draws)
			local ctx = fake_ctx.new(rng)
			local vit, def1 = weapon.vit_def(ctx, cfg, unit_or(tgt))
			assert(rng.used == rng.total, kind .. " row " .. i .. ": draw count")
			assert(vit == row.vit_def, kind .. " row " .. i .. ": vit_def " .. vit .. " ~= " .. row.vit_def)
			local out = weapon.def_reduce(ctx, row.damage_in, def1, vit, row.ignores_def == 1, row.weapon_defense_type)
			assert(out == row.damage_out, kind .. " row " .. i .. ": damage " .. out .. " ~= " .. row.damage_out)
			n = n + 1
		end
	end
	assert(n == 516, "rows replayed: " .. n)
end)
