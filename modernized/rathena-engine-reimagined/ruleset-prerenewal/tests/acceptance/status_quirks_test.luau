--!nonstrict
-- Legacy quirks and findings of the status calculation, each pinned twice: the REAL legacy result is in the golden data, and the Luau value must
-- equal it. A quirk is listed in README.md / docs; this file is where it is checked. (extra suite)
local T = import("tests/support/harness")
local runner = import("tests/support/status_golden_runner")
local status = import("scripts/status/index")
local K = import("scripts/status/consts")

local g = runner.load()
local cfg = g.consts.cfg

local function case(tag)
	for _, c in ipairs(g.cases) do
		if c.tag == tag then return c end
	end
	error("no golden case tagged " .. tag)
end
local function luau(c) return runner.compute(c, K, cfg) end

local function quirk(rule, tag, title, want)
	T.run(rule, title, false, function()
		local c = case(tag)
		assert(c.out == want, "the legacy recorded " .. tostring(c.out) .. ", expected " .. tostring(want))
		assert(luau(c) == want, "the Luau value is " .. tostring(luau(c)))
	end)
end

quirk("BR-RULESET-066", "R066-berserk", "Berserk returns def2 0: the PRE clamp to >= 1 does not apply to early returns (status.cpp:7887-7888)", 0)
quirk("BR-RULESET-066", "R066-floor", "def2 without statuses is clamped up to 1 (status.cpp:7884)", 1)
quirk("BR-RULESET-066", "Q-defset-narrowing", "Def Set returns val1 unclamped through a signed char: val1 40000 gives DEF 64 (status.cpp:7792-7793)", 64)
quirk("BR-RULESET-066", "Q-stone-def-truncation", "Stone halves a negative DEF toward zero (-7 / 2 = -3, not -4; status.cpp:7813-7814)", -3)
quirk("BR-RULESET-069", "Q-stone-mdef-truncation", "Stone adds 25% of a negative MDEF toward zero (-7 + 25 * -7 / 100 = -8; status.cpp:7971-7972)", -8)
quirk("BR-RULESET-069", "Q-overed-boost-narrowing", "Overed Boost returns val2 through an int16: 40000 gives FLEE -25536 (status.cpp:7648-7649)", -25536)
quirk("BR-RULESET-065", "Q-harmonize-below-zero", "Harmonize subtracts val2 from the stat and returns it clamped at 0 (status.cpp:6779-6780)", 0)
quirk("BR-RULESET-069", "Q-marionette-wrap", "Marionette's -1000 flat max HP on a unit with less than 1000 HP wraps (uint64) and the cap gives UINT_MAX, not 1 (status.cpp:8737)", 4294967295)
quirk("BR-RULESET-069", "Q-marionette-ok", "Marionette's -1000 flat max HP on a unit with 5000 HP is an ordinary subtraction", 4000)

T.run("BR-RULESET-077", "calc flags select what is recalculated: DEX and FLEE flagged, the other fields keep their stale values (golden chain R077-partial)", false, function()
	local c = case("R077-partial")
	local out = luau(c)
	for _, f in ipairs(runner.CHAIN_FIELDS) do assert(out[f] == c.out[f], f .. ": luau " .. tostring(out[f]) .. " legacy " .. tostring(c.out[f])) end
	-- DEX flagged -> dex, and by propagation BATK and HIT; FLEE flagged. Nothing else is touched.
	assert(c.out.hit ~= 999 and c.out.flee ~= 777 and c.out.dex ~= 70, "flagged fields were not recalculated")
	assert(c.out.str == c.b.str and c.out.agi == c.b.agi and c.out.def2 == c.b.def2 and c.out.speed == c.b.speed and c.out.mdef == c.b.mdef,
		"fields that were not flagged changed")
end)

T.run("BR-RULESET-077", "stats are stored with the width of status_data: a calculated stat above 32767 is stored negative (int16), the cap is 65535 only inside the function", false, function()
	local n = 0
	for _, c in ipairs(g.cases) do
		if c.cat == "chain_extreme" then
			for _, f in ipairs({ "str", "agi", "vit", "int", "dex", "luk" }) do
				if c.out[f] < 0 then n = n + 1 end
			end
		end
	end
	assert(n >= 10, "expected narrowed (negative) stats in the extreme chain cases, found " .. n)
end)

