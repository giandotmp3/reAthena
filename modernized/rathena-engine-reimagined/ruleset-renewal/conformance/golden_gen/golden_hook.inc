// RENEWAL golden generator hook (rAthena COPY only; included at the very end of src/map/battle.cpp by apply_edits.py).
// Derived from ruleset-prerenewal/conformance/golden_gen/golden_hook.inc (same case format, same output format); the additions
// for the renewal ATK parts are marked "RE:".
//
// Reads synthetic attacker/target descriptions (RA_GOLDEN_CASES, one case per line, `key=value` tokens), builds
// map_session_data / mob_data units with explicit status data (nothing is derived by status_calc_*), seeds the
// legacy RNG, calls the REAL battle_calc_attack(BF_WEAPON, ...) -> battle_calc_weapon_attack() (RENEWAL build) and
// writes inputs' ids, the result (damage, damage2, type, div, flag, dmg_lv) and every RNG draw plus the stage probes.
// RA_GOLDEN_DUMP=<file> additionally dumps the element table, size table and the battle.conf values the pipeline reads.
//
// Case keys (all ints; missing = 0 unless noted):
//   id seed map(0 normal, 1 pvp, 2 gvg)
//   a.* attacker, t.* target:
//     type(0 pc, 1 mob) str agi vit int dex luk level job(0 swordman, 1 thief, 2 kagerou) skill.as_right/as_left/ko_right/ko_left batk hit cri flee flee2 def def2 size race ele ele_lv boss
//     rhw.atk rhw.atk2 rhw.ele rhw.wlv   lhw.atk lhw.atk2 lhw.ele lhw.wlv   wtype1 wtype2 bow(1: arrow attack)
//     arrow_atk crit_atk_rate non_crit_atk_rate short_atk long_atk overrefine_r overrefine_l no_sizefix
//     RE: patk crate eatk res mres atk_rate wdr_r wdr_l (weapon_damage_rate of the right/left weapon type) ammo(1 arrow, 2 cannonball)
//         rhw.range (mobs: > 3 makes a basic attack ranged)  ignore_res.N  skill.vulture  damagetaken (mob targets)
//     add.race.N add.ele.N add.size.N add.class.N  ladd.race.N ladd.ele.N ladd.size.N      (right/left weapon card bonuses)
//     sub.race.N sub.ele.N sub.size.N sub.class.N sub.defele.N near_def long_def crit_def     (target-side card bonuses)
//     sc.NAME=val1[,val2,val3,val4]  (status entries, NAME in the table below)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "mob.hpp"
#include "pc.hpp"
#include "unit.hpp"

