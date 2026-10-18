--!nonstrict
-- More pipeline API checks (extra suite; tests/main is what rathena-sim runs and pins at 111 tests): the engine-driven pipeline (pipeline.define / stage / run, plus the mod-facing insert / replace / cancel / move)
-- runs this package's stage bodies. The CLI has no script-host, so these run against the fake host (tests/support/fake_host); the same
-- scenarios run against the REAL pipeline runtime in script-host/tests/test_rulesets.cpp. The cross-package numbers (shared lines,
-- operation counts versus the pre-renewal order) are computed by tools/diff_report.py and checked from Python.
local T = import("tests/support/harness")
local booted = import("tests/support/booted")
local fake_host = import("tests/support/fake_host")
local pipeline = import("scripts/pipeline")
local init = import("scripts/init")
local weapon = import("scripts/damage/weapon")
local unit = import("scripts/damage/unit")
local cfg = import("data/damage_cfg")
local fake_ctx = import("tests/support/fake_ctx")
local scripted = import("tests/support/scripted_rng")

local function mk_attack()
	local a = unit.normalize({ kind = "pc", wtype1 = 0, hit = 500, str = 1, dex = 1, rhw = { atk = 1000, atk2 = 0, ele = 0, wlv = 0 } })
	local t = unit.normalize({ kind = "mob", res = 100, flee = 0, size = 1 })
	return a, t
end

-- registers the package with a fresh fake engine, lets `setup` edit the pipeline through the mod-facing API, runs one attack and
-- restores the default binding. Returns the attack result and the stage probes in the order they ran.
local function attack(setup, config)
	local res, trace
	booted.isolated(function()
		local engine = fake_host(config)
		init.register(engine)
		if setup then setup(engine.pipeline) end
		local ctx = fake_ctx.new(scripted.lcg(7))
		local a, t = mk_attack()
		trace = {}
		local ok, r = pcall(weapon.calc_attack, ctx, cfg, a, t, { trace = trace })
		assert(ok, type(r) == "table" and tostring(r.code) or tostring(r))
		res = r
	end)
	return res, trace
end
local function probe_names(trace)
	local out = {}
	for _, p in ipairs(trace) do out[#out + 1] = p[1] end
	return out
end

T.run("BR-RULESET-019", "the engine runs the registered order: RES of 100 leaves 840 of 1000", false, function()
	assert(attack().damage == 840)
end)

T.run("BR-RULESET-019", "pipeline.insert_after: an inserted stage runs where it was put (after element, +10 on 840)", false, function()
	local res = attack(function(pl) pl.insert_after("weapon", "element", "mymod:fire_brand", function(S) S.dmg:add(10) end) end)
	assert(res.damage == 850)
	-- insert_before / insert_after put a stage on either side of the anchor: the probes recorded so far tell where it ran
	local seen = {}
	attack(function(pl)
		pl.insert_before("weapon", "element", "mymod:early", function(S) seen.early = #S.trace end)
		pl.insert_after("weapon", "element", "mymod:late", function(S) seen.late = #S.trace end)
	end)
	assert(seen.early and seen.late and seen.early < seen.late, "early " .. tostring(seen.early) .. " late " .. tostring(seen.late))
end)

T.run("BR-RULESET-019", "pipeline.move: moving 'res' behind 'defense' moves its probe behind the defense probes", false, function()
	local _, base = attack(nil)
	local _, moved = attack(function(pl) pl.move("weapon", "res", { after = "defense" }) end)
	local function index(trace, name)
		for i, p in ipairs(trace) do if p[1] == name then return i end end
	end
	assert(index(base, "res") < index(base, "defred"), "baseline: RES before the DEF reduction")
	assert(index(moved, "res") > index(moved, "defred"), "moved: RES after the DEF reduction")
	assert(#base == #moved, "the same probes ran")
end)

T.run("BR-RULESET-019", "a stage cancelled and re-inserted under a new id reproduces the pre-edit result (cancel + insert_after == move)", false, function()
	local viaMove = attack(function(pl) pl.move("weapon", "res", { after = "defense" }) end)
	local viaCancel = attack(function(pl)
		pl.cancel("weapon", "res")
		pl.insert_after("weapon", "defense", "mymod:res", weapon.STAGES.res)
	end)
	assert(viaMove.damage == viaCancel.damage and viaMove.damage == 840)
end)

T.run("BR-RULESET-019", "a stage without a body raises NO_STAGE_BODY (the engine refuses to skip it silently)", false, function()
	local engine = fake_host()
	engine.pipeline.define("weapon", { "init", "ghost" })
	local ok, e = pcall(engine.pipeline.run, "weapon", {})
	assert(not ok and type(e) == "table" and e.code == "NO_STAGE_BODY")
end)

T.run("BR-RULESET-019", "the probes of one attack follow the effective order (ORDER.weapon without edits)", false, function()
	local order_index = {}
	for i, n in ipairs(pipeline.ORDER.weapon) do order_index[n] = i end
	local stage_of = { base = "base_attack", sum = "sum", ratio = "skill_ratio", res = "res", defense = "element", element = "element", final = "final" }
	local _, trace = attack(nil)
	local last = 0
	for _, p in ipairs(trace) do
		local st = stage_of[p[1]]
		if st then
			assert(order_index[st] >= last, "probe " .. p[1] .. " ran out of order")
			last = order_index[st]
		end
	end
	assert(last == order_index.final)
end)
