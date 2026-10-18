--!nonstrict
-- Architecture-proof checks that live on the Luau side: what register() asks of the engine, that every stage name of the order has
-- a body, and that the engine-driven pipeline (pipeline.define / stage / run, plus the mod-facing insert / replace / cancel / move)
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

T.run("BR-RULESET-019", "register() defines both orders, registers one body per weapon stage and uses only pipeline.define/stage/run, num.f32, config", false, function()
	local defined, stages, touched = {}, {}, {}
	local function strict(name, t)
		return setmetatable(t, { __index = function(_, k) touched[#touched + 1] = name .. "." .. tostring(k); error("package used " .. name .. "." .. tostring(k)) end })
	end
	-- an engine that offers ONLY what the package may use: any other API access raises
	local engine = strict("engine", {
		pipeline = strict("engine.pipeline", {
			define = function(kind, order) defined[kind] = order end,
			stage = function(kind, stage, fn, opts) stages[#stages + 1] = { kind = kind, stage = stage, fn = fn, protected = opts and opts.protected } end,
			run = function() end,
		}),
		num = strict("engine.num", { f32 = function(x) return x end }),
		config = strict("engine.config", { legacy_quirks = true }),
	})
	booted.isolated(function() init.register(engine) end) -- restores the binding the other tests use
	T.assert_deep_equal(defined.weapon, pipeline.ORDER.weapon, "BR-RULESET-019")
	T.assert_deep_equal(defined.magic, pipeline.ORDER.magic, "BR-RULESET-019")
	assert(#touched == 0, "unexpected engine API use: " .. table.concat(touched, ","))
	-- one body per stage of the weapon order, in order; the magic order is declared without bodies (unverified, no golden data)
	assert(#stages == #pipeline.ORDER.weapon, "stage() calls: " .. #stages)
	for i, name in ipairs(pipeline.ORDER.weapon) do
		local s = stages[i]
		assert(s.kind == "weapon" and s.stage == name and type(s.fn) == "function", "stage " .. i .. " is " .. tostring(s.stage))
		assert((s.protected == true) == (name == "init" or name == "final"), "protected flag of " .. name)
	end
end)

T.run("BR-RULESET-019", "every stage of the weapon order has a body, and no body is orphaned", false, function()
	local seen = {}
	for _, name in ipairs(pipeline.ORDER.weapon) do
		assert(weapon.STAGES[name], "stage without body: " .. name)
		seen[name] = true
	end
	for name in pairs(weapon.STAGES) do assert(seen[name], "body for a stage that is not in the order: " .. name) end
end)

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

T.run("BR-RULESET-019", "pipeline.cancel: cancelling 'res' skips RES (1000 vs 840)", false, function()
	local res, trace = attack(function(pl) pl.cancel("weapon", "res") end)
	assert(res.damage == 1000)
	for _, n in ipairs(probe_names(trace)) do assert(n ~= "res", "the res stage still ran") end
end)

T.run("BR-RULESET-019", "pipeline.replace: a mod body replaces 'res' and receives the attack state", false, function()
	assert(attack(function(pl) pl.replace("weapon", "res", "mymod:res", function(S) S.dmg:add(-1) end) end).damage == 999)
end)