namespace {

typedef std::map<std::string, long long> GKV;

long long gkv( const GKV& kv, const std::string& k, long long def = 0 ){
	auto it = kv.find( k );
	return it == kv.end() ? def : it->second;
}

struct GSc { const char* name; sc_type type; };
const GSc g_sc_table[] = {
	{ "berserk", SC_BERSERK }, { "curse", SC_CURSE }, { "provoke", SC_PROVOKE }, { "edp", SC_EDP },
	{ "maximize", SC_MAXIMIZEPOWER }, { "weaponperfection", SC_WEAPONPERFECTION }, { "overthrust", SC_OVERTHRUST },
	{ "deluge", SC_DELUGE }, { "volcano", SC_VOLCANO }, { "violentgale", SC_VIOLENTGALE }, { "enchantarms", SC_ENCHANTARMS },
	{ "assumptio", SC_ASSUMPTIO }, { "concentration", SC_CONCENTRATION }, { "soundofdestruction", SC_SOUNDOFDESTRUCTION },
	{ "shadow_clock", SC_SHADOW_CLOCK }, { "defender", SC_DEFENDER }, { "adjustment", SC_ADJUSTMENT },
	{ "smokepowder", SC_SMOKEPOWDER }, { "water_barrier", SC_WATER_BARRIER },
	// RE:
	{ "a_telum", SC_A_TELUM }, { "potent_venom", SC_POTENT_VENOM }, { "kaite", SC_KAITE }, { "raid", SC_RAID },
	{ "steelbody", SC_STEELBODY }, { "groundgravity", SC_GROUNDGRAVITY },
};

void g_add_sc( status_change& sc, const std::string& name, const std::string& vals ){
	for( const GSc& e : g_sc_table ){
		if( name != e.name ) continue;
		status_change_entry* sce = sc.createSCE( e.type );
		sce->timer = INVALID_TIMER;
		int32* v[4] = { &sce->val1, &sce->val2, &sce->val3, &sce->val4 };
		std::stringstream ss( vals );
		std::string tok;
		int i = 0;
		while( i < 4 && std::getline( ss, tok, ',' ) ) *v[i++] = atoi( tok.c_str() );
		return;
	}
	fprintf( stderr, "golden: unknown status '%s'\n", name.c_str() );
	exit( 2 );
}

int16 g_map_m = -1;
alignas( map_session_data ) char g_pc_buf[2][sizeof( map_session_data )];
alignas( mob_data ) char g_mob_buf[2][sizeof( mob_data )];
bool g_pc_live[2] = { false, false }, g_mob_live[2] = { false, false };
item_data g_items[2][2]; // [unit][hand]
item_data g_ammo[2];     // RE: [unit]

void g_fill_status( status_data& st, const GKV& kv, const std::string& p ){
	st = status_data(); // all zero
	st.hp = st.max_hp = 100000;
	st.sp = st.max_sp = 1000;
	st.str = gkv( kv, p + "str" ); st.agi = gkv( kv, p + "agi" ); st.vit = gkv( kv, p + "vit" );
	st.int_ = gkv( kv, p + "int" ); st.dex = gkv( kv, p + "dex" ); st.luk = gkv( kv, p + "luk" );
	st.batk = gkv( kv, p + "batk" );
	st.hit = gkv( kv, p + "hit" ); st.flee = gkv( kv, p + "flee" ); st.cri = gkv( kv, p + "cri" ); st.flee2 = gkv( kv, p + "flee2" );
	st.def = gkv( kv, p + "def" ); st.def2 = gkv( kv, p + "def2" );
	st.size = gkv( kv, p + "size" ); st.race = gkv( kv, p + "race" ); st.class_ = gkv( kv, p + "boss" ) ? CLASS_BOSS : CLASS_NORMAL;
	st.def_ele = gkv( kv, p + "ele" ); st.ele_lv = gkv( kv, p + "ele_lv", 1 );
	st.rhw.atk = gkv( kv, p + "rhw.atk" ); st.rhw.atk2 = gkv( kv, p + "rhw.atk2" ); st.rhw.ele = gkv( kv, p + "rhw.ele" );
	st.rhw.range = gkv( kv, p + "rhw.range", 1 ); st.rhw.wlv = gkv( kv, p + "rhw.wlv" );
	st.lhw.atk = gkv( kv, p + "lhw.atk" ); st.lhw.atk2 = gkv( kv, p + "lhw.atk2" ); st.lhw.ele = gkv( kv, p + "lhw.ele" ); st.lhw.range = 1;
	st.lhw.wlv = gkv( kv, p + "lhw.wlv" );
	// RE: what status_calc_pc stores (status.cpp:4368): watk = weapon ATK + refine ATK of each hand; the new ATK parts inputs
	st.watk = st.rhw.atk + st.rhw.atk2; st.watk2 = st.lhw.atk + st.lhw.atk2;
	st.eatk = gkv( kv, p + "eatk" ); st.patk = gkv( kv, p + "patk" ); st.crate = gkv( kv, p + "crate" );
	st.res = gkv( kv, p + "res" ); st.mres = gkv( kv, p + "mres" );
	st.amotion = 500; st.adelay = 1000; st.dmotion = 400;
	st.mode = MD_CANMOVE | MD_CANATTACK;
}

block_list* g_make_unit( int idx, const GKV& kv, const std::string& p, int32 level_default ){
	const bool is_mob = gkv( kv, p + "type" ) == 1;
	const int32 level = gkv( kv, p + "level", level_default );
	if( !is_mob ){
		if( g_pc_live[idx] ) ( (map_session_data*)g_pc_buf[idx] )->~map_session_data();
		map_session_data* sd = new( g_pc_buf[idx] ) map_session_data();
		g_pc_live[idx] = true;
		sd->type = BL_PC; sd->id = 150000 + idx;
		sd->m = g_map_m; sd->x = 150; sd->y = 150;
		sd->state.active = 1;
		sd->fd = 0;
		sd->status.base_level = level; sd->status.job_level = 1;
		const long long job = gkv( kv, p + "job" ); // 0 swordman, 1 thief (assassin-line dual wield), 2 kagerou/oboro
		sd->status.class_ = job == 2 ? JOB_KAGEROU : ( job == 1 ? JOB_THIEF : JOB_SWORDMAN );
		sd->class_ = job == 2 ? MAPID_KAGEROUOBORO : ( job == 1 ? MAPID_THIEF : MAPID_SWORDMAN );
		for( int i = 0; i < EQI_MAX; i++ ) sd->equip_index[i] = -1;
		unit_dataset( sd );

		g_fill_status( sd->battle_status, kv, p );
		sd->base_status = sd->battle_status;

		const int wt1 = gkv( kv, p + "wtype1" ), wt2 = gkv( kv, p + "wtype2" );
		sd->weapontype1 = (int16)wt1; sd->weapontype2 = (int16)wt2;
		sd->status.weapon = (int16)wt1;
		sd->state.arrow_atk = gkv( kv, p + "bow" ) ? 1 : 0;
		sd->special_state.no_sizefix = gkv( kv, p + "no_sizefix" ) ? 1 : 0;

		// weapon items (the weapon level is read from the inventory data in pre-renewal)
		for( int h = 0; h < 2; h++ ){
			item_data& it = g_items[idx][h];
			it.type = IT_WEAPON;
			it.weapon_level = gkv( kv, p + ( h ? "lhw.wlv" : "rhw.wlv" ), 0 );
			sd->inventory_data[h] = &it;
			sd->inventory.u.items_inventory[h].nameid = 1201;
			sd->inventory.u.items_inventory[h].amount = 1;
			if( it.weapon_level > 0 ) sd->equip_index[h ? EQI_HAND_L : EQI_HAND_R] = h; // wlv 0 = nothing equipped in that hand
		}

		// RE: a shield (an armor item) in the left hand: equip_index[EQI_HAND_L] >= 0 with a non-weapon item. The renewal ATK parts
		// still roll a (0..0) weapon ATK for it, which consumes an RNG draw (battle_calc_base_weapon_attack, battle.cpp:2454).
		if( gkv( kv, p + "shield" ) ){
			item_data& it = g_items[idx][1];
			it.type = IT_ARMOR; it.weapon_level = 0;
			sd->inventory_data[1] = &it;
			sd->inventory.u.items_inventory[1].nameid = 2101;
			sd->inventory.u.items_inventory[1].amount = 1;
			sd->equip_index[EQI_HAND_L] = 1;
		}

		// size modifiers: the server's size_fix table for the equipped weapon types (what status_calc_pc does)
		for( int h = 0; h < 2; h++ ){
			std::shared_ptr<s_sizefix_db> sf = size_fix_db.find( h ? wt2 : wt1 );
			weapon_data& w = h ? sd->left_weapon : sd->right_weapon;
			w.atkmods[SZ_SMALL] = sf ? sf->small : 100;
			w.atkmods[SZ_MEDIUM] = sf ? sf->medium : 100;
			w.atkmods[SZ_BIG] = sf ? sf->large : 100;
		}

		for( const auto& sk : { std::pair<const char*, uint16>{ "skill.as_right", AS_RIGHT }, { "skill.as_left", AS_LEFT },
				{ "skill.ko_right", KO_RIGHT }, { "skill.ko_left", KO_LEFT }, { "skill.vulture", AC_VULTURE },
				{ "skill.weapon_research", BS_WEAPONRESEARCH }, { "skill.sm_sword", SM_SWORD }, { "skill.sm_twohand", SM_TWOHAND },
				{ "skill.mace", PR_MACEMASTERY }, { "skill.spear", KN_SPEARMASTERY }, { "skill.axe", AM_AXEMASTERY },
				{ "skill.demonbane", AL_DEMONBANE } } ){
			const long long lv = gkv( kv, p + sk.first );
			const uint16 idx = skill_db.get_index( sk.second, false, __FUNCTION__, __FILE__, __LINE__ );
			if( lv > 0 && idx != 0 ){ sd->status.skill[idx].id = sk.second; sd->status.skill[idx].lv = lv; }
		}
		// RE: ammo item (arrow ATK is part of equipAtk only while ammo is equipped and is not a cannonball)
		if( const long long am = gkv( kv, p + "ammo" ) ){
			item_data& it = g_ammo[idx];
			it.type = IT_AMMO; it.subtype = am == 2 ? AMMO_CANNONBALL : AMMO_ARROW;
			sd->inventory_data[2] = &it;
			sd->inventory.u.items_inventory[2].nameid = 1750;
			sd->inventory.u.items_inventory[2].amount = 100;
			sd->equip_index[EQI_AMMO] = 2;
		}
		sd->bonus.arrow_atk = gkv( kv, p + "arrow_atk" );
		sd->bonus.arrow_ele = gkv( kv, p + "arrow_ele" ); // RE: element of the right-hand weapon while firing arrows (0 = none)
		sd->bonus.atk_rate = gkv( kv, p + "atk_rate" );
		// legacy defect (pc.hpp: int32 weapon_damage_rate[16], indexed by weapon type up to 23): types >= 16 read arrow_addele[type - 16];
		// the hook only sets the in-bounds slots, the aliasing is what the case's aadd.ele.N values exercise
		if( wt1 < 16 ) sd->indexed_bonus.weapon_damage_rate[wt1] = gkv( kv, p + "wdr_r" );
		if( wt2 < 16 && wt2 != wt1 ) sd->indexed_bonus.weapon_damage_rate[wt2] = gkv( kv, p + "wdr_l" );
		for( int i = 0; i < RC_MAX; i++ ) sd->indexed_bonus.ignore_res_by_race[i] = gkv( kv, p + "ignore_res." + std::to_string( i ) );
		sd->bonus.crit_atk_rate = gkv( kv, p + "crit_atk_rate" );
		sd->bonus.non_crit_atk_rate = gkv( kv, p + "non_crit_atk_rate" );
		sd->bonus.short_attack_atk_rate = gkv( kv, p + "short_atk" );
		sd->bonus.long_attack_atk_rate = gkv( kv, p + "long_atk" );
		sd->bonus.near_attack_def_rate = gkv( kv, p + "near_def" );
		sd->bonus.long_attack_def_rate = gkv( kv, p + "long_def" );
		sd->bonus.critical_def = gkv( kv, p + "crit_def" );
		sd->bonus.crit_def_rate = gkv( kv, p + "crit_def_rate" );
		sd->bonus.perfect_hit = gkv( kv, p + "perfect_hit" );
		sd->bonus.arrow_hit = gkv( kv, p + "arrow_hit" );
		sd->bonus.arrow_cri = gkv( kv, p + "arrow_cri" );
		sd->bonus.critical_rangeatk = gkv( kv, p + "crit_range" );
		sd->bonus.skill_ratio = gkv( kv, p + "skill_ratio" );
		sd->right_weapon.overrefine = gkv( kv, p + "overrefine_r" );
		sd->left_weapon.overrefine = gkv( kv, p + "overrefine_l" );
		for( int i = 0; i < RC_MAX; i++ ){
			sd->right_weapon.addrace[i] = gkv( kv, p + "add.race." + std::to_string( i ) );
			sd->left_weapon.addrace[i] = gkv( kv, p + "ladd.race." + std::to_string( i ) );
			sd->indexed_bonus.subrace[i] = gkv( kv, p + "sub.race." + std::to_string( i ) );
			sd->indexed_bonus.arrow_addrace[i] = gkv( kv, p + "aadd.race." + std::to_string( i ) );
			sd->indexed_bonus.critaddrace[i] = gkv( kv, p + "critadd.race." + std::to_string( i ) );
		}
		for( int i = 0; i < ELE_MAX; i++ ){
			sd->right_weapon.addele[i] = gkv( kv, p + "add.ele." + std::to_string( i ) );
			sd->left_weapon.addele[i] = gkv( kv, p + "ladd.ele." + std::to_string( i ) );
			sd->indexed_bonus.subele[i] = gkv( kv, p + "sub.ele." + std::to_string( i ) );
			sd->indexed_bonus.subdefele[i] = gkv( kv, p + "sub.defele." + std::to_string( i ) );
			sd->indexed_bonus.arrow_addele[i] = gkv( kv, p + "aadd.ele." + std::to_string( i ) );
		}
		for( int i = 0; i < SZ_MAX; i++ ){
			sd->right_weapon.addsize[i] = gkv( kv, p + "add.size." + std::to_string( i ) );
			sd->left_weapon.addsize[i] = gkv( kv, p + "ladd.size." + std::to_string( i ) );
			sd->indexed_bonus.subsize[i] = gkv( kv, p + "sub.size." + std::to_string( i ) );
			sd->indexed_bonus.arrow_addsize[i] = gkv( kv, p + "aadd.size." + std::to_string( i ) );
		}
		for( int i = 0; i < CLASS_MAX; i++ ){
			sd->right_weapon.addclass[i] = gkv( kv, p + "add.class." + std::to_string( i ) );
			sd->indexed_bonus.subclass[i] = gkv( kv, p + "sub.class." + std::to_string( i ) );
			sd->indexed_bonus.arrow_addclass[i] = gkv( kv, p + "aadd.class." + std::to_string( i ) );
		}
		sd->ud.target_count = gkv( kv, p + "targeted" );
		return sd;
	}

	if( g_mob_live[idx] ) ( (mob_data*)g_mob_buf[idx] )->~mob_data();
	mob_data* md = new( g_mob_buf[idx] ) mob_data();
	g_mob_live[idx] = true;
	md->type = BL_MOB; md->id = 200000 + idx;
	md->m = g_map_m; md->x = 150; md->y = 150;
	md->mob_id = 1002; // Poring: only the view class / race2 list are read from the db entry
	md->db = mob_db.find( md->mob_id );
	md->level = level;
	status_set_viewdata( md, md->mob_id );
	unit_dataset( md );
	md->spawn_timer = INVALID_TIMER; md->deletetimer = INVALID_TIMER; md->skill_idx = -1;
	md->damagetaken = gkv( kv, p + "damagetaken", 100 );
	g_fill_status( md->status, kv, p );
	md->base_status = &md->status;
	md->ud.target_count = gkv( kv, p + "targeted" );
	return md;
}

status_change* g_sc_of( block_list* bl ){ return status_get_sc( bl ); }

// per-case battle.conf toggles (cfg.NAME=value); restored after every case
struct GCfg { const char* name; int32* ref; int32 def; };
GCfg g_cfg[] = {
	{ "left_cardfix_to_right", &battle_config.left_cardfix_to_right, 0 }, { "attr_recover", &battle_config.attr_recover, 0 },
	{ "attack_attr_none", &battle_config.attack_attr_none, 0 }, { "weapon_defense_type", &battle_config.weapon_defense_type, 0 },
	{ "min_hitrate", &battle_config.min_hitrate, 0 }, { "max_hitrate", &battle_config.max_hitrate, 0 },
	{ "gvg_short_damage_rate", &battle_config.gvg_short_damage_rate, 0 }, { "gvg_long_damage_rate", &battle_config.gvg_long_damage_rate, 0 },
	{ "max_res_mres_ignored", &battle_config.max_res_mres_ignored, 0 }, // RE
	{ "vit_penalty_type", &battle_config.vit_penalty_type, 0 }, { "agi_penalty_type", &battle_config.agi_penalty_type, 0 },
};

void g_apply_sc( block_list* bl, const GKV& kv, const std::string& p, const std::map<std::string, std::string>& sv ){
	status_change* sc = g_sc_of( bl );
	const std::string pref = p + "sc.";
	for( auto& e : sv ) if( e.first.compare( 0, pref.size(), pref ) == 0 ) g_add_sc( *sc, e.first.substr( pref.size() ), e.second );
}

void g_dump( const char* path ){
	FILE* f = fopen( path, "wb" );
	if( !f ) return;
	for( int lv = 1; lv <= MAX_ELE_LEVEL; lv++ )
		for( int a = 0; a < ELE_MAX; a++ )
			for( int d = 0; d < ELE_MAX; d++ )
				fprintf( f, "attr %d %d %d %d\n", lv, a, d, (int)elemental_attribute_db.getAttribute( lv, a, d ) );
	for( int w = 0; w < MAX_WEAPON_TYPE; w++ ){
		std::shared_ptr<s_sizefix_db> sf = size_fix_db.find( w );
		fprintf( f, "size %d %d %d %d\n", w, sf ? sf->small : 100, sf ? sf->medium : 100, sf ? sf->large : 100 );
	}
#define GCFG( n ) fprintf( f, "cfg " #n " %d\n", (int)battle_config.n );
	GCFG( min_hitrate ) GCFG( max_hitrate ) GCFG( attack_attr_none ) GCFG( left_cardfix_to_right ) GCFG( attr_recover )
	GCFG( cardfix_monster_physical ) GCFG( skill_min_damage ) GCFG( weapon_defense_type ) GCFG( agi_penalty_type )
	GCFG( agi_penalty_target ) GCFG( agi_penalty_count ) GCFG( vit_penalty_type ) GCFG( vit_penalty_target ) GCFG( vit_penalty_count )
	GCFG( gvg_short_damage_rate ) GCFG( gvg_long_damage_rate ) GCFG( gvg_weapon_damage_rate ) GCFG( bg_short_damage_rate )
	GCFG( bg_long_damage_rate ) GCFG( pk_mode ) GCFG( ksprotection ) GCFG( devotion_rdamage ) GCFG( max_res_mres_ignored ) GCFG( undead_detect_type ) GCFG( agi_penalty_num ) GCFG( vit_penalty_num )
#undef GCFG
	fclose( f );
}

} // namespace

