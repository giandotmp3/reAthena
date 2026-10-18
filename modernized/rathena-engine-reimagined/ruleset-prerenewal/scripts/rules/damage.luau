--!nonstrict
-- Rule functions for group 'damage'. Each rNNN takes the case `given` table and returns
-- the outcome table. Unimplemented rules raise NOT_IMPLEMENTED (expected-failure
-- acceptance tests, tagged with the rule ID, track them).
local notimpl = import("scripts/notimpl")
local weapon = import("scripts/damage/weapon")
local unit = import("scripts/damage/unit")
local C = import("scripts/damage/consts")
local DEFAULT_CFG = import("data/damage_cfg")
local M = {}

-- Rules that draw random numbers or do integer math take the injected `ctx` as 2nd argument (interface: header of
-- scripts/damage/weapon.luau). `input.draws` is the recorded sequence the test ctx replays; rules never see `input.draws`.
local function pc_unit(extra)
	local u = unit.normalize({ kind = "pc" })
	for k, v in pairs(extra or {}) do u[k] = v end
	return u
end

-- BR-RULESET-001 (B, P0): calculator chosen by attack type; total < 1 is MISS, div becomes 1 for players
function M.r001(input, ctx)
	-- total below 1 is a miss, players report div 1 (the weapon calculator; magic and misc calculators are not covered)
	assert(input.attack_type == nil or input.attack_type == "weapon", "only the weapon calculator is implemented")
	local dmg_lv, div = weapon.finish(input.is_player, input.damage, input.damage2 or 0, input.dmg_lv, input.div)
	return { dmg_lv = dmg_lv, div = div }
end

-- BR-RULESET-002 (B, P0): context init: div from hit count, ground skills amotion 0, flags WEAPON+SKILL
function M.r002(input)
	return notimpl("BR-RULESET-002")
end

-- BR-RULESET-003 (B, P0): lucky dodge: basic attack avoided when rnd%1000 < flee2
function M.r003(input, ctx)
	return { avoided = weapon.lucky_dodge(ctx, { flee2 = input.flee2 }) }
end

-- BR-RULESET-004 (B, P0): double attack chance is lv*5 percent in pre-renewal (dagger)
function M.r004(input)
	return notimpl("BR-RULESET-004")
end

-- BR-RULESET-005 (B, P0): crit eligibility: rnd%1000 < cri after crit-defense scaling
function M.r005(input, ctx)
	local src = pc_unit({ kind = input.attacker or "pc", cri = input.cri })
	local tgt = pc_unit({ kind = input.target or "mob", luk = input.target_luk })
	if input.crit_def and input.crit_def ~= 0 then tgt.bonus = { crit_def = input.crit_def } end
	local crit, chance = weapon.crit_roll(ctx, src, tgt, nil)
	return { crit = crit, chance = chance }
end

-- BR-RULESET-006 (B, P0): hit determination: hitrate = 80 + hit - flee, clamped to 5..100
function M.r006(input, ctx)
	local cfg = {}
	for k, v in pairs(DEFAULT_CFG) do cfg[k] = v end
	cfg.min_hitrate, cfg.max_hitrate = input.min or cfg.min_hitrate, input.max or cfg.max_hitrate
	local src = pc_unit({ hit = input.hit })
	local tgt = pc_unit({ kind = "mob", flee = input.flee })
	local hit, hitrate = weapon.hit_roll(ctx, cfg, src, tgt, false, nil)
	return { hit = hit, hitrate = hitrate }
end

-- BR-RULESET-007 (B, P1): plant / infinite-defense targets take 1 damage per hit
function M.r007(input)
	return notimpl("BR-RULESET-007")
end

-- BR-RULESET-008 (PRE, P0): base damage: min = DEX*(80+20*weaponLv)%, max = weapon ATK, top value never rolled
function M.r008(input, ctx)
	local src = pc_unit({ dex = input.dex, batk = input.batk or 0, wtype1 = input.wtype or 0,
		rhw = { atk = input.weapon_atk, atk2 = 0, ele = 0, wlv = input.weapon_lv },
		sc = input.maximize and { maximize = {} } or nil })
	local min, max = weapon.base_range(ctx, src, "r", input.crit, false)
	if input.maximize then min = max end
	local damage = weapon.base_damage(ctx, src, "r", input.size or 1, input.crit, false, false, nil)
	return { min = min, max = max, damage = damage }
end

-- BR-RULESET-012 (B, P0): per-skill base-damage replacement (Sacrifice = 9% max HP)
function M.r012(input)
	return notimpl("BR-RULESET-012")
end

-- BR-RULESET-013 (B, P1): SPLASHSPLIT divides damage by target count
function M.r013(input)
	return notimpl("BR-RULESET-013")
end

-- BR-RULESET-014 (B, P1): PRE: a crit adds crit_atk_rate percent
function M.r014(input, ctx)
	local src = pc_unit({ bonus = { crit_atk_rate = input.crit_atk_rate } })
	local pct = weapon.base_rate_pct(src, nil, input.crit)
	return { damage = input.damage + ctx.mul_div(input.damage, pct, 100) }