T.run("BR-RULESET-077", "recalculation guard: 10 nested levels pass, the 11th aborts; the legacy resets the counter instead of decrementing it", false, function()
	local recalc = import("scripts/status/recalc")
	local g1 = recalc.new(true)
	for i = 1, 10 do assert(recalc.enter(g1), "level " .. i .. " must run") end
	assert(recalc.enter(g1) == false, "the 11th nested level must abort")
	-- legacy_quirks: an inner call that finishes resets the counter, so the outer calls are no longer counted
	local q = recalc.new(true)
	recalc.enter(q); recalc.enter(q); recalc.enter(q)
	recalc.leave(q)
	assert(q.depth == 0, "legacy: calculating = 0 at the normal exit")
	local fixed = recalc.new(false)
	recalc.enter(fixed); recalc.enter(fixed); recalc.enter(fixed)
	recalc.leave(fixed)
	assert(fixed.depth == 2, "corrected: the counter is decremented")
	assert(recalc.hold(5, false) == true and recalc.hold(5, true) == false and recalc.hold(0, false) == false, "delayed damage holds an unforced recalculation")
end)

-- ---- BR-RULESET-077 audit: a status that contributes to an attribute its database calc flags do not list is only picked up when
-- something else triggers that attribute's recalculation. The propagation of status_calc_bl_main is taken into account (STR -> BATK, AGI -> FLEE
-- ..., VIT -> DEF2 / MDEF2 / MAXHP, INT -> MATK / MDEF2 / MAXSP, DEX -> BATK / HIT, LUK -> BATK / CRI / FLEE2).
local ATTR_FLAG = { str = "STR", agi = "AGI", vit = "VIT", int = "INT", dex = "DEX", luk = "LUK", batk = "BATK", watk = "WATK", hit = "HIT", cri = "CRI",
	flee = "FLEE", flee2 = "FLEE2", def = "DEF", def2 = "DEF2", mdef = "MDEF", mdef2 = "MDEF2", speed = "SPEED", aspd_rate = "ASPD", fix_aspd = "ASPD",
	patk = "PATK", smatk = "SMATK", res = "RES", mres = "MRES", hplus = "HPLUS", crate = "CRATE", pow = "POW", sta = "STA", wis = "WIS", spl = "SPL",
	con = "CON", crt = "CRT", element = "DEF_ELE", element_lv = "DEF_ELE", atk_element = "ATK_ELE", mode = "MODE", hp_fix = "MAXHP", hp_rate = "MAXHP",
	sp_fix = "MAXSP", sp_rate = "MAXSP", matk = "MATK" }
local DERIVES = { STR = { "BATK" }, AGI = { "FLEE", "ASPD" }, VIT = { "DEF2", "MDEF2", "MAXHP" }, INT = { "MATK", "MDEF2", "MAXSP" },
	DEX = { "BATK", "HIT", "ASPD" }, LUK = { "BATK", "CRI", "FLEE2" }, ALL = {} }
T.run("BR-RULESET-077", "audit: the contributions that the status' calc flags do not trigger are exactly the documented set (legacy data defects)", false, function()
	local bad = {}
	for _, ent in ipairs(status.all_entries()) do
		local d = status.get(ent.owner or "")
		local flag = ATTR_FLAG[ent.attr]
		if d and flag and d.record and ent.owner then
			local have = {}
			for _, f in ipairs(d.calc_flags) do
				have[f] = true
				for _, x in ipairs(DERIVES[f] or {}) do have[x] = true end
			end
			if not have[flag] then bad[ent.owner .. "." .. ent.attr] = true end
		end
	end
	local want = { ["ANGRIFFS_MODUS.watk"] = true, ["BEYONDOFWARCRY.cri"] = true, ["BURNING.mdef2"] = true, ["CAMOUFLAGE.def"] = true, ["CAMOUFLAGE.def2"] = true,
		["CROSSBOWCLAN.dex"] = true, ["DEFSET.def2"] = true, ["MDEFSET.mdef2"] = true, ["PARTYFLEE.flee"] = true, ["PYROCLASTIC.watk"] = true,
		["TIDAL_WEAPON.watk"] = true, ["_INVISIBILITY.element_lv"] = true, ["_AUTOSHADOWSPELL.matk"] = true }
	local missing, extra = {}, {}
	for k in pairs(want) do if not bad[k] then missing[#missing + 1] = k end end
	for k in pairs(bad) do if not want[k] then extra[#extra + 1] = k end end
	table.sort(missing); table.sort(extra)
	assert(#missing == 0 and #extra == 0, "audit changed: now fixed: " .. table.concat(missing, " ") .. " / new: " .. table.concat(extra, " "))
end)
