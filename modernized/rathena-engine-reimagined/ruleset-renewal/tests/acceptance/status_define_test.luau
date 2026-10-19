--!nonstrict
-- The declarative status record and definition API (BR-RULESET-051), scripted statuses (075) and partial recalculation (077).
local T = import("tests/support/harness")
local S = import("scripts/status/init")
local status = S.status
local recalc = import("scripts/status/recalc")
local bonus_scripts = import("scripts/status/bonus_scripts")
local golden = import("tests/support/status_runner").load()

local function raises(fn, needle)
	local ok, err = pcall(fn)
	assert(not ok, "expected an error")
	local msg = type(err) == "table" and (tostring(err.code) .. " " .. tostring(err.msg)) or tostring(err)
	assert(msg:find(needle, 1, true), "error '" .. msg .. "' does not mention '" .. needle .. "'")
end

-- ---- BR-RULESET-051: the record --------------------------------------------------------------------------------------------------------
T.run("BR-RULESET-051", "the status records are data: 1,018 renewal records with id, states, calc flags, flags, min rate / duration", false, function()
	local n, with_states, with_calc, with_flags = 0, 0, 0, 0
	for _, rec in pairs(status.records()) do
		n += 1
		assert(type(rec.name) == "string" and type(rec.legacy_sc) == "number" and type(rec.min_rate) == "number" and type(rec.min_duration) == "number")
		if rec.states then with_states += 1 end
		if rec.calc_flags then with_calc += 1 end
		if rec.flags then with_flags += 1 end
	end
	assert(n == 1018, n .. " records")
	assert(with_states == 75 and with_calc == 554 and with_flags == 845, ("states %d calc %d flags %d"):format(with_states, with_calc, with_flags))
end)

T.run("BR-RULESET-051", "every record's legacy sc_type number equals the one of the legacy binary (golden meta), and SC_<name> round-trips to the record", false, function()
	local sc_ids = golden.meta.sc_ids
	for key, rec in pairs(status.records()) do
		assert(sc_ids["SC_" .. rec.name] == rec.legacy_sc, key .. ": sc_type " .. tostring(sc_ids["SC_" .. rec.name]) .. " vs record " .. rec.legacy_sc)
		assert(status.by_sc("SC_" .. rec.name) == key, key)
	end
end)

T.run("BR-RULESET-051", "behaviour is defined for 321 statuses (the 317 the status_calc_* functions read plus 4 that only have conditional states); 697 records are data only", false, function()
	assert(status.count() == 321, status.count())
	local defined, data_only = 0, 0
	for key in pairs(status.records()) do
		if status.get(key) then defined += 1 else data_only += 1 end
	end
	assert(defined == 321 and data_only == 697, defined .. " / " .. data_only)
end)

T.run("BR-RULESET-051", "Q-F: the val1..val4 slots have names per status; every slot an expression reads is named (a definition does not compile otherwise)", false, function()
	local named, slots = 0, 0
	for _, name in ipairs(status.names()) do
		local def = status.get(name)
		local any = false
		for i = 1, 4 do if def.fields[i] then any = true; slots += 1 end end
		if any then named += 1 end
		for attr, list in pairs(def.contributions) do
			for _, c in ipairs(list) do
				assert(c.at and c.stage and c.op, name .. "." .. attr)
			end
		end
	end
	assert(named == 247 and slots == 385, named .. " statuses with named fields, " .. slots .. " named slots")
end)

