--!strict-compatible
-- Conformance cases for group 'contract'. One entry per assigned BR-RULESET rule.
-- expected == nil means the expected value must still be captured from a
-- RATHENA_GOLDEN_TRACE recording (see conformance/golden/README.md); such a case
-- cannot pass until it is captured. Inputs are synthetic, no legacy data.
return {
	{ rule = "BR-RULESET-111", fn = "r111", mode = "B", pri = "P0", title = "ruleset publishes the tables the native default stage needs",
	  example = nil,
	  given = {},
	  expected = {tables={'cardfix_buckets','element_matrix','size_matrix','status_modifiers'}} },
	{ rule = "BR-RULESET-112", fn = "r112", mode = "B", pri = "P0", title = "item scripts return a cacheable compiled list of bonus deltas",
	  example = nil,
	  given = {item='poring_card'},
	  expected = {cacheable=true, deltas={{attr='luk',op='add',value=2},{attr='cri',op='add',value=10}}} },
	{ rule = "BR-RULESET-116", fn = "r116", mode = "B", pri = "P0", title = "calculation returns an ordered effect queue and never mutates the world",
	  example = nil,
	  given = {attacker_weapon='1h_sword', target_status='reject_sword', damage=1000},
	  expected = {mutated_world=false, effects={{kind='reflect',amount=500}}} },
	{ rule = "BR-RULESET-117", fn = "r117", mode = "B", pri = "P0", title = "hook surface: skill, damage-stage, status, item and formula hooks",
	  example = nil,
	  given = {},
	  expected = {skill=9, damage={'base','ratio','element','defense','final'}, status={'start_gate','resist','calc_contribution','tick','end'}, item={'bonus_script','equip','unequip','use','combo'}, formula={'heal','cast','delay','sp_cost'}} },
	{ rule = "BR-RULESET-118", fn = "r118", mode = "B", pri = "P0", title = "manifest names mode, sub-flags, DB set and battle-config defaults",
	  example = nil,
	  given = {},
	  expected = {mode='pre-renewal', db_set='db/pre-re', subflags={cast=false,drop=false,exp=false,lvdmg=false,aspd=false,stat=false,edp=false}, battle_defaults={hit_rate_min=5,hit_rate_max=100,max_res_mres_ignored=50,attack_attr_none=14,left_cardfix_to_right=true,cardfix_monster_physical=true}, legacy_quirks=true} },
	{ rule = "BR-RULESET-119", fn = "r119", mode = "B", pri = "P0", title = "conformance suite covers every assigned rule ID exactly once",
	  example = nil,
	  given = {},
	  expected = {missing={}, extra={}, duplicates={}} },
}