end

-- BR-RULESET-015 (B, P0): skill ratio = 100 + bonuses; Berserk adds +100 in PRE
function M.r015(input, ctx)
	local src = pc_unit({ bonus = { skill_ratio = input.bonus_skill_ratio or 0 }, sc = {} })
	if input.berserk then src.sc.berserk = {} end
	if input.overthrust_val3 then src.sc.overthrust = { val3 = input.overthrust_val3 } end
	local ratio = weapon.skill_ratio(src)
	return { ratio = ratio, damage = input.damage and ctx.mul_div(input.damage, ratio, 100) or nil }
end

-- BR-RULESET-016 (B, P0): PRE: status ATK% multiplies the damage after the skill ratio
function M.r016(input, ctx)
	local src = pc_unit({ sc = input.sc })
	return { damage = ctx.mul_div(input.damage, weapon.atk_percent(src), 100) }
end

-- BR-RULESET-017 (B, P1): constant additions after ratio (Asura = 250 + 150*lv)
function M.r017(input)
	return notimpl("BR-RULESET-017")
end

-- BR-RULESET-020 (B, P0): attacker card fix multiplies, it does not add
function M.r020(input, ctx)
	local src = pc_unit({ bonus = { add_race = { [input.race_id or 0] = input.race_pct }, add_size = { [input.size_id or 1] = input.size_pct } } })
	local tgt = unit.normalize({ kind = "mob", race = input.race_id or 0, size = input.size_id or 1 })
	local cfg = DEFAULT_CFG
	return { damage = input.damage + weapon.cardfix_attacker(ctx, cfg, src, tgt, input.damage, "r", C.BF_WEAPON + C.BF_NORMAL + C.BF_SHORT) }
end

-- BR-RULESET-021 (B, P0): defender card fix (weapon): element, size, race, class, near/far rates
function M.r021(input, ctx)
	local src, tgt = unit.normalize(input.src), unit.normalize(input.tgt)
	local delta = weapon.cardfix_target(ctx, src, tgt, input.damage, "r", input.flag, input.rele, input.lele or 0)
	return { damage = input.damage + delta }
end

-- BR-RULESET-022 (B, P0): magic card fix PRE: one multiplicative product
function M.r022(input)
	return notimpl("BR-RULESET-022")
end

-- BR-RULESET-023 (B, P1): status elemental vulnerability applied as card fix (Magic Poison +50)
function M.r023(input)
	return notimpl("BR-RULESET-023")
end

-- BR-RULESET-024 (B, P0): mastery: Demon Bane = lv*(baseLv/20+3) vs demon/undead
function M.r024(input)
	return notimpl("BR-RULESET-024")
end

-- BR-RULESET-026 (B, P1): defense modifiers beyond BR-MAP-013..016 (Expiatio -5%/lv DEF)
function M.r026(input)
	return notimpl("BR-RULESET-026")
end

-- BR-RULESET-027 (B, P0): PRE crit ignores DEF
function M.r027(input, ctx)
	local tgt = unit.normalize(input.tgt)
	local vit, def1 = weapon.vit_def(ctx, DEFAULT_CFG, tgt)
	local damage = weapon.def_reduce(ctx, input.damage, def1, vit, input.crit, 0)
	return { damage = damage, def_bypassed = input.crit == true and damage == input.damage }
end

-- BR-RULESET-028 (B, P1): PRE post-defense: refine bonus, then min 1
function M.r028(input, ctx)
	return { damage = weapon.post_defense(input.damage, input.refine_bonus) }
end

-- BR-RULESET-029 (B, P0): element selection: endow overrides weapon element
function M.r029(input, ctx)
	local src = pc_unit({ rhw = { atk = 0, atk2 = 0, ele = input.weapon_element, wlv = 0 } })
	if input.endow then src.sc = { enchantarms = { val1 = input.endow } } end
	return { element = weapon.weapon_element(src, "r") }
end

-- BR-RULESET-030 (B, P0): NK_IGNOREELEMENT skips element application
function M.r030(input, ctx)
	local cfg = {}
	for k, v in pairs(DEFAULT_CFG) do cfg[k] = v end
	if input.attack_attr_none then cfg.attack_attr_none = input.attack_attr_none end
	local src = unit.normalize({ kind = input.attacker or "pc" })
	local tgt = unit.normalize({ kind = "mob", element = input.def_element, ele_lv = input.def_ele_lv })
	return { damage = weapon.element_damage(ctx, cfg, src, tgt, input.damage, input.atk_element) }
end

-- BR-RULESET-031 (B, P0): PRE: status percent applied first, then multiplied by the element table
function M.r031(input, ctx)
	local src = pc_unit({ sc = input.deluge_val3 and { deluge = { val3 = input.deluge_val3 } } or nil })
	local tgt = unit.normalize({ kind = "mob", element = input.def_element, ele_lv = input.def_ele_lv })
	return { damage = weapon.attr_fix(ctx, src, tgt, input.damage, input.atk_element) }
end

