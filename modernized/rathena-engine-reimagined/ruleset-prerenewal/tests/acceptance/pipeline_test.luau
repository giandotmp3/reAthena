--!nonstrict
-- The engine-driven pipeline (pipeline.define / stage / run and the mod-facing insert / replace / cancel / move) with this package's
-- stage bodies. The Luau CLI has no script-host, so these run against the fake host (tests/support/fake_host); the same scenarios run
-- against the REAL pipeline runtime in script-host/tests/test_rulesets.cpp. Lua-subset source (also runs under plain Lua).
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

T.run("BR-RULESET-018", "register() defines all three orders, registers one body per weapon stage (init and final protected), none for magic or misc", false, function()
	local defined, stages, touched = {}, {}, {}
	local function strict(name, t)
		return setmetatable(t, { __index = function(_, k) touched[#touched + 1] = name .. "." .. tostring(k); error("package used " .. name .. "." .. tostring(k)) end })
	end
	local engine = strict("engine", {
		pipeline = strict("engine.pipeline", {
			define = function(kind, order) defined[kind] = order end,
			stage = function(kind, stage, fn, opts) stages[#stages + 1] = { kind = kind, stage = stage, fn = fn, protected = opts and opts.protected } end,
			run = function() end,
		}),
		num = strict("engine.num", { f32 = function(x) return x end }),
		config = strict("engine.config", { legacy_quirks = true }),
		status = { define = function() end }, skill = { define = function() end }, item = { define = function() end },
	})
	booted.isolated(function() init.register(engine) end) -- restores the binding the other tests use
	T.assert_deep_equal(defined.weapon, pipeline.ORDER.weapon, "BR-RULESET-018")
	T.assert_deep_equal(defined.magic, pipeline.ORDER.magic, "BR-RULESET-018")
	T.assert_deep_equal(defined.misc, pipeline.ORDER.misc, "BR-RULESET-018")
	assert(#touched == 0, "unexpected engine API use: " .. table.concat(touched, ","))
	assert(#stages == #pipeline.ORDER.weapon, "stage() calls: " .. #stages)
	for i, name in ipairs(pipeline.ORDER.weapon) do
		local s = stages[i]
		assert(s.kind == "weapon" and s.stage == name and type(s.fn) == "function", "stage " .. i .. " is " .. tostring(s.stage))
		assert((s.protected == true) == (name == "init" or name == "final"), "protected flag of " .. name)
	end
end)

T.run("BR-RULESET-018", "register() works against a host that offers no status / skill / item registry (script-host has none yet)", false, function()
	booted.isolated(function()
		local engine = fake_host()
		engine.status, engine.skill, engine.item = nil, nil, nil
		init.register(engine)
		assert(#engine.pipeline.order("weapon") == #pipeline.ORDER.weapon)
	end)
end)

T.run("BR-RULESET-018", "every stage of the weapon order has a body, and no body is orphaned", false, function()
	local seen = {}
	for _, name in ipairs(pipeline.ORDER.weapon) do
		assert(weapon.STAGES[name], "stage without body: " .. name)
		seen[name] = true
	end
	for name in pairs(weapon.STAGES) do assert(seen[name], "body for a stage that is not in the order: " .. name) end
end)

-- a pc with a plain 1000 ATK weapon (Maximize Power: no roll) hitting a 50% DEF monster: 1000 -> hard DEF 50% -> 500
local function mk()
	local a = unit.normalize({ kind = "pc", wtype1 = 0, hit = 500, sc = { maximize = { val1 = 1 } }, rhw = { atk = 1000, atk2 = 0, ele = 0, wlv = 0 } })
	local t = unit.normalize({ kind = "mob", def = 50, flee = 0, size = 1 })
	return a, t
end

local function attack(setup, config)
	local res, trace
	booted.isolated(function()
		local engine = fake_host(config)
		init.register(engine)
		if setup then setup(engine.pipeline) end
		local ctx = fake_ctx.new(scripted.lcg(7))
		local a, t = mk()
		trace = {}
		local ok, r = pcall(weapon.calc_attack, ctx, cfg, a, t, { trace = trace })
		assert(ok, type(r) == "table" and tostring(r.code) or tostring(r))
		res = r
	end)
	return res, trace
end
local function index(trace, name)
	for i, p in ipairs(trace) do if p[1] == name then return i end end
end

T.run("BR-RULESET-018", "the engine runs the registered order: 1000 ATK against 50% DEF leaves 500", false, function()
	local res = attack()
	assert(res.damage == 500 and res.dmg_lv == 5 and res.div == 1)
end)

T.run("BR-RULESET-018", "pipeline.cancel: cancelling 'defense' skips the DEF reduction (1000 vs 500) and its probes", false, function()
	local res, trace = attack(function(pl) pl.cancel("weapon", "defense") end)
	assert(res.damage == 1000)
	assert(index(trace, "defred") == nil, "the defense stage still ran")
end)

T.run("BR-RULESET-018", "pipeline.replace: a mod body replaces 'skill_ratio' and receives the attack state", false, function()
	local res = attack(function(pl) pl.replace("weapon", "skill_ratio", "mymod:ratio", function(S) S.dmg:set(400) end) end)
	assert(res.damage == 200) -- 400, then 50% DEF
end)

T.run("BR-RULESET-018", "pipeline.insert_before / insert_after: stages run on either side of their anchor", false, function()
	local res = attack(function(pl) pl.insert_after("weapon", "element", "mymod:fire_brand", function(S) S.dmg:add(10) end) end)
	assert(res.damage == 510)
	local seen = {}
	attack(function(pl)
		pl.insert_before("weapon", "element", "mymod:early", function(S) seen.early = #S.trace end)
		pl.insert_after("weapon", "element", "mymod:late", function(S) seen.late = #S.trace end)
	end)
	assert(seen.early and seen.late and seen.early < seen.late)
end)

T.run("BR-RULESET-018", "pipeline.move: moving 'defense' behind 'element' moves its probes behind the element probes", false, function()
	local base_res, base = attack(nil)
	local moved_res, moved = attack(function(pl) pl.move("weapon", "defense", { after = "element" }) end)
	assert(index(base, "defred") < index(base, "ele"), "baseline: DEF before the element table")
	assert(index(moved, "defred") > index(moved, "attr"), "moved: DEF after the element table")
	assert(base_res.damage == moved_res.damage and #base == #moved)
end)

T.run("BR-RULESET-018", "a missed attack skips the HIT_ONLY stages (skip set given to pipeline.run) but still runs the rest of the order", false, function()
	local a, t = mk()
	t.flee = 1000 -- hit rate falls to its floor; the scripted draw 99 is above it: a miss
	local trace = {}
	local res = weapon.calc_attack(fake_ctx.new(scripted.scripted({ 99 })), cfg, a, t, { trace = trace })
	assert(res.dmg_lv == 2 and res.damage == 0, "a flee miss yields no damage (ATK_FLEE)")
	assert(index(trace, "base") == nil and index(trace, "ratio") == nil and index(trace, "defred") == nil, "HIT_ONLY stages ran on a miss")
	assert(index(trace, "defense") ~= nil and index(trace, "final") ~= nil, "the stages after the hit block still ran")
end)

T.run("BR-RULESET-018", "probes of one attack follow the effective order (ORDER.weapon without edits)", false, function()
	local order_index = {}
	for i, n in ipairs(pipeline.ORDER.weapon) do order_index[n] = i end
	local stage_of = { base = "base_attack", ratio = "skill_ratio", defred = "defense", element = "element_bonus", card_atk = "card_attacker",
		card_tgt = "card_target", hands = "div_hands", map = "map_rates", final = "final" }
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
