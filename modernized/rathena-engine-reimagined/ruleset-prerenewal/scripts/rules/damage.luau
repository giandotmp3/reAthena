--!strict-compatible
-- Rule functions for group 'damage'. Each rNNN takes the case `given` table and returns
-- the outcome table. Unimplemented rules raise NOT_IMPLEMENTED (expected-failure
-- acceptance tests, tagged with the rule ID, track them).
local notimpl = import("scripts/notimpl")
local M = {}

-- BR-RULESET-001 (B, P0): calculator chosen by attack type; total < 1 is MISS, div becomes 1 for players
function M.r001(input)
	return notimpl("BR-RULESET-001")
end

-- BR-RULESET-002 (B, P0): context init: div from hit count, ground skills amotion 0, flags WEAPON+SKILL
function M.r002(input)
	return notimpl("BR-RULESET-002")
end

-- BR-RULESET-003 (B, P0): lucky dodge: basic attack avoided when rnd%1000 < flee2
function M.r003(input)
	return notimpl("BR-RULESET-003")
end

-- BR-RULESET-004 (B, P0): double attack chance is lv*5 percent in pre-renewal (dagger)
function M.r004(input)
	return notimpl("BR-RULESET-004")
end

-- BR-RULESET-005 (B, P0): crit eligibility: rnd%1000 < cri after crit-defense scaling
function M.r005(input)
	return notimpl("BR-RULESET-005")
end

-- BR-RULESET-006 (B, P0): hit determination: hitrate = 80 + hit - flee, clamped to 5..100
function M.r006(input)
	return notimpl("BR-RULESET-006")
end

-- BR-RULESET-007 (B, P1): plant / infinite-defense targets take 1 damage per hit
function M.r007(input)
	return notimpl("BR-RULESET-007")
end

-- BR-RULESET-008 (PRE, P0): base damage: min = DEX*(80+20*weaponLv)%, max = weapon ATK, top value never rolled
function M.r008(input)
	return notimpl("BR-RULESET-008")
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
function M.r014(input)
	return notimpl("BR-RULESET-014")
end

-- BR-RULESET-015 (B, P0): skill ratio = 100 + bonuses; Berserk adds +100 in PRE
function M.r015(input)
	return notimpl("BR-RULESET-015")
end

-- BR-RULESET-016 (B, P0): PRE: status ATK% multiplies the damage after the skill ratio
function M.r016(input)
	return notimpl("BR-RULESET-016")
end

-- BR-RULESET-017 (B, P1): constant additions after ratio (Asura = 250 + 150*lv)
function M.r017(input)
	return notimpl("BR-RULESET-017")
end

-- BR-RULESET-020 (B, P0): attacker card fix multiplies, it does not add
function M.r020(input)
	return notimpl("BR-RULESET-020")
end

-- BR-RULESET-021 (B, P0): defender card fix (weapon): element, size, race, class, near/far rates
function M.r021(input)
	return notimpl("BR-RULESET-021")
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
function M.r027(input)
	return notimpl("BR-RULESET-027")
end

-- BR-RULESET-028 (B, P1): PRE post-defense: refine bonus, then min 1
function M.r028(input)
	return notimpl("BR-RULESET-028")
end

-- BR-RULESET-029 (B, P0): element selection: endow overrides weapon element
function M.r029(input)
	return notimpl("BR-RULESET-029")
end

-- BR-RULESET-030 (B, P0): NK_IGNOREELEMENT skips element application
function M.r030(input)
	return notimpl("BR-RULESET-030")
end

-- BR-RULESET-031 (B, P0): PRE: status percent applied first, then multiplied by the element table
function M.r031(input)
	return notimpl("BR-RULESET-031")
end

-- BR-RULESET-032 (B, P1): attr_recover=0 clamps a negative element ratio to 0 damage
function M.r032(input)
	return notimpl("BR-RULESET-032")
end

-- BR-RULESET-033 (B, P1): PRE EDP adds val3 percent after element, right hand only
function M.r033(input)
	return notimpl("BR-RULESET-033")
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
function M.r037(input)
	return notimpl("BR-RULESET-037")
end

-- BR-RULESET-038 (B, P0): battle_calc_damage ordering: gates, increases, reductions, caster effects
function M.r038(input)
	return notimpl("BR-RULESET-038")
end

-- BR-RULESET-039 (B, P0): target modifiers: PRE Assumptio halves damage
function M.r039(input)
	return notimpl("BR-RULESET-039")
end

-- BR-RULESET-040 (B, P0): caster-side and tail modifiers incl. damage-to-0 statuses
function M.r040(input)
	return notimpl("BR-RULESET-040")
end

-- BR-RULESET-041 (B, P0): map-mode rates: GvG skills x60%
function M.r041(input)
	return notimpl("BR-RULESET-041")
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
