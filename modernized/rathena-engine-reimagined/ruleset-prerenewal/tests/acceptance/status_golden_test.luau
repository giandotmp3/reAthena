--!nonstrict
-- Status calculation against the REAL legacy code (extra suite). conformance/golden/status_pre.jsonl.gz holds what the real
-- status_calc_* functions of a strict pre-renewal build returned for synthetic units (conformance/golden_gen/gen_status_cases.py);
-- here every case is replayed through the pure Luau aggregator and every output is compared. Nothing is tolerant: one differing output
-- fails the function's test. Also checks what the golden data lets us check about the definitions: constants, the status database
-- records, the field names, the expressibility in the engine attrs model, the coverage of the contributions.
local T = import("tests/support/harness")
local runner = import("tests/support/status_golden_runner")
local slices = import("tests/support/status_rule_slices")
local cases_status = import("conformance/cases/status")
local status = import("scripts/status/index")
local aggregate = import("scripts/status/aggregate")
local engine_model = import("scripts/status/engine_model")
local fake_ctx = import("tests/support/fake_ctx")
local K = import("scripts/status/consts")

local TAG = "GOLDEN-STATUS-PRE"
local g = runner.load()

-- ---- replay: one test per function (all its cases must match), the BR rule it belongs to in the title
local RULE_OF = {
	str = "BR-RULESET-065", agi = "BR-RULESET-065", vit = "BR-RULESET-065", int = "BR-RULESET-065", dex = "BR-RULESET-065", luk = "BR-RULESET-065",
	def = "BR-RULESET-066", def2 = "BR-RULESET-066", speed = "BR-RULESET-067", aspd_rate = "BR-RULESET-068", fix_aspd = "BR-RULESET-068",
	element = "BR-RULESET-071", element_lv = "BR-RULESET-071", atk_element = "BR-RULESET-071", mode = "BR-RULESET-071",
	state = "BR-RULESET-073", script = "BR-RULESET-075", chain = "BR-RULESET-077",
}
local order, by_fn = {}, {}
for _, c in ipairs(g.cases) do
	if not by_fn[c.fn] then by_fn[c.fn] = {}; order[#order + 1] = c.fn end
	local list = by_fn[c.fn]
	list[#list + 1] = c
end
table.sort(order)
local rep = runner.run_all(g)
for _, fn in ipairs(order) do
	local n = #by_fn[fn]
	T.run(RULE_OF[fn] or "BR-RULESET-069", "status golden: function " .. fn .. " (" .. n .. " cases) matches the legacy output", false, function()
		local x = rep.by_fn[fn]
		if x.matched ~= x.total then
			local first
			for _, m in ipairs(rep.mismatches) do
				if m.fn == fn then first = m; break end
			end
			error({ code = "MISMATCH", rule = TAG, msg = (x.total - x.matched) .. " of " .. x.total .. " differ; first: case " .. tostring(first and first.id) ..
				" luau=" .. tostring(first and first.got) .. " legacy=" .. tostring(first and first.want) })
		end
	end)
end

T.run(TAG, "status golden: all cases match (>= 5000 cases, 0 runtime errors)", false, function()
	assert(rep.total >= 5000, "fewer than 5000 golden cases: " .. rep.total)
	assert(rep.errors == 0, rep.errors .. " cases raised")
	assert(rep.matched == rep.total, rep.total - rep.matched .. " of " .. rep.total .. " cases differ")
end)

-- ---- every converted rule case is cut from its golden record
for _, case in ipairs(cases_status) do
	local sl = slices[case.rule]
	if sl and case.golden ~= nil then
		T.run(case.rule, "case is derived from the status golden record " .. tostring(case.golden), false, function()
			local rec = sl.find(g, case.golden)
			assert(rec, "status golden record " .. tostring(case.golden) .. " not found")
			T.assert_deep_equal(case.given, sl.given(g, rec), case.rule)
			T.assert_deep_equal(case.expected, sl.expected(g, rec), case.rule)
		end)
	end
end

-- ---- the Luau constants are the ones the legacy build reports
T.run(TAG, "scripts/status/consts.luau equals the constants, battle.conf defaults and script constants the legacy build reported", false, function()
	for k, v in pairs(g.consts.K) do assert(K[k] == v, "K." .. k .. ": luau " .. tostring(K[k]) .. " legacy " .. tostring(v)) end
	for k, v in pairs(g.consts.cfg) do assert(K.CFG[k] == v, "CFG." .. k) end
	for k, v in pairs(g.consts.scriptk) do assert(K.SCRIPT[k] == v, "SCRIPT." .. k) end
	for k, v in pairs(g.consts.sc) do assert(K.SC[k] == v, "SC." .. k) end
	for k, v in pairs(g.consts.skdb) do assert(K.SKILL_IN_PRE_DB[k] == (v == 1), "SKILL_IN_PRE_DB." .. k) end
end)

-- ---- BR-RULESET-051: the 699 status records equal the status database of the real loader
-- (`CalcFlags: All: true` sets the whole bitset in the legacy loader, bit 0 = SCB_NONE included; the migrated record lists it as "NONE",
-- the dump of the hook leaves it out: bit 0 means nothing)
local function same_set(a, b)
	local function drop_none(l)
		local o = {}
		for _, v in ipairs(l or {}) do
			if v ~= "NONE" then o[#o + 1] = v end
		end
		return o
	end
	a, b = drop_none(a), drop_none(b)
	local m = {}
	for _, v in ipairs(a or {}) do m[v] = (m[v] or 0) + 1 end
	for _, v in ipairs(b or {}) do m[v] = (m[v] or 0) - 1 end
	for _, n in pairs(m) do if n ~= 0 then return false end end
	return #(a or {}) == #(b or {})
end
T.run("BR-RULESET-051", "all 699 status records (states, calc flags, flags, fail / end lists, min rate / duration, script) equal the real loader's database", false, function()
	local n = 0
	for name, d in pairs(g.db) do
		local def = status.get(name)
		assert(def and def.record, "no record for " .. name)
		local r = def.record
		local valid, why = status.validate_record(def)
		assert(valid, name .. ": " .. tostring(why))
		assert(r.sc == g.consts.sc[name], name .. ": sc id " .. tostring(r.sc) .. " vs " .. tostring(g.consts.sc[name]))
		assert(same_set(r.states, d.state), name .. ": states")
		assert(same_set(r.calc, d.calc), name .. ": calc flags")
		assert(same_set(r.flags, d.flag), name .. ": flags")
		for _, k in ipairs({ "fail", "end_on_start", "end_return", "end_on_end" }) do assert(same_set(r[k], d[k]), name .. ": " .. k) end
		assert(r.min_rate == d.min_rate and r.min_duration == d.min_duration, name .. ": min rate / duration")
		assert((r.script ~= nil) == (d.script == 1), name .. ": script")
		n = n + 1
	end
	assert(n == 699, "expected 699 database records, got " .. n)
end)

-- ---- BR-RULESET-051 / Q-F: field names
T.run("BR-RULESET-051", "every covered status names its val1..val4 (Q-F); named fields are unique and reach the contributions", false, function()
	local covered = 0
	for _, name in ipairs(status.names()) do
		local d = status.get(name)
		if not d.record_only then
			covered = covered + 1
			for n = 1, 4 do assert(type(d.fields[n]) == "string", name .. ".val" .. n .. " has no name") end
		end
	end
	assert(covered >= 319, "fewer than 319 covered statuses: " .. covered)
end)

-- ---- the engine attrs model: the same value from {source, tag, value} records and five stage functions
T.run("BR-RULESET-065", "expressible in engine-core attrs: Contribution{source, tag, value} + ruleset stage list gives the same value (standard attributes, aspd_rate)", false, function()
	local ctx = fake_ctx.new({})
	local checked = 0
	for _, c in ipairs(g.cases) do
		local attr = c.fn
		if (attr ~= "state" and attr ~= "chain" and attr ~= "script" and attr ~= "maxhp" and attr ~= "maxsp" and attr ~= "speed" and attr ~= "mode"
			and not attr:match("^[hs]p_")) then
			local e = aggregate.env(ctx, runner.unit_of(c, K), runner.active_of(c), setmetatable(c.cfg or {}, { __index = g.consts.cfg }))
			local v, span = engine_model.value(e, attr, c.base)
			assert(v == c.out, "case " .. c.id .. " " .. attr .. ": engine model " .. tostring(v) .. " legacy " .. tostring(c.out))
			for _, r in ipairs(span) do
				assert(type(r.source) == "string" and type(r.tag) == "string" and type(r.value) == "number", "not a flat Contribution")
			end
			checked = checked + 1
		end
	end
	assert(checked >= 4000, "only " .. checked .. " cases checked")
end)

-- ---- coverage: every contribution fires in some case, except the homunculus-only ones
T.run(TAG, "contribution coverage: every non-homunculus contribution fires in at least one golden case", false, function()
	aggregate.cover = {}
	runner.run_all(g)
	local cover = aggregate.cover
	aggregate.cover = nil
	local HOM_ONLY = { ["DEFENCE.def@7802"] = true, ["OVERED_BOOST.def@7854"] = true, ["PYROCLASTIC.batk@7332"] = true }
	local unhit = {}
	for _, ent in ipairs(status.all_entries()) do
		local key = string.format("%s.%s@%d", ent.owner or "(rule)", ent.attr, ent.at)
		if not cover[ent] and not HOM_ONLY[key] then unhit[#unhit + 1] = key end
	end
	assert(#unhit == 0, "never fired: " .. table.concat(unhit, " "))
end)