void golden_main(){
	const char* dump = getenv( "RA_GOLDEN_DUMP" );
	if( dump && *dump ) g_dump( dump );
	const char* cases = getenv( "RA_GOLDEN_CASES" );
	const char* out = getenv( "RA_GOLDEN_OUT" );
	if( !cases || !*cases || !out || !*out ){
		if( dump && *dump ) exit( 0 );
		return;
	}
	std::ifstream in( cases );
	FILE* fo = fopen( out, "wb" );
	if( !in || !fo ){ fprintf( stderr, "golden: cannot open files\n" ); exit( 2 ); }

	g_map_m = map_mapname2mapid( "prontera" );
	map_data* mapdata = map_getmapdata( g_map_m );
	std::vector<GoldenRnd> rlog;
	std::vector<GoldenStage> slog;
	g_golden_rnd_log = &rlog;
	g_golden_stages = &slog;

	for( GCfg& c : g_cfg ) c.def = *c.ref;
	std::string line;
	long n = 0;
	while( std::getline( in, line ) ){
		if( line.empty() || line[0] == '#' ) continue;
		GKV kv;
		std::map<std::string, std::string> sv;
		std::stringstream ss( line );
		std::string tok;
		while( ss >> tok ){
			size_t eq = tok.find( '=' );
			if( eq == std::string::npos ) continue;
			std::string k = tok.substr( 0, eq ), v = tok.substr( eq + 1 );
			if( k.find( "sc." ) != std::string::npos ) sv[k] = v;
			else kv[k] = atoll( v.c_str() );
		}
		// the typed snapshot the delegated map-rules calls read (rules_config(): agi/vit penalty type, count, num, ...) must follow
		for( GCfg& c : g_cfg ){
			*c.ref = (int32)gkv( kv, std::string( "cfg." ) + c.name, c.def );
			rules_config_refresh( c.ref );
		}
		const long long id = gkv( kv, "id" );
		const int mapmode = gkv( kv, "map" );
		mapdata->setMapFlag( MF_PVP, mapmode == 1 );
		mapdata->setMapFlag( MF_GVG, mapmode == 2 );

		block_list* src = g_make_unit( 0, kv, "a.", 1 );
		block_list* tgt = g_make_unit( 1, kv, "t.", 1 );
		g_apply_sc( src, kv, "a.", sv );
		g_apply_sc( tgt, kv, "t.", sv );

		rnd_seed( (uint32)gkv( kv, "seed", 1 ) );
		rlog.clear(); slog.clear();
		Damage d = battle_calc_attack( BF_WEAPON, src, tgt, 0, 0, 0 );

		fprintf( fo, "id=%lld dmg=%lld dmg2=%lld type=%d div=%d flag=%d dmglv=%d", id, (long long)d.damage, (long long)d.damage2,
			(int)d.type, (int)d.div_, (int)d.flag, (int)d.dmg_lv );
		fputs( " R", fo );
		for( const GoldenRnd& r : rlog ){
			if( r.kind == 0 ) fprintf( fo, " r:%lld", r.val );
			else fprintf( fo, " v:%lld:%lld:%lld", r.lo, r.hi, r.val );
		}
		fputs( " S", fo );
		for( const GoldenStage& s : slog ) fprintf( fo, " %s:%lld:%lld", s.name, s.d1, s.d2 );
		fputc( '\n', fo );
		n++;
	}
	fclose( fo );
	fprintf( stderr, "golden: %ld cases written\n", n );
	exit( 0 );
}
