// Golden generator hook for the STATUS calculation (rAthena COPY only; included at the very end of src/map/status.cpp by
// apply_edits.py, so the file-static status_calc_* functions are callable).
//
// Reads synthetic unit + status-set descriptions (RA_GOLDEN_ST_CASES, one case per line, `key=value` tokens), builds a
// map_session_data (PC) or mob_data (MOB) with an explicit status set (sc_type + val1..val4), calls the REAL legacy function
// named by `fn` and writes its result (RA_GOLDEN_ST_OUT). Nothing is derived by the Luau side: every input is an explicit token.
// RA_GOLDEN_ST_CONSTS=<file> dumps the enum / macro constants the Luau code needs (names from golden_status_consts.inc).
//
// Case keys (all ints unless noted; missing = 0):
//   id fn ut(0 pc, 1 mob) base(the value passed in) [base2] map(0 normal, 1 gvg, 2 battleground)
//   w(status.weapon) lr(sd->state.lr_flag) rwlv lwlv (weapon level of the equipped right / left weapon item, 0 = nothing) opt(sc.option)
//   job(0 swordman, 1 assassin) sex perm_speed (sd->state.permanent_speed) sk.NAME=lv  (NAME in the skill table below)
//   skilltimer(1: ud.skilltimer valid) skill_id (ud.skill_id by NAME in the skill table, token `ud.skill=NAME`) ud.lv
//   bonus.speed_rate bonus.speed_add_rate bonus.hp bonus.sp  cfg.NAME=value (battle.conf values the functions read)
//   script: runs the Script: of every active status (sc_type ascending) and prints the pc_bonus* calls it makes (log=n,type,..,value;...)
//   chain: b.NAME / s.NAME (base and battle status fields of GCHAIN_FIELDS, s.* defaults to b.*), flags=STR,AGI,... (SCB_ names), bonus.weapon_atk_rate
//   level (base level)  cant.NAME=0|1 (initial sc.cant flags for fn=state)  changed=NAME start=0|1 (fn=state)
//   sc.NAME=v1[,v2,v3,v4]  (status entries; NAME = the SC_ suffix)
#include <algorithm>
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
#include "script_host.hpp"

