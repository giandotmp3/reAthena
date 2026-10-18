--!nonstrict
-- Architecture-proof checks that live on the Luau side: what register() asks of the engine, that every stage name of the order has a
-- body, and that the stage-body table is a usable replace / insert / cancel surface (the semantics of engine.pipeline.* applied to
-- this package's own runner). The cross-package numbers (shared lines, op counts versus the pre-renewal order) are computed by
-- tools/diff_report.py and checked from Python.
local T = import("tests/support/harness")
local pipeline = import("scripts/pipeline")
local init = import("scripts/init")
local weapon = import("scripts/damage/weapon")
local unit = import("scripts/damage/unit")
local cfg = import("data/damage_cfg")
local fake_ctx = import("tests/support/fake_ctx")
local scripted = import("tests/support/scripted_rng")

T.run("BR-RULESET-019", "register() defines the weapon and magic order through engine.pipeline.define and needs nothing else", false, function()
	local defined, touched = {}, {}
	-- an engine that offers ONLY pipeline.define: any other API access raises
	local engine = setmetatable({
		pipeline = setmetatable({ define = function(kind, order) defined[kind] = order end }, {
			__index = function(_, k) touched[#touched + 1] = k; error("package used engine.pipeline." .. tostring(k)) end,
		}),
	}, { __index = function(_, k) touched[#touched + 1] = k; error("package used engine." .. tostring(k)) end })
	init.register(engine)
	T.assert_deep_equal(defined.weapon, pipeline.ORDER.weapon, "BR-RULESET-019")
	T.assert_deep_equal(defined.magic, pipeline.ORDER.magic, "BR-RULESET-019")
	assert(#touched == 0, "unexpected engine API use: " .. table.concat(touched, ","))
end)

T.run("BR-RULESET-019", "every stage of the weapon order has a body, and no body is orphaned", false, function()
	local seen = {}
	for _, name in ipairs(pipeline.ORDER.weapon) do
		assert(weapon.STAGES[name], "stage without body: " .. name)
		seen[name] = true
	end
	for name in pairs(weapon.STAGES) do assert(seen[name], "body for a stage that is not in the order: " .. name) end
end)

local function attack(opts)
	local ctx = fake_ctx.new(scripted.lcg(7))
	local a = unit.normalize({ kind = "pc", wtype1 = 0, hit = 500, str = 1, dex = 1, rhw = { atk = 1000, atk2 = 0, ele = 0, wlv = 0 } })
	local t = unit.normalize({ kind = "mob", res = 100, flee = 0, size = 1 })
	return weapon.calc_attack(ctx, cfg, a, t, opts)
end

T.run("BR-RULESET-019", "pipeline.cancel semantics: running the order without 'res' skips RES (1000 vs 840)", false, function()
	assert(attack({}).damage == 840)
	local order = {}
	for _, n in ipairs(pipeline.ORDER.weapon) do if n ~= "res" then order[#order + 1] = n end end
	assert(attack({ order = order }).damage == 1000)
end)

T.run("BR-RULESET-019", "pipeline.replace / insert_after semantics: a mod body replaces 'res'; an inserted stage runs where it was put", false, function()
	local stages = setmetatable({}, { __index = weapon.STAGES })
	stages.res = function(S) S.dmg:add(-1) end -- replace
	assert(attack({ stages = stages }).damage == 999)
	local order, extra = {}, setmetatable({}, { __index = weapon.STAGES })
	for _, n in ipairs(pipeline.ORDER.weapon) do
		order[#order + 1] = n
		if n == "element" then order[#order + 1] = "mymod:fire_brand" end -- insert_after("weapon", "element", ...)
	end
	extra["mymod:fire_brand"] = function(S) S.dmg:add(10) end
	assert(attack({ order = order, stages = extra }).damage == 850) -- 840 after RES, +10
end)
