--!nonstrict
-- legacy_quirks (HITL #2: legacy defects ship as-is behind a flag, default on). The pre-renewal weapon pipeline has ONE of the two
-- defects the renewal proof found: weapon_damage_rate has 16 slots but is indexed by weapon type up to 23 (pc.hpp:577), so guns read
-- arrow_addele[type - 16]. The other one, the doubled monster crit multiplier, is #ifdef RENEWAL in the legacy code (battle.cpp:2635)
-- and does not exist here. The weapon_damage_rate step itself (battle_add_weapon_damage) was missing from this package before: the
-- golden data never exercises it (no case sets a wdr bonus, no gun case sets arrow_addele), so the expectations below are derived
-- from battle.cpp:2411-2429 and the renewal golden (the same function), not recorded from a PRE build. Lua-subset source.
local T = import("tests/support/harness")
local booted = import("tests/support/booted")
local weapon = import("scripts/damage/weapon")
local unit = import("scripts/damage/unit")
local cfg_defaults = import("data/damage_cfg")
local fake_ctx = import("tests/support/fake_ctx")
local scripted = import("tests/support/scripted_rng").scripted
local runner = import("tests/support/golden_runner")

local golden = runner.load()

local function run_case(c, legacy_quirks)
	local cfg = {}
	for k, v in pairs(cfg_defaults) do cfg[k] = v end
	for k, v in pairs(c.cfg) do cfg[k] = v end
	cfg.legacy_quirks = legacy_quirks
	local rng = scripted(c.out.draws)
	local res = weapon.calc_attack(fake_ctx.new(rng), cfg, unit.normalize(c.a), unit.normalize(c.t), { map = c.map })
	return res, rng
end

T.run("BR-RULESET-118", "legacy_quirks defaults to true in the package config", false, function()
	assert(cfg_defaults.legacy_quirks == true)
end)

-- a crit takes the weapon's max ATK (no roll): rifle ATK 100, no size fix, batk 0
local function gun(bonus, wtype, lhw)
	return unit.normalize({ kind = "pc", wtype1 = wtype or 18, level = 1, no_sizefix = true, rhw = { atk = 100, atk2 = 0, ele = 0, wlv = 1 }, bonus = bonus })
end
local function base(src, quirks)
	return weapon.base_damage(fake_ctx.new(scripted({})), src, "r", 1, true, false, true, { cfg = { legacy_quirks = quirks } })
end

T.run("BR-RULESET-008", "weapon_damage_rate, legacy_quirks = true: a rifle (type 18) reads arrow_addele[2] = +20% instead of its own slot (100 -> 120)", false, function()
	assert(base(gun({ aadd_ele = { [2] = 20 }, wdr_r = -10 }), true) == 120)
	assert(base(gun({ wdr_r = 30 }), true) == 100, "legacy: the own slot is not read for types >= 16")
	assert(weapon.base_damage(fake_ctx.new(scripted({})), gun({ aadd_ele = { [2] = 20 } }), "r", 1, true, false, true, nil) == 120,
		"without an attack state the legacy behaviour applies")
end)

T.run("BR-RULESET-008", "weapon_damage_rate, legacy_quirks = false: the rifle uses its own slot (100 -> 90 for -10%); arrow_addele no longer leaks", false, function()
	assert(base(gun({ aadd_ele = { [2] = 20 }, wdr_r = -10 }), false) == 90)
	assert(base(gun({ aadd_ele = { [2] = 20 } }), false) == 100, "no weapon_damage_rate bonus: unchanged")
	assert(base(gun({ wdr_r = 30 }), false) == 130)
end)

T.run("BR-RULESET-008", "weapon_damage_rate: types below 16 read their own slot in both modes; it follows batk and the over-refine roll", false, function()
	local sword = gun({ aadd_ele = { [2] = 20 }, wdr_r = 10 }, 2)
	assert(base(sword, true) == 110 and base(sword, false) == 110)
	sword.batk = 5 -- (100 + 5) + 10% = 105 + 10 (10.5 truncated)
	assert(base(sword, true) == 115 and base(sword, false) == 115)
	local rng = scripted({ 3 }) -- over-refine 4: + rnd() % 4 + 1 = 4, THEN the rate: (105 + 4) + 10% = 109 + 10
	local o = gun({ overrefine_r = 4, wdr_r = 10 }, 2)
	o.batk = 5
	assert(weapon.base_damage(fake_ctx.new(rng), o, "r", 1, true, false, true, { cfg = { legacy_quirks = true } }) == 119)
end)

T.run("BR-RULESET-008", "weapon_damage_rate: every gun type (17..21) aliases arrow_addele[type - 16] under the quirk and reads wdr_r without it", false, function()
	for wtype = 17, 21 do
		local ele = {}
		ele[wtype - 16] = 50
		local src = gun({ aadd_ele = ele, wdr_r = -20 }, wtype)
		assert(base(src, true) == 150, "legacy type " .. wtype .. " got " .. base(src, true))
		assert(base(src, false) == 80, "corrected type " .. wtype .. " got " .. base(src, false))
	end
end)

T.run("BR-RULESET-008", "weapon_damage_rate: the left hand reads the slot of weapon type 2 (shared with the right hand when both are one type)", false, function()
	local src = gun({ wdr_r = 10, wdr_l = 50 }, 2)
	src.wtype2 = 1
	src.lhw = { atk = 100, atk2 = 0, ele = 0, wlv = 1 }
	local ctx = fake_ctx.new(scripted({}))
	local S = { cfg = { legacy_quirks = false } }
	assert(weapon.base_damage(ctx, src, "r", 1, true, false, true, S) == 110)
	assert(weapon.base_damage(ctx, src, "l", 1, true, false, true, S) == 150)
	src.wtype2 = 2 -- the same type in both hands: one slot
	assert(weapon.base_damage(ctx, src, "l", 1, true, false, true, S) == 110)
end)

T.run("BR-RULESET-118", "legacy_quirks = false changes nothing the golden data exercises: every case is identical in both modes", false, function()
	local mob_crit = 0
	for _, c in ipairs(golden) do
		local a, rng_a = run_case(c, true)
		local b, rng_b = run_case(c, false)
		assert(rng_a.used == rng_b.used, "case " .. c.id .. ": the number of draws differs")
		assert(a.damage == b.damage and a.damage2 == b.damage2 and a.type == b.type and a.flag == b.flag and a.dmg_lv == b.dmg_lv and a.div == b.div,
			"case " .. c.id .. " (" .. c.cat .. ") differs between the modes")
		assert(a.damage == c.out.damage, "case " .. c.id .. ": legacy mode no longer matches the recording")
		if c.a.kind == "mob" and c.out.type == 10 then mob_crit = mob_crit + 1 end
	end
	assert(mob_crit > 50, "the golden data has monster crits (" .. mob_crit .. ")")
end)

T.run("BR-RULESET-118", "a monster crit in pre-renewal is NOT multiplied: the recorded base damage is exactly max ATK + batk (all golden monster crits)", false, function()
	local seen = 0
	for _, c in ipairs(golden) do
		if c.a.kind == "mob" and c.out.type == 10 then
			for _, st in ipairs(c.out.stages) do
				if st[1] == "base" then
					assert(st[2] == c.a.rhw.atk2 + (c.a.batk or 0), "case " .. c.id .. " base " .. st[2])
					seen = seen + 1
				end
			end
		end
	end
	assert(seen >= 50, "monster crits seen: " .. seen)
end)

T.run("BR-RULESET-118", "register() takes legacy_quirks from engine.config (default true when the host says nothing)", false, function()
	local before = cfg_defaults.legacy_quirks
	booted.isolated(function()
		booted.boot({ legacy_quirks = false })
		assert(cfg_defaults.legacy_quirks == false, "damage_cfg default follows engine.config")
	end)
	assert(cfg_defaults.legacy_quirks == before, "the isolated block restored the default")
end)
