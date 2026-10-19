--!nonstrict
-- Conformance of the declarative status layer against the REAL status_calc_* functions of the renewal legacy binary
-- (conformance/golden_gen/golden_status_hook.inc, conformance/golden/status_re.jsonl.gz): every recorded output of every case is compared, nothing
-- is tolerated. One test per group of functions (BR-RULESET-065..071, 073), per category of cases, per worked example.
local T = import("tests/support/harness")
local runner = import("tests/support/status_runner")
local S = import("scripts/status/init")

local golden = runner.load()
local rep = runner.run_all(golden.cases)
local meta = golden.meta

local function group(rule, title, fns, min_outputs)
	T.run(rule, title, false, function()
		local n, ok = 0, 0
		for _, fn in ipairs(fns) do
			local e = rep.by_fn[fn]
			if not e then error({ code = "NO_CASES", rule = rule, msg = fn }) end
			n += e.n
			ok += e.ok
		end
		if n < min_outputs then error({ code = "TOO_FEW", rule = rule, msg = n .. " outputs compared, need " .. min_outputs }) end
		if ok ~= n then
			local first = ""
			for _, b in ipairs(rep.bad) do
				for _, fn in ipairs(fns) do if b.res.outs[fn] and not b.res.outs[fn].ok then first = ("case %d %s"):format(b.case.id, b.res.detail or "") break end end
				if first ~= "" then break end
			end
			error({ code = "MISMATCH", rule = rule, msg = (n - ok) .. " of " .. n .. " differ; first: " .. first })
		end
	end)
end

-- ---- BR-RULESET-065: STR..CRT -----------------------------------------------------------------------------------------------------------
group("BR-RULESET-065", "stat contributions STR AGI VIT INT DEX LUK POW STA WIS SPL CON CRT equal status_calc_* (add / mul / override in code order, cap 0..65535)",
	{ "str", "agi", "vit", "int", "dex", "luk", "pow", "sta", "wis", "spl", "con", "crt" }, 15000)
group("BR-RULESET-066", "DEF and DEF2 contributions equal status_calc_def / status_calc_def2 (overrides first, RENEWAL allows a negative DEF2)", { "def", "def2" }, 6000)
group("BR-RULESET-067", "speed equals status_calc_speed: haste and slow are maxima, net rate 100 - haste + slow floored at 40, then cart, Paralysis, Steel Body, Defender", { "speed" }, 5000)
group("BR-RULESET-068", "ASPD (RENEWAL_ASPD) equals status_calc_aspd (fixed and percent groups), status_calc_fix_aspd and status_calc_aspd_rate",
	{ "aspd_fixed", "aspd_pct", "fix_aspd", "aspd_rate" }, 12000)
group("BR-RULESET-069", "BATK WATK HIT FLEE FLEE2 CRI MDEF MDEF2 MAXHP MAXSP MAXAP P.ATK S.MATK RES MRES H.PLUS C.RATE and flat MATK equal their status_calc_* functions",
	{ "batk", "watk", "matk_flat", "cri", "hit", "flee", "flee2", "mdef", "mdef2", "maxhp", "maxsp", "maxap", "patk", "smatk", "res", "mres", "hplus", "crate" }, 25000)
group("BR-RULESET-071", "statuses that override the element, element level, attack element and mode equal status_calc_element / _element_lv / _attack_element / _mode",
	{ "def_ele", "ele_lv", "atk_ele", "mode" }, 3500)
group("BR-RULESET-073", "restrictions (sc->cant) computed from the active set equal status_calc_state for all 1,025 sc_type values", { "states" }, 6500)