namespace {

typedef std::map<std::string, long long> GSKV;

long long gskv( const GSKV& kv, const std::string& k, long long def = 0 ){
	auto it = kv.find( k );
	return it == kv.end() ? def : it->second;
}

struct GSSc { const char* name; sc_type type; };
const GSSc g_st_sc[] = {
#include "golden_status_sc.inc"
};

struct GSSk { const char* name; uint16 id; };
const GSSk g_st_sk[] = {
	{ "SA_FREECAST", SA_FREECAST }, { "LG_EXEEDBREAK", LG_EXEEDBREAK }, { "NC_MADOLICENCE", NC_MADOLICENCE },
	{ "RA_WUGRIDER", RA_WUGRIDER }, { "RG_TUNNELDRIVE", RG_TUNNELDRIVE }, { "TF_MISS", TF_MISS }, { "MC_PUSHCART", MC_PUSHCART },
	{ "BA_MUSICALLESSON", BA_MUSICALLESSON }, { "DC_DANCINGLESSON", DC_DANCINGLESSON }, { "NV_TRANSCENDENCE", NV_TRANSCENDENCE },
	{ "CR_TRUST", CR_TRUST }, { "SU_SPRITEMABLE", SU_SPRITEMABLE }, { "SU_POWEROFSEA", SU_POWEROFSEA }, { "NV_BREAKTHROUGH", NV_BREAKTHROUGH },
	{ "SL_KAINA", SL_KAINA }, { "RA_RESEARCHTRAP", RA_RESEARCHTRAP }, { "WM_LESSON", WM_LESSON }, { "HP_MEDITATIO", HP_MEDITATIO },
	{ "HW_SOULDRAIN", HW_SOULDRAIN }, { "SU_TUNABELLY", SU_TUNABELLY }, { "SU_TUNAPARTY", SU_TUNAPARTY }, { "SU_BUNCHOFSHRIMP", SU_BUNCHOFSHRIMP },
	{ "SU_FRESHSHRIMP", SU_FRESHSHRIMP }, { "SU_GROOMING", SU_GROOMING }, { "SU_PURRING", SU_PURRING }, { "SU_SHRIMPARTY", SU_SHRIMPARTY },
};

sc_type g_st_find( const std::string& name ){
	for( const GSSc& e : g_st_sc ) if( name == e.name ) return e.type;
	fprintf( stderr, "golden status: unknown status '%s'\n", name.c_str() );
	exit( 2 );
}

int16 g_st_m = -1;
alignas( map_session_data ) char g_st_pc_buf[sizeof( map_session_data )];
alignas( mob_data ) char g_st_mob_buf[sizeof( mob_data )];
status_data g_st_mob_base; // base_status of the synthetic mob (separate from md->status, as for a real spawned mob)
bool g_st_pc_live = false, g_st_mob_live = false;
item_data g_st_items[2];

void g_st_fill_status( status_data& st, const GSKV& kv ){
	st = status_data();
	st.hp = st.max_hp = 100000;
	st.sp = st.max_sp = 1000;
	st.amotion = 500; st.adelay = 1000; st.dmotion = 400;
	st.mode = MD_CANMOVE | MD_CANATTACK;
	st.speed = 150;
}

block_list* g_st_make_unit( const GSKV& kv ){
	const bool is_mob = gskv( kv, "ut" ) == 1;
	if( !is_mob ){
		if( g_st_pc_live ){ map_deliddb( (map_session_data*)g_st_pc_buf ); ( (map_session_data*)g_st_pc_buf )->~map_session_data(); }
		map_session_data* sd = new( g_st_pc_buf ) map_session_data();
		g_st_pc_live = true;
		sd->type = BL_PC; sd->id = 150000; sd->status.char_id = 150000;
		sd->m = g_st_m; sd->x = 150; sd->y = 150;
		sd->state.active = 1;
		sd->fd = 0;
		sd->status.base_level = gskv( kv, "level", 1 ); sd->status.job_level = 1;
		const long long job = gskv( kv, "job" ); // 0 swordman, 1 assassin
		sd->status.class_ = job == 1 ? JOB_ASSASSIN : JOB_SWORDMAN;
		sd->class_ = job == 1 ? MAPID_ASSASSIN : MAPID_SWORDMAN;
		sd->status.sex = gskv( kv, "sex" ) ? 1 : 0;
		for( int i = 0; i < EQI_MAX; i++ ) sd->equip_index[i] = -1;
		unit_dataset( sd );
		g_st_fill_status( sd->battle_status, kv );
		sd->base_status = sd->battle_status;
		sd->status.weapon = (int16)gskv( kv, "w" );
		sd->weapontype1 = (int16)gskv( kv, "w" );
		sd->state.lr_flag = gskv( kv, "lr" ) ? LR_FLAG_WEAPON : LR_FLAG_NONE;
		sd->state.permanent_speed = gskv( kv, "perm_speed" ) ? 1 : 0;
		sd->sc.option = (uint32)gskv( kv, "opt" );
		sd->bonus.hp = (int32)gskv( kv, "bonus.hp" ); sd->bonus.sp = (int32)gskv( kv, "bonus.sp" );
		sd->bonus.speed_rate = (int16)gskv( kv, "bonus.speed_rate" );
		sd->bonus.speed_add_rate = (int16)gskv( kv, "bonus.speed_add_rate" );
		for( int h = 0; h < 2; h++ ){
			item_data& it = g_st_items[h];
			it = item_data();
			it.type = IT_WEAPON;
			it.weapon_level = gskv( kv, h ? "lwlv" : "rwlv", 0 );
			sd->inventory_data[h] = &it;
			sd->inventory.u.items_inventory[h].nameid = 1201;
			sd->inventory.u.items_inventory[h].amount = 1;
			if( it.weapon_level > 0 ) sd->equip_index[h ? EQI_HAND_L : EQI_HAND_R] = h;
		}
		for( const GSSk& sk : g_st_sk ){
			const long long lv = gskv( kv, std::string( "sk." ) + sk.name );
			const uint16 idx = skill_db.get_index( sk.id, false, __FUNCTION__, __FILE__, __LINE__ );
			if( lv > 0 && idx != 0 ){ sd->status.skill[idx].id = sk.id; sd->status.skill[idx].lv = lv; }
		}
		if( gskv( kv, "skilltimer" ) ){
			sd->ud.skilltimer = 1; // any valid timer id: only compared against INVALID_TIMER
			sd->ud.skill_id = (uint16)gskv( kv, "ud.skill" );
			sd->ud.skill_lv = (uint16)gskv( kv, "ud.lv" );
		}
		map_addiddb( sd ); // status_get_hpbonus / status_get_spbonus look the player up by id (map_id2sd)
		return sd;
	}
	if( g_st_mob_live ) ( (mob_data*)g_st_mob_buf )->~mob_data();
	mob_data* md = new( g_st_mob_buf ) mob_data();
	g_st_mob_live = true;
	md->type = BL_MOB; md->id = 200000;
	md->m = g_st_m; md->x = 150; md->y = 150;
	md->mob_id = 1002;
	md->db = mob_db.find( md->mob_id );
	md->level = gskv( kv, "level", 1 );
	status_set_viewdata( md, md->mob_id );
	unit_dataset( md );
	md->spawn_timer = INVALID_TIMER; md->deletetimer = INVALID_TIMER; md->skill_idx = -1;
	md->damagetaken = 100;
	g_st_fill_status( md->status, kv );
	g_st_mob_base = md->status;
	md->base_status = &g_st_mob_base;
	return md;
}

struct GSCfg { const char* name; int32* ref; int32 def; };
GSCfg g_st_cfg[] = {
	{ "gvg_flee_penalty", &battle_config.gvg_flee_penalty, 0 }, { "bg_flee_penalty", &battle_config.bg_flee_penalty, 0 },
	{ "max_walk_speed", &battle_config.max_walk_speed, 0 }, { "rental_mount_speed_boost", &battle_config.rental_mount_speed_boost, 0 },
	{ "pc_damage_delay_rate", &battle_config.pc_damage_delay_rate, 0 }, { "enable_baseatk", &battle_config.enable_baseatk, 0 },
};

// the status_data members the chain (status_calc_bl_main) case reads / writes: CF( output name, member )
#define GCHAIN_FIELDS \
	CF( str, str ) CF( agi, agi ) CF( vit, vit ) CF( int, int_ ) CF( dex, dex ) CF( luk, luk ) CF( batk, batk ) \
	CF( rhw_atk, rhw.atk ) CF( rhw_atk2, rhw.atk2 ) CF( lhw_atk, lhw.atk ) CF( lhw_atk2, lhw.atk2 ) CF( rhw_ele, rhw.ele ) CF( lhw_ele, lhw.ele ) \
	CF( hit, hit ) CF( flee, flee ) CF( flee2, flee2 ) CF( cri, cri ) CF( def, def ) CF( def2, def2 ) CF( mdef, mdef ) CF( mdef2, mdef2 ) \
	CF( speed, speed ) CF( amotion, amotion ) CF( adelay, adelay ) CF( aspd_rate, aspd_rate ) CF( max_hp, max_hp ) CF( max_sp, max_sp ) \
	CF( def_ele, def_ele ) CF( ele_lv, ele_lv ) CF( matk_min, matk_min ) CF( matk_max, matk_max )

struct GSCant { const char* name; bool status_change::* ref; };

void g_st_cant_out( FILE* f, const status_change& sc ){
	fprintf( f, " move=%d cast=%d chat=%d attack=%d warp=%d pickup=%d drop=%d equip=%d unequip=%d consume=%d deathpenalty=%d interact=%d",
		sc.cant.move, sc.cant.cast, sc.cant.chat, sc.cant.attack, sc.cant.warp, sc.cant.pickup, sc.cant.drop, sc.cant.equip,
		sc.cant.unequip, sc.cant.consume, sc.cant.deathpenalty, sc.cant.interact );
}

void g_st_dump_consts( const char* path ){
	FILE* f = fopen( path, "wb" );
	if( !f ) return;
#define GK( n ) fprintf( f, #n " %lld\n", (long long)( n ) );
#include "golden_status_consts.inc"
#undef GK
	// db facts the Luau side needs: the walk-speed bounds, the cap values used by the clamps
	fprintf( f, "MIN_ASPD_VALUE %d\n", (int)MIN_ASPD );
	// the status database as the real loader holds it (db/pre-re/status.yml + import): states, calc flags, flags, lists
	for( const GSSc& e : g_st_sc ){
		std::shared_ptr<s_status_change_db> d = status_db.find( e.type );
		if( d == nullptr ){ fprintf( f, "DB %s -\n", e.name ); continue; }
		bool first;
		fprintf( f, "DB %s state=", e.name ); first = true;
#define GS( n ) if( d->state[SCS_##n] ){ fprintf( f, "%s%s", first ? "" : ",", #n ); first = false; }
#include "golden_status_scs.inc"
#undef GS
		if( first ) fputc( '-', f );
		fprintf( f, " calc=" ); first = true;
#define GS( n ) if( d->calc_flag[SCB_##n] ){ fprintf( f, "%s%s", first ? "" : ",", #n ); first = false; }
#include "golden_status_scb.inc"
#undef GS
		if( first ) fputc( '-', f );
		fprintf( f, " flag=" ); first = true;
#define GS( n ) if( d->flag[SCF_##n] ){ fprintf( f, "%s%s", first ? "" : ",", #n ); first = false; }
#include "golden_status_scf.inc"
#undef GS
		if( first ) fputc( '-', f );
		const std::vector<sc_type>* lists[4] = { &d->fail, &d->endonstart, &d->endreturn, &d->endonend };
		const char* lname[4] = { "fail", "end_on_start", "end_return", "end_on_end" };
		for( int li = 0; li < 4; li++ ){
			fprintf( f, " %s=", lname[li] );
			if( lists[li]->empty() ) fputc( '-', f );
			for( size_t k = 0; k < lists[li]->size(); k++ ){
				const char* nm = "?";
				for( const GSSc& x : g_st_sc ) if( x.type == ( *lists[li] )[k] ){ nm = x.name; break; }
				fprintf( f, "%s%s", k ? "," : "", nm );
			}
		}
		fprintf( f, " opt1=%u opt2=%u opt3=%u look=%u skill=%u min_rate=%u min_duration=%lld script=%d\n", (unsigned)d->opt1, (unsigned)d->opt2,
			(unsigned)d->opt3, (unsigned)d->look, (unsigned)d->skill_id, (unsigned)d->min_rate, (long long)d->min_duration, d->script != nullptr ? 1 : 0 );
	}
	{ // script constants (RA_GOLDEN_ST_SCRIPTK=name,name,...): the numeric values of the identifiers the status scripts use
		const char* names = getenv( "RA_GOLDEN_ST_SCRIPTK" );
		if( names ){
			std::stringstream ns( names );
			std::string nm;
			while( std::getline( ns, nm, ',' ) ){
				int64 val = 0;
				if( script_get_constant( nm.c_str(), &val ) ) fprintf( f, "SCRIPTK %s %lld\n", nm.c_str(), (long long)val );
				else fprintf( f, "SCRIPTK %s ?\n", nm.c_str() );
			}
		}
	}
	for( const GSCfg& c : g_st_cfg ) fprintf( f, "CFG %s %d\n", c.name, (int)*c.ref );   // the effective battle.conf defaults of the values a case may override
	for( const GSSk& sk : g_st_sk ) fprintf( f, "SKDB %s %d\n", sk.name, skill_db.get_index( sk.id, true, __FUNCTION__, __FILE__, __LINE__ ) != 0 ? 1 : 0 );
	for( const GSSc& e : g_st_sc ) fprintf( f, "SC %s %d\n", e.name, (int)e.type );
	fclose( f );
}

} // namespace