-- BR-RULESET-032 (B, P1): attr_recover=0 clamps a negative element ratio to 0 damage
function M.r032(input, ctx)
	local cfg = { attr_recover = input.attr_recover, attack_attr_none = 0 }
	local src, tgt = pc_unit(), unit.normalize({ kind = "mob", element = input.def_element, ele_lv = input.def_ele_lv })
	local damage = weapon.element_damage(ctx, cfg, src, tgt, input.damage, input.atk_element)
	return { damage = weapon.recover_cap(cfg, damage) }
end

-- BR-RULESET-033 (B, P1): PRE EDP adds val3 percent after element, right hand only
function M.r033(input, ctx)
	return { damage = input.damage + ctx.mul_div(input.damage, input.edp_val3, 100) }
end

-- BR-RULESET-035 (B, P1): PRE flat additions: Weaponry Research +2 per level
function M.r035(input)
	return notimpl("BR-RULESET-035")
end

-- BR-RULESET-036 (B, P1): div fix: negative div means divide then multiply
function M.r036(input)
	return notimpl("BR-RULESET-036")
end

-- BR-RULESET-037 (B, P1): dual wield (Assassin): right 50+10lv, left 30+10lv
function M.r037(input, ctx)
	local src = pc_unit({ job = (input.job == "assassin" or input.job == "thief") and "thief" or input.job,
		skills = { as_right = input.right_lv, as_left = input.left_lv, ko_right = input.right_lv, ko_left = input.left_lv } })
	local right, left = weapon.dual_wield(ctx, src, input.right, input.left)
	return { right = right, left = left }
end

-- BR-RULESET-038 (B, P0): battle_calc_damage ordering: gates, increases, reductions, caster effects
function M.r038(input)
	return notimpl("BR-RULESET-038")
end

-- BR-RULESET-039 (B, P0): target modifiers: PRE Assumptio halves damage
function M.r039(input, ctx)
	local tgt = unit.normalize({ kind = "mob", sc = input.assumptio and { assumptio = {} } or nil })
	return { damage = weapon.target_modifiers(ctx, DEFAULT_CFG, tgt, input.damage, C.BF_WEAPON + C.BF_NORMAL + C.BF_SHORT, input.vs_map == true) }
end

-- BR-RULESET-040 (B, P0): caster-side and tail modifiers incl. damage-to-0 statuses
function M.r040(input)
	return notimpl("BR-RULESET-040")
end

-- BR-RULESET-041 (B, P0): map-mode rates: GvG skills x60%
function M.r041(input, ctx)
	assert(input.map_mode == "gvg", "only the GvG basic-attack rates are implemented (BG shares them; PK and skill rates are not)")
	local cfg = {}
	for k, v in pairs(DEFAULT_CFG) do cfg[k] = v end
	cfg.gvg_short_damage_rate = input.gvg_short_damage_rate or cfg.gvg_short_damage_rate
	cfg.gvg_long_damage_rate = input.gvg_long_damage_rate or cfg.gvg_long_damage_rate
	local flag = C.BF_WEAPON + C.BF_NORMAL + (input.range == "long" and C.BF_LONG or C.BF_SHORT)
	return { damage = weapon.gvg_damage(ctx, cfg, input.damage, flag) }
end

-- BR-RULESET-042 (B, P1): final weapon modifiers: Reject Sword reflect, Poison React autocast
function M.r042(input)
	return notimpl("BR-RULESET-042")
end

-- BR-RULESET-043 (B, P1): damage reflection (battle_calc_return_damage)
function M.r043(input)
	return notimpl("BR-RULESET-043")
end

-- BR-RULESET-044 (B, P0): magic ordering PRE: ratio immediately, skill ATK bonus before MDEF, card fix last
function M.r044(input)
	return notimpl("BR-RULESET-044")
end

-- BR-RULESET-045 (B, P0): PRE MDEF: dmg*(100-mdef)/100 - mdef2
function M.r045(input)
	return notimpl("BR-RULESET-045")
end

-- BR-RULESET-046 (B, P1): magic special bases: PRE Turn Undead chance
function M.r046(input)
	return notimpl("BR-RULESET-046")
end

-- BR-RULESET-047 (B, P1): misc attacks: PRE Land Mine = lv*(DEX+75)*(100+INT)/100
function M.r047(input)
	return notimpl("BR-RULESET-047")
end

-- BR-RULESET-048 (B, P0): heal amount PRE: (baseLv+INT)/8 * (4+8*lv)
function M.r048(input)
	return notimpl("BR-RULESET-048")
end

-- BR-RULESET-049 (B, P1): non-player attackers attack Neutral (attack_attr_none)
function M.r049(input)
	return notimpl("BR-RULESET-049")
end

-- BR-RULESET-050 (B, P1): skill damage DB percent adjustment applied at the very end
function M.r050(input)
	return notimpl("BR-RULESET-050")
end

-- BR-RULESET-018: implemented, stage order is declarative data.
function M.r018(input)
	local pipeline = import("scripts/pipeline")
	return { stages = pipeline.ORDER[input.kind] }
end

return M