-- ---- by category: each case must match in full ---------------------------------------------------------------------------------------------
local cats = {}
for c in pairs(rep.by_cat) do cats[#cats + 1] = c end
table.sort(cats)
local RULE_OF_CAT = { single = "BR-RULESET-069", pair = "BR-RULESET-065", pair_site = "BR-RULESET-065", override = "BR-RULESET-065", combo = "BR-RULESET-069",
	extreme = "BR-RULESET-069", facts = "BR-RULESET-067", speed_facts = "BR-RULESET-067", clamp = "BR-RULESET-069", wrap = "BR-RULESET-069", worked = "BR-RULESET-067",
	states = "BR-RULESET-073", states_combo = "BR-RULESET-073" }
for _, cat in ipairs(cats) do
	local e = rep.by_cat[cat]
	T.run(RULE_OF_CAT[cat], "golden category " .. cat .. " (" .. e.n .. " cases) matches the legacy output case by case", false, function()
		if e.ok ~= e.n then error({ code = "MISMATCH", msg = (e.n - e.ok) .. " of " .. e.n .. " differ" }) end
	end)
end

T.run("BR-RULESET-069", "no golden case reaches an int32 overflow (undefined behaviour in the legacy C++) and the replay raised no error", false, function()
	assert(rep.overflow == 0, rep.overflow .. " overflow cases")
	assert(rep.matched == rep.total, (rep.total - rep.matched) .. " cases differ")
end)

-- ---- every status the legacy functions read has a declarative definition, and every defined site is exercised -------------------------------------------
T.run("BR-RULESET-069", "all statuses read by the RENEWAL status_calc_* functions (parsed from status.cpp) have a definition; the count is " .. #meta.referenced, false, function()
	assert(#meta.referenced == 317, "the parser found " .. #meta.referenced .. " referenced statuses")
	local missing = {}
	for _, sc in ipairs(meta.referenced) do
		local key = S.status.by_sc(sc)
		if not key or not S.status.get(key) then missing[#missing + 1] = sc end
	end
	assert(#missing == 0, "no definition for: " .. table.concat(missing, ", "))
end)

T.run("BR-RULESET-069", "every contribution site of every definition is applied by some golden case, and every conditional site is skipped by some case", false, function()
	local applied, skipped = runner.coverage(golden.cases)
	local never, never_skipped, sites = {}, {}, 0
	for _, name in ipairs(S.status.names()) do
		local def = S.status.get(name)
		for attr, list in pairs(def.contributions) do
			for _, c in ipairs(list) do
				sites += 1
				local k = name .. "|" .. attr .. "|" .. c.op .. "|" .. tostring(c.at)
				if not applied[k] then never[#never + 1] = k end
				if c.when and not skipped[k] then never_skipped[#never_skipped + 1] = k end
			end
		end
	end
	assert(sites >= 650, "only " .. sites .. " sites")
	assert(#never == 0, #never .. " sites never applied, first " .. tostring(never[1]))
	assert(#never_skipped == 0, #never_skipped .. " conditional sites never skipped, first " .. tostring(never_skipped[1]))
end)

-- ---- worked examples: the exact legacy numbers, pinned by name ------------------------------------------------------------------------------
local WORKED = {
	["E-4b speed 150 under Quagmire + Increase AGI"] = { speed = 187 },
	["speed haste group is a maximum, not a sum"] = { speed = 67 },
	["speed rate floor 40 percent"] = { speed = 60 },
	["Marsh of Abyss caps the slow at 150"] = { speed = 225 },
	["Steel Body speed 200"] = { speed = 200 },
	["Harmonize returns before everything"] = { str = 90, agi = 90 },
	["Blessing adds its val2"] = { str = 87, int = 87, dex = 87, hit = 120 },
	["Blessing halves the stat when val2 is 0"] = { str = 41, int = 40, dex = 40, hit = 120 },
	["Curse luk 0 even with Gloria"] = { luk = 0 },
	["stat order: add, then a rate term of the sum"] = { str = 123 },
	["Berserk and Eternal Chaos zero the defense"] = { def = 0, def2 = 0, mdef = 0, mdef2 = 0 },
	["Steel Body gives no DEF in renewal"] = { def = 50, mdef = 20 },
	["Provoke does not alter a player's DEF"] = { def = 50, def2 = 15 },
	["Provoke lowers a monster's DEF"] = { def = 25, def2 = 15 },
	["Defset above the int16 range wraps"] = { def = -32536, def2 = -32536 },
	["negative soft DEF is kept in renewal"] = { def2 = -120 },
	["ASPD: best of the quicken family"] = { aspd_fixed = 7 },
	["ASPD: Quagmire voids the quicken family"] = { aspd_fixed = 0 },
	["ASPD: only the best potion adds"] = { aspd_fixed = 9 },
	["ASPD: Berserk 15 after the quicken family"] = { aspd_fixed = 15 },
	["maxhp: fixed bonus first, then the rate"] = { maxhp = 12600 },
	["maxhp: a negative fixed bonus wraps around 2^64 and is capped"] = { maxhp = 4294967295 },
	["flee: Poison Mist returns 0, Overed Boost returns its value"] = { flee = 0 },
	["flee: Overed Boost is final"] = { flee = 77 },
	["flee: fixed terms before the rate terms"] = { flee = 87 },
	["mode: set, add, delete in that order"] = { mode = 5 },
	["element: Enchant Arms first, then water, earth, fire"] = { atk_ele = 7 },
	["element: Freeze sets water level 1"] = { def_ele = 1, ele_lv = 1 },
	["critical: Cloaking doubles the value"] = { cri = 140 },
	["states: Stone restrains moving, casting and attacking"] = { states = 265 },
	["states: Hiding restrains a monster's movement; the item restrictions are for players"] = { states = 1 },
	["states: Hiding without Tunnel Drive restrains a player's movement and items"] = { states = 131 },
	["states: Hiding with Tunnel Drive lets a player move"] = { states = 130 },
	["states: Gospel restrains only its caster (BCT_SELF)"] = { states = 1 },
	["states: Gospel of the enemy side does not restrain movement"] = { states = 0 },
	["states: an ensemble restrains movement only for Moonlit Serenade and Hermode"] = { states = 265 },
	["states: an ensemble of another song does not restrain movement"] = { states = 264 },
	["states: Camouflage below level 3 restrains movement"] = { states = 1 },
	["states: Camouflage level 3 does not"] = { states = 0 },
	["states: a manner penalty restrains chat and items of players only"] = { states = 150 },
	["states: a manner penalty on a monster restrains chat only"] = { states = 16 },
	["states: the union of the active statuses"] = { states = 265 },
}
local RULE_OF_WORKED = { speed = "BR-RULESET-067", str = "BR-RULESET-065", agi = "BR-RULESET-065", int = "BR-RULESET-065", dex = "BR-RULESET-065", luk = "BR-RULESET-065",
	def = "BR-RULESET-066", def2 = "BR-RULESET-066", mdef = "BR-RULESET-066", mdef2 = "BR-RULESET-066", aspd_fixed = "BR-RULESET-068", maxhp = "BR-RULESET-069",
	flee = "BR-RULESET-069", hit = "BR-RULESET-069", cri = "BR-RULESET-069", states = "BR-RULESET-073", mode = "BR-RULESET-071", atk_ele = "BR-RULESET-071", def_ele = "BR-RULESET-071" }
local seen = {}
for _, c in ipairs(golden.cases) do
	if c.cat == "worked" then
		local want = WORKED[c.note]
		seen[c.note] = (seen[c.note] or 0) + 1
		local first
		for fn in pairs(want or {}) do first = first or fn end
		T.run(RULE_OF_WORKED[first or "speed"], "worked example [" .. c.unit.kind .. "] " .. c.note, false, function()
			assert(want, "no pinned expectation for " .. c.note)
			local r = runner.run(c)
			for fn, v in pairs(want) do
				assert(c.out[fn] == v, ("the legacy binary recorded %s=%s, the test pins %s"):format(fn, tostring(c.out[fn]), tostring(v)))
				assert(r.outs[fn] and r.outs[fn].got == v, ("the Luau status layer gave %s=%s, expected %s"):format(fn, tostring(r.outs[fn] and r.outs[fn].got), tostring(v)))
			end
			assert(r.ok, r.detail)
		end)
	end
end
T.run("BR-RULESET-069", "every pinned worked example is present in the golden data", false, function()
	for name in pairs(WORKED) do assert(seen[name], "missing worked case: " .. name) end
end)

return true