void golden_status_main(){
	const char* consts = getenv( "RA_GOLDEN_ST_CONSTS" );
	if( consts && *consts ) g_st_dump_consts( consts );
	const char* cases = getenv( "RA_GOLDEN_ST_CASES" );
	const char* out = getenv( "RA_GOLDEN_ST_OUT" );
	if( !cases || !*cases || !out || !*out ){
		if( consts && *consts ) exit( 0 );
		return;
	}
	std::ifstream in( cases );
	FILE* fo = fopen( out, "wb" );
	if( !in || !fo ){ fprintf( stderr, "golden status: cannot open files\n" ); exit( 2 ); }

	g_st_m = map_mapname2mapid( "prontera" );
	map_data* mapdata = map_getmapdata( g_st_m );
	for( GSCfg& c : g_st_cfg ) c.def = *c.ref;

	std::string line;
	long n = 0;
	while( std::getline( in, line ) ){
		if( line.empty() || line[0] == '#' ) continue;
		GSKV kv;
		std::map<std::string, std::string> sv;
		std::string fn, changed, flags_s;
		std::stringstream ss( line );
		std::string tok;
		while( ss >> tok ){
			size_t eq = tok.find( '=' );
			if( eq == std::string::npos ) continue;
			std::string k = tok.substr( 0, eq ), v = tok.substr( eq + 1 );
			if( k.compare( 0, 3, "sc." ) == 0 ) sv[k.substr( 3 )] = v;
			else if( k == "fn" ) fn = v;
			else if( k == "changed" ) changed = v;
			else if( k == "flags" ) flags_s = v;
			else if( k == "ud.skill" ){
				kv[k] = 0;
				for( const GSSk& sk : g_st_sk ) if( v == sk.name ) kv[k] = sk.id;
			}
			else kv[k] = atoll( v.c_str() );
		}
		for( GSCfg& c : g_st_cfg ) *c.ref = (int32)gskv( kv, std::string( "cfg." ) + c.name, c.def );
		const int mapmode = (int)gskv( kv, "map" );
		mapdata->setMapFlag( MF_GVG, mapmode == 1 );
		mapdata->setMapFlag( MF_BATTLEGROUND, mapmode == 2 );

		block_list* bl = g_st_make_unit( kv );
		status_change* sc = status_get_sc( bl );
		for( auto& e : sv ){
			status_change_entry* sce = sc->createSCE( g_st_find( e.first ) );
			sce->timer = INVALID_TIMER;
			int32* v[4] = { &sce->val1, &sce->val2, &sce->val3, &sce->val4 };
			std::stringstream vs( e.second );
			std::string t;
			int i = 0;
			while( i < 4 && std::getline( vs, t, ',' ) ) *v[i++] = atoi( t.c_str() );
		}
		const int32 base = (int32)gskv( kv, "base" );
		map_session_data* sd = BL_CAST( BL_PC, bl );
		long long r = 0;
		bool ok = true;
		bool is_state = false, is_chain = false, is_script = false;
		std::string chain_out, script_out;

#define F( name, call ) else if( fn == name ){ r = (long long)( call ); }
		if( false ){}
		F( "str", status_calc_str( bl, sc, base ) ) F( "agi", status_calc_agi( bl, sc, base ) ) F( "vit", status_calc_vit( bl, sc, base ) )
		F( "int", status_calc_int( bl, sc, base ) ) F( "dex", status_calc_dex( bl, sc, base ) ) F( "luk", status_calc_luk( bl, sc, base ) )
		F( "batk", status_calc_batk( bl, sc, base ) ) F( "watk", status_calc_watk( bl, sc, base ) )
		F( "hit", status_calc_hit( bl, sc, base ) ) F( "cri", status_calc_critical( bl, sc, base ) )
		F( "flee", status_calc_flee( bl, sc, base ) ) F( "flee2", status_calc_flee2( bl, sc, base ) )
		F( "def", status_calc_def( bl, sc, base ) ) F( "def2", status_calc_def2( bl, sc, base ) )
		F( "mdef", status_calc_mdef( bl, sc, base ) ) F( "mdef2", status_calc_mdef2( bl, sc, base ) )
		F( "speed", status_calc_speed( bl, sc, base ) )
		F( "aspd_rate", status_calc_aspd_rate( bl, sc, base ) ) F( "fix_aspd", status_calc_fix_aspd( bl, sc, base ) )
		F( "patk", status_calc_patk( bl, sc, base ) ) F( "smatk", status_calc_smatk( bl, sc, base ) )
		F( "res", status_calc_res( bl, sc, base ) ) F( "mres", status_calc_mres( bl, sc, base ) )
		F( "hplus", status_calc_hplus( bl, sc, base ) ) F( "crate", status_calc_crate( bl, sc, base ) )
		F( "pow", status_calc_pow( bl, sc, base ) ) F( "sta", status_calc_sta( bl, sc, base ) ) F( "wis", status_calc_wis( bl, sc, base ) )
		F( "spl", status_calc_spl( bl, sc, base ) ) F( "con", status_calc_con( bl, sc, base ) ) F( "crt", status_calc_crt( bl, sc, base ) )
		F( "element", status_calc_element( bl, sc, base ) ) F( "element_lv", status_calc_element_lv( bl, sc, base ) )
		F( "atk_element", status_calc_attack_element( bl, sc, base ) ) F( "mode", status_calc_mode( bl, sc, base ) )
		F( "maxhp", status_calc_maxhp( bl, (uint64)base ) ) F( "maxsp", status_calc_maxsp( bl, (uint64)base ) )
		F( "matk", status_calc_pseudobuff_matk( sd, sc, base ) )
		F( "hp_fix", status_get_hpbonus( bl, STATUS_BONUS_FIX ) ) F( "hp_rate", status_get_hpbonus( bl, STATUS_BONUS_RATE ) )
		F( "sp_fix", status_get_spbonus( bl, STATUS_BONUS_FIX ) ) F( "sp_rate", status_get_spbonus( bl, STATUS_BONUS_RATE ) )
		else if( fn == "script" ){
			// the Script: of every active status that has one, in ascending sc_type order (status_calc_pc_sub iterates an unordered map; the
			// bonuses add up, so only the set of calls matters, and the Luau side compares the same ascending order)
			is_script = true;
			std::vector<std::vector<long long>> blog;
			std::vector<sc_type> types;
			for( const auto& it : *sc ) types.push_back( it.first );
			std::sort( types.begin(), types.end() );
			g_golden_bonus_log = &blog;
			for( sc_type t : types ){
				std::shared_ptr<s_status_change_db> d = status_db.find( t );
				if( d != nullptr && d->script != nullptr ) ScriptHost::run_item_script( *sd, d->script );
			}
			g_golden_bonus_log = nullptr;
			script_out = "";
			for( size_t i = 0; i < blog.size(); i++ ){
				for( size_t k = 0; k < blog[i].size(); k++ ) script_out += ( k ? "," : ( i ? ";" : "" ) ) + std::to_string( blog[i][k] );
			}
			if( script_out.empty() ) script_out = "-";
		}
		else if( fn == "chain" ){
			is_chain = true;
			// base status b.* (the unit's base_status), battle status s.* (default: the base values = what a first calculation starts from)
			status_data* bst = status_get_base_status( bl );
			status_data* st = status_get_status_data( *bl );
#define CF( nm, mem ) bst->mem = (std::remove_reference_t<decltype( bst->mem )>)gskv( kv, std::string( "b." ) + #nm ); \
	st->mem = (std::remove_reference_t<decltype( st->mem )>)gskv( kv, std::string( "s." ) + #nm, (long long)bst->mem );
			GCHAIN_FIELDS
#undef CF
			bst->hp = st->hp = bst->max_hp; bst->sp = st->sp = bst->max_sp;
			if( sd ){ sd->matk_rate = 100; sd->bonus.weapon_atk_rate = (int16)gskv( kv, "bonus.weapon_atk_rate" ); }
			std::bitset<SCB_MAX> flags;
			{
				std::stringstream fs( flags_s );
				std::string t;
				while( std::getline( fs, t, ',' ) ){
					bool known = false;
#define GS( n ) if( t == #n ){ flags.set( SCB_##n ); known = true; }
#include "golden_status_scb.inc"
#undef GS
					if( !known ){ fprintf( stderr, "golden status: unknown flag '%s'\n", t.c_str() ); exit( 2 ); }
				}
			}
			status_calc_bl_main( *bl, flags );
			chain_out = "";
#define CF( nm, mem ) chain_out += std::string( " " ) + #nm + "=" + std::to_string( (long long)st->mem );
			GCHAIN_FIELDS
#undef CF
		}
		else if( fn == "state" ){
			is_state = true;
			// initial flags (the function only rewrites the ones the changed status touches)
			sc->cant.move = gskv( kv, "cant.move" ); sc->cant.cast = gskv( kv, "cant.cast" ); sc->cant.chat = gskv( kv, "cant.chat" );
			sc->cant.attack = gskv( kv, "cant.attack" ); sc->cant.warp = gskv( kv, "cant.warp" ); sc->cant.pickup = gskv( kv, "cant.pickup" );
			sc->cant.drop = gskv( kv, "cant.drop" ); sc->cant.equip = gskv( kv, "cant.equip" ); sc->cant.unequip = gskv( kv, "cant.unequip" );
			sc->cant.consume = gskv( kv, "cant.consume" ); sc->cant.deathpenalty = gskv( kv, "cant.deathpenalty" );
			sc->cant.interact = gskv( kv, "cant.interact" );
			std::shared_ptr<s_status_change_db> scdb = status_db.find( g_st_find( changed ) );
			if( scdb == nullptr ){ fprintf( stderr, "golden status: no db entry for %s\n", changed.c_str() ); exit( 2 ); }
			status_calc_state( *bl, *sc, scdb, gskv( kv, "start" ) != 0 );
		}
		else ok = false;
#undef F
		if( !ok ){ fprintf( stderr, "golden status: unknown fn '%s'\n", fn.c_str() ); exit( 2 ); }
		fprintf( fo, "id=%lld fn=%s", gskv( kv, "id" ), fn.c_str() );
		if( is_state ) g_st_cant_out( fo, *sc );
		else if( is_chain ) fputs( chain_out.c_str(), fo );
		else if( is_script ) fprintf( fo, " log=%s", script_out.c_str() );
		else fprintf( fo, " out=%lld", r );
		fputc( '\n', fo );
		n++;
	}
	fclose( fo );
	fprintf( stderr, "golden status: %ld cases written\n", n );
	exit( 0 );
}