T.run("BR-RULESET-051", "a definition merges the behaviour with its record: states and flags come from the data unless overridden", false, function()
	local gospel = status.get("gospel")
	assert(gospel.record.name == "GOSPEL" and gospel.states[1] == "NOMOVECOND" and gospel.fields[4] == "target_type")
	local blessing = status.get("blessing")
	assert(blessing.record.legacy_sc == 33 or blessing.record.legacy_sc > 0)
	assert(#blessing.contributions.str == 2 and blessing.contributions.hit[1].op == "add")
end)

T.run("BR-RULESET-051", "status.define validates: unknown attribute, unknown op, missing `at`, duplicate name, bad field key, unknown identifier", false, function()
	raises(function() status.define("zz_bad1", { contributes = { nonsense = { "add", "1", at = 1 } } }) end, "unknown attribute")
	raises(function() status.define("zz_bad2", { contributes = { str = { "frobnicate", "1", at = 1 } } }) end, "no op")
	raises(function() status.define("zz_bad3", { contributes = { str = { "add", "1" } } }) end, "`at`")
	raises(function() status.define("blessing", {}) end, "already defined")
	raises(function() status.define("zz_bad4", { fields = { val5 = "x" } }) end, "val1..val4")
	raises(function() status.define("zz_bad5", { fields = { val1 = "x", val2 = "x" } }) end, "duplicate field")
	raises(function() status.define("zz_bad6", { contributes = { str = { "add", "undefined_name", at = 1 } } }) end, "unknown identifier")
	assert(status.get("zz_bad1") == nil and status.get("zz_bad6") == nil)
end)

T.run("BR-RULESET-051", "the expression language has the legacy integer semantics: truncating division, arithmetic shift, 0/1 comparisons, int32 overflow is an error", false, function()
	local expr = import("scripts/status/expr")
	local function ev(src, v) return expr.compile(src, { a = 1, b = 2 }, import("scripts/status/consts"))(v or { 0, 0, 0, 0 }, {}) end
	assert(ev("-7 / 2") == -3 and ev("7 / -2") == -3 and ev("-7 / -2") == 3)
	assert(ev("-17 >> 2") == -5 and ev("0x12345 >> 8 & 0xFF") == 0x23)
	assert(ev("3 < 4") == 1 and ev("3 >= 4") == 0 and ev("!0") == 1 and ev("1 || 0 && 0") == 1)
	assert(ev("a * 100 / b", { 3, 7, 0, 0 }) == 42 and ev("max(a, b)", { 3, 7, 0, 0 }) == 7 and ev("min(a, -b)", { 3, 7, 0, 0 }) == -7)
	assert(ev("2147483647") == 2147483647)
	raises(function() ev("2147483647 + 1") end, "INT32_OVERFLOW")
end)

-- ---- BR-RULESET-075: scripted status effects ------------------------------------------------------------------------------------------------
T.run("BR-RULESET-075", "all 168 renewal status scripts compile into bonus-delta lists (bonus statements, getstatus, if)", false, function()
	local n = 0
	for key, rec in pairs(status.records()) do
		if rec.script then
			n += 1
			local ok, err = pcall(bonus_scripts.compile, rec.script)
			assert(ok, key .. ": " .. tostring(type(err) == "table" and err.msg or err))
		end
	end
	assert(n == 168, n .. " scripts")
end)

T.run("BR-RULESET-075", "Speedup0 / Atkpotion contribute through their script: bonus bSpeedRate / bBaseAtk with the val1 of the active status", false, function()
	local active = { speedup0 = { 25, 0, 0, 0 }, atkpotion = { 90, 0, 0, 0 } }
	local F = S.aggregate.facts_for({ kind = "pc" }, active)
	local b = bonus_scripts.bonuses("speedup0", active.speedup0, F)
	assert(#b == 1 and b[1].name == "bSpeedRate" and b[1].args[1] == 25)
	b = bonus_scripts.bonuses("atkpotion", active.atkpotion, F)
	assert(#b == 1 and b[1].name == "bBaseAtk" and b[1].args[1] == 90)
	assert(bonus_scripts.bonuses("blessing", { 1, 0, 0, 0 }, F) == nil, "a status without a script yields no list")
end)

T.run("BR-RULESET-075", "a script keeps its conditions: Life Potion bonuses vanish under Berserk; variables and symbols (Ele_*, RC_*) are carried", false, function()
	local F1 = S.aggregate.facts_for({ kind = "pc" }, { s_lifepotion = { 3, 4, 0, 0 } })
	local b = bonus_scripts.bonuses("s_lifepotion", { 3, 4, 0, 0 }, F1)
	assert(#b == 1 and b[1].name == "bRegenPercentHP" and b[1].args[1] == -3 and b[1].args[2] == 4000, "args " .. table.concat(b[1].args, ","))
	local F2 = S.aggregate.facts_for({ kind = "pc" }, { s_lifepotion = { 3, 4, 0, 0 }, berserk = { 1, 0, 0, 0 } })
	assert(#bonus_scripts.bonuses("s_lifepotion", { 3, 4, 0, 0 }, F2) == 0)
	local F3 = S.aggregate.facts_for({ kind = "pc" }, { armor_element_water = { 10, 20, 30, 40 } })
	b = bonus_scripts.bonuses("armor_element_water", { 10, 20, 30, 40 }, F3)
	assert(b[1].name == "bSubEle" and b[1].args[1] == "Ele_Water" and b[1].args[2] == 10 and b[4].args[2] == 40)
end)

-- ---- BR-RULESET-077: partial, guarded recalculation ---------------------------------------------------------------------------------------
T.run("BR-RULESET-077", "calc flags select the attributes to recompute; a stat flag sets the flags of the stats derived from it (status_calc_bl_main)", false, function()
	local names = { "str", "batk", "watk", "flee", "def2", "aspd_pct", "maxhp", "maxsp", "hit", "matk_flat", "res", "patk" }
	local function has(list, a) for _, x in ipairs(list) do if x == a then return true end end return false end
	local pc = recalc.attributes({ STR = true }, "pc", names)
	assert(has(pc, "str") and has(pc, "batk") and not has(pc, "watk") and not has(pc, "flee"))
	local hom = recalc.attributes({ STR = true }, "hom", names)
	assert(has(hom, "watk"), "a homunculus recomputes WATK from STR")
	local agi = recalc.attributes({ AGI = true }, "pc", names)
	assert(has(agi, "flee") and has(agi, "def2") and has(agi, "aspd_pct") and not has(agi, "maxhp"))
	assert(not has(recalc.attributes({ AGI = true }, "mob", names), "aspd_pct"), "ASPD follows AGI for players and homunculi only")
	local vit = recalc.attributes({ VIT = true }, "pc", names)
	assert(has(vit, "def2") and has(vit, "maxhp") and not has(recalc.attributes({ VIT = true }, "mob", names), "maxhp"))
	local luk = recalc.attributes({ LUK = true }, "mob", names)
	assert(has(luk, "hit") and has(luk, "flee") and has(luk, "batk"), "renewal: LUK feeds HIT and FLEE")
	assert(has(recalc.attributes({ CON = true }, "pc", names), "patk") and has(recalc.attributes({ STA = true }, "pc", names), "res"))
end)

T.run("BR-RULESET-077", "the nesting guard counts calls, refuses the 11th, and a completed nested recalculation resets the counter so the outer one aborts", false, function()
	local g = recalc.new_guard()
	for i = 1, 10 do assert(g.enter(), "call " .. i .. " must run") end
	assert(not g.enter(), "the 11th nested call is refused (returns -1)")
	assert(not g.outer_must_abort())
	g.finish()
	assert(g.outer_must_abort(), "calculating == 0 after a nested call finished: the outer call returns 1")
	assert(g.enter())
end)

T.run("BR-RULESET-077", "a player with delayed damage holds the recalculation unless it is forced; other units never hold", false, function()
	assert(recalc.should_hold({ kind = "pc", delayed_damage = 5 }, false))
	assert(not recalc.should_hold({ kind = "pc", delayed_damage = 5 }, true))
	assert(not recalc.should_hold({ kind = "pc", delayed_damage = 0 }, false))
	assert(not recalc.should_hold({ kind = "mob", delayed_damage = 5 }, false))
end)

T.run("BR-RULESET-077", "behaviour versus record: 15 contributions are not covered by their own record's calc flags (status.yml gaps, even after the cascade)", false, function()
	local gaps, statuses = 0, 0
	local expect = {
		angriffs_modus = "watk", attack_stance = "patk+smatk", beyondofwarcry = "cri", burning = "mdef2", camouflage = "def+def2", crossbowclan = "dex",
		defset = "def2", mdefset = "mdef2", partyflee = "flee", tidal_weapon = "atk_ele+watk", invisibility = "ele_lv",
	}
	for _, name in ipairs(status.names()) do
		local u = recalc.uncovered(status.get(name), "pc")
		if #u > 0 then
			statuses += 1
			gaps += #u
			assert(expect[name] == table.concat(u, "+"), name .. ": " .. table.concat(u, "+"))
		end
	end
	assert(statuses == 11 and gaps == 14, statuses .. " statuses, " .. gaps .. " attributes")
	-- the one player/monster difference: Marionette's HP change is reachable from VIT for players (MAXHP follows VIT) but not for monsters
	assert(#recalc.uncovered(status.get("marionette"), "pc") == 0 and recalc.uncovered(status.get("marionette"), "mob")[1] == "hp_fix")
end)

return true
