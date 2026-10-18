--!nonstrict
-- legacy_quirks (HITL #2: legacy defects ship as-is behind a flag, default on). Two defects live in the renewal weapon pipeline:
--   1. a monster's critical hit is multiplied twice: x14/10 in the base damage (battle.cpp:2635) and x1.4f in the crit stage (5679-5683)
--   2. weapon_damage_rate has 16 slots but is indexed by weapon type up to 23 (pc.hpp:577): guns read arrow_addele[type - 16]
-- With legacy_quirks = true (the default) both are reproduced exactly: the golden suite proves it. With false the corrected behaviour
-- below applies, and NOTHING else changes (checked over every golden case).
local T = import("tests/support/harness")
local booted = import("tests/support/booted")
local weapon = import("scripts/damage/weapon")
local unit = import("scripts/damage/unit")
local cfg_defaults = import("data/damage_cfg")
local fake_ctx = import("tests/support/fake_ctx")
local scripted = import("tests/support/scripted_rng").scripted
local runner = import("tests/support/golden_runner")

local golden = runner.load()
local by_id = {}
for _, c in ipairs(golden) do by_id[c.id] = c end

local function run_case(c, legacy_quirks)
	local cfg = {}
	for k, v in pairs(cfg_defaults) do cfg[k] = v end
	for k, v in pairs(c.cfg) do cfg[k] = v end
	cfg.legacy_quirks = legacy_quirks
	local rng = scripted(c.out.draws)
	local ctx = fake_ctx.new(rng)
	local res = weapon.calc_attack(ctx, cfg, unit.normalize(c.a), unit.normalize(c.t), { map = c.map })
	return res, rng
end

T.run("BR-RULESET-118", "legacy_quirks defaults to true in the package config", false, function()
	assert(cfg_defaults.legacy_quirks == true)
end)

T.run("BR-RULESET-034", "monster crit, legacy_quirks = true: x14/10 in the base damage AND x1.4f after DEF (golden case 2520: 1009 -> 766 -> 1072)", false, function()
	local c = by_id[2520]
	assert(c.a.kind == "mob" and c.out.type == 10 and c.out.damage == 1072, "the golden record is a monster crit")
	local res, rng = run_case(c, true)
	assert(res.damage == 1072 and res.type == 10, "got " .. res.damage)
	assert(rng.used == rng.total)
end)

T.run("BR-RULESET-034", "monster crit, legacy_quirks = false: ONE x1.4f (case 2520: base 635 + 86 = 721 -> DEF 529 -> floor(529 * 1.4f) = 740), same draws", false, function()
	local res, rng = run_case(by_id[2520], false)
	assert(res.damage == 740 and res.type == 10 and res.dmg_lv == 5 and res.flag == 529, "got " .. res.damage)
	assert(rng.used == rng.total, "the corrected path consumes the same RNG draws")
end)

T.run("BR-RULESET-034", "mob_base_damage: a crit takes max + batk; the x14/10 is applied only under the legacy quirk", false, function()
	local ctx = fake_ctx.new(scripted({}))
	local mob = unit.normalize({ kind = "mob", batk = 5, rhw = { atk = 1, atk2 = 100 } })
	assert(weapon.mob_base_damage(ctx, mob, "r", true, { cfg = { legacy_quirks = true } }) == 147) -- (100 + 5) * 14 / 10
	assert(weapon.mob_base_damage(ctx, mob, "r", true, { cfg = { legacy_quirks = false } }) == 105)
	assert(weapon.mob_base_damage(ctx, mob, "r", true) == 147, "without an attack state the legacy behaviour applies")
	local rng = scripted({ 7 }) -- a non-crit takes atkmin + rnd() % (max - min) + batk: no multiplier in either mode
	assert(weapon.mob_base_damage(fake_ctx.new(rng), mob, "r", false, { cfg = { legacy_quirks = false } }) == 1 + 7 % 99 + 5)
	rng = scripted({ 7 })
	assert(weapon.mob_base_damage(fake_ctx.new(rng), mob, "r", false, { cfg = { legacy_quirks = true } }) == 1 + 7 % 99 + 5)
end)

-- weapon ATK part of a rifle (type 18, slot 2 of arrow_addele under the alias): weapon ATK 100 level 1, DEX 0 -> range 95..105, a crit takes 105
local function rifle(bonus, wtype)
	return unit.normalize({ kind = "pc", wtype1 = wtype or 18, level = 1, no_sizefix = true, rhw = { atk = 100, atk2 = 0, ele = 0, wlv = 1 }, bonus = bonus })
end
local TGT = unit.normalize({ kind = "mob", size = 1 })
local function atk_part(src, quirks)
	return weapon.weapon_atk_part(fake_ctx.new(scripted({})), src, TGT, "r", true, { cfg = { legacy_quirks = quirks } })
end

T.run("BR-RULESET-102", "weapon_damage_rate, legacy_quirks = true: a rifle (type 18) reads arrow_addele[2] = +20% instead of its own slot (105 -> 126)", false, function()
	local src = rifle({ aadd_ele = { [2] = 20 }, wdr_r = -10 })
	assert(atk_part(src, true) == 126)
end)

T.run("BR-RULESET-102", "weapon_damage_rate, legacy_quirks = false: the rifle uses its own slot, -10% truncated toward zero (105 -> 95); arrow_addele no longer leaks", false, function()
	local src = rifle({ aadd_ele = { [2] = 20 }, wdr_r = -10 })
	assert(atk_part(src, false) == 95)
	assert(atk_part(rifle({ aadd_ele = { [2] = 20 } }), false) == 105, "no weapon_damage_rate bonus: unchanged")
	assert(atk_part(rifle({ wdr_r = 30 }), true) == 105, "legacy: the own slot is not read for types >= 16")
	assert(atk_part(rifle({ wdr_r = 30 }), false) == 136, "corrected: +30% of 105 = 31.5 -> 31")
end)

T.run("BR-RULESET-102", "weapon_damage_rate: types below 16 read their own slot in both modes", false, function()
	local sword = rifle({ aadd_ele = { [2] = 20 }, wdr_r = 10 }, 2)
	assert(atk_part(sword, true) == 115 and atk_part(sword, false) == 115) -- +10% of 105 = 10.5 -> 10
end)

T.run("BR-RULESET-102", "weapon_damage_rate: every gun type (17..21) aliases arrow_addele[type - 16] under the quirk and reads wdr_r without it", false, function()
	for wtype = 17, 21 do
		local ele = {}
		ele[wtype - 16] = 50
		local src = rifle({ aadd_ele = ele, wdr_r = -20 }, wtype)
		assert(atk_part(src, true) == 157, "legacy type " .. wtype .. " got " .. atk_part(src, true)) -- 105 + 52
		assert(atk_part(src, false) == 84, "corrected type " .. wtype .. " got " .. atk_part(src, false)) -- 105 - 21
	end
end)

T.run("BR-RULESET-102", "golden case 1590 (gatling, arrow_addele[3] = -10): legacy_quirks = true reproduces the recorded 30; false gives the corrected, higher damage", false, function()
	local c = by_id[1590]
	assert(c.cat == "gun_wdr_alias" and c.a.wtype1 == 19 and c.out.damage == 30)
	local res, rng = run_case(c, true)
	assert(res.damage == 30 and rng.used == rng.total)
	local fixed, rng2 = run_case(c, false)
	assert(fixed.damage > 30, "corrected damage " .. fixed.damage)
	assert(rng2.used == rng2.total)
end)

T.run("BR-RULESET-118", "legacy_quirks = false changes nothing except the two defects: every other golden case is identical in both modes", false, function()
	local affected_changed, affected_total = 0, 0
	for _, c in ipairs(golden) do
		local mob_crit = c.a.kind == "mob" and c.out.type == 10
		local gun = c.a.kind == "pc" and ((c.a.wtype1 or 0) >= 16 or (c.a.wtype2 or 0) >= 16)
		local a, rng_a = run_case(c, true)
		local b, rng_b = run_case(c, false)
		assert(rng_a.used == rng_b.used, "case " .. c.id .. ": the number of draws differs")
		if mob_crit or gun then
			affected_total = affected_total + 1
			if a.damage ~= b.damage or a.damage2 ~= b.damage2 then affected_changed = affected_changed + 1 end
		else
			assert(a.damage == b.damage and a.damage2 == b.damage2 and a.type == b.type and a.flag == b.flag and a.dmg_lv == b.dmg_lv and a.div == b.div,
				"case " .. c.id .. " (" .. c.cat .. ") differs although it exercises neither defect")
		end
	end
	assert(affected_total > 100 and affected_changed > 100, affected_changed .. " of " .. affected_total .. " affected cases changed")
end)

T.run("BR-RULESET-118", "register() takes legacy_quirks from engine.config: with false the default config corrects the defect without touching the cfg", false, function()
	local before = cfg_defaults.legacy_quirks
	booted.isolated(function()
		booted.boot({ legacy_quirks = false })
		assert(cfg_defaults.legacy_quirks == false, "damage_cfg default follows engine.config")
		local c = by_id[2520]
		local cfg = {}
		for k, v in pairs(cfg_defaults) do cfg[k] = v end
		local res = weapon.calc_attack(fake_ctx.new(scripted(c.out.draws)), cfg, unit.normalize(c.a), unit.normalize(c.t), { map = c.map })
		assert(res.damage == 740, "got " .. res.damage)
	end)
	assert(cfg_defaults.legacy_quirks == before, "the isolated block restored the default")
end)
