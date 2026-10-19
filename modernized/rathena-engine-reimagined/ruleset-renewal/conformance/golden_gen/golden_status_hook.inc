// RENEWAL status-calculation golden hook (rAthena COPY only; included at the very end of src/map/status.cpp by apply_edits.py so that it can call
// the file-static status_calc_* functions).
//
// Reads synthetic cases (RA_GOLDEN_ST_CASES, one per line, `key=value` tokens), builds ONE synthetic unit (map_session_data / mob_data / homun_data)
// with explicit base data and an explicit active status set {sc_type, val1..val4}, calls the REAL static status_calc_* functions that the case asks
// for and writes the results (RA_GOLDEN_ST_OUT). Nothing is derived: every number that a function reads is an input of the case.
//
// Case keys (integers unless noted; missing = 0):
//   id=N
//   bl=0|1|2        unit kind: 0 player (map_session_data), 1 monster (mob_data), 2 homunculus (homun_data; only block_list::type and the status data are read)
//   nosc=1          pass sc == nullptr to the function instead of the unit's status_change
//   lv, vit         base level; battle-status VIT (status_get_vit(bl): the renewal Angelus term of status_calc_def2)
//   weapon, sex, class_  (sd->status.weapon, sd->status.sex, sd->class_)         option (sc.option bits: riding, dragon, wug rider, mado gear, cart)
//   perm_speed, skilltimer(1 = a skill is being cast), ud_skill_id, ud_skill_lv   bonus_speed_rate, bonus_speed_add_rate, bonus_hp, bonus_sp, bonus_ap
//   map=0|1|2       0 normal, 1 GvG, 2 battleground (status_calc_flee reads the map flags)
//   cfg.gvg_flee_penalty, cfg.bg_flee_penalty, cfg.rental_mount_speed_boost      per-case battle.conf values (restored after the case)
//   skill.NAME=lv   (players) skill levels by script constant (skill_name2id), read through pc_checkskill
//   summoner_sea=N  the SUMMONER_POWER_SEA threshold input: sets the lv of the seven skills that pc_checkskill_summoner sums (SU_TUNABELLY .. SU_SHRIMPARTY)
//   sc.SC_NAME=v1[,v2[,v3[,v4]]]   status entries
//   in.states=1     also record sc->cant as a bit mask (move pickup drop cast chat equip unequip consume attack warp deathpenalty interact = bits 0..11)
//   in.<fn>=N       call fn with initial value N. fn is one of: str agi vit int dex luk pow sta wis spl con crt batk watk matk_flat cri hit flee flee2 def
//                   def2 mdef mdef2 speed aspd_fixed aspd_pct fix_aspd aspd_rate patk smatk res mres hplus crate maxhp maxsp maxap def_ele ele_lv atk_ele mode
// Output: `id=N fn=value ...` (the functions in the order of g_fn below), or `id=N skip=reason` when the case is not valid for the unit kind.
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "homunculus.hpp"
#include "mob.hpp"
#include "pc.hpp"
#include "unit.hpp"

void golden_status_main();

namespace {

struct GStSc { const char* name; sc_type type; };
const GStSc g_st_sc[] = {
#include "golden_status_table.inc"
};

typedef std::map<std::string, long long> GStKV;

long long gst( const GStKV& kv, const std::string& k, long long def = 0 ){
	auto it = kv.find( k );
	return it == kv.end() ? def : it->second;
}

alignas( map_session_data ) char g_st_pc_buf[sizeof( map_session_data )];
alignas( mob_data ) char g_st_mob_buf[sizeof( mob_data )];
alignas( homun_data ) char g_st_hom_buf[sizeof( homun_data )];
bool g_st_pc_live = false, g_st_mob_live = false, g_st_hom_live = false;
int16 g_st_map = -1;

void g_st_status( status_data& st, const GStKV& kv ){
	st = status_data();
	st.hp = st.max_hp = 100000; st.sp = st.max_sp = 1000;
	st.vit = gst( kv, "vit" );
	st.mode = MD_CANMOVE | MD_CANATTACK;
}

void g_st_add_sc( status_change& sc, const std::string& name, const std::string& vals ){
	for( const GStSc& e : g_st_sc ){
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
	fprintf( stderr, "golden_status: unknown status '%s'\n", name.c_str() );
	exit( 2 );
}

// the functions in output order; kind of the call decides the signature
enum GFn { F_STR, F_AGI, F_VIT, F_INT, F_DEX, F_LUK, F_POW, F_STA, F_WIS, F_SPL, F_CON, F_CRT, F_BATK, F_WATK, F_MATK_FLAT, F_CRI, F_HIT, F_FLEE, F_FLEE2, F_DEF,
	F_DEF2, F_MDEF, F_MDEF2, F_SPEED, F_ASPD_FIXED, F_ASPD_PCT, F_FIX_ASPD, F_ASPD_RATE, F_PATK, F_SMATK, F_RES, F_MRES, F_HPLUS, F_CRATE, F_MAXHP, F_MAXSP,
	F_MAXAP, F_DEF_ELE, F_ELE_LV, F_ATK_ELE, F_MODE, F_COUNT };
const char* g_fn[F_COUNT] = { "str", "agi", "vit", "int", "dex", "luk", "pow", "sta", "wis", "spl", "con", "crt", "batk", "watk", "matk_flat", "cri", "hit",
	"flee", "flee2", "def", "def2", "mdef", "mdef2", "speed", "aspd_fixed", "aspd_pct", "fix_aspd", "aspd_rate", "patk", "smatk", "res", "mres", "hplus",
	"crate", "maxhp", "maxsp", "maxap", "def_ele", "ele_lv", "atk_ele", "mode" };

long long g_st_call( GFn f, block_list* bl, status_change* sc, long long in ){
	switch( f ){
		case F_STR: return status_calc_str( bl, sc, (int32)in );
		case F_AGI: return status_calc_agi( bl, sc, (int32)in );
		case F_VIT: return status_calc_vit( bl, sc, (int32)in );
		case F_INT: return status_calc_int( bl, sc, (int32)in );
		case F_DEX: return status_calc_dex( bl, sc, (int32)in );
		case F_LUK: return status_calc_luk( bl, sc, (int32)in );
		case F_POW: return status_calc_pow( bl, sc, (int32)in );
		case F_STA: return status_calc_sta( bl, sc, (int32)in );
		case F_WIS: return status_calc_wis( bl, sc, (int32)in );
		case F_SPL: return status_calc_spl( bl, sc, (int32)in );
		case F_CON: return status_calc_con( bl, sc, (int32)in );
		case F_CRT: return status_calc_crt( bl, sc, (int32)in );
		case F_BATK: return status_calc_batk( bl, sc, (int32)in );
		case F_WATK: return status_calc_watk( bl, sc, (int32)in );
		case F_MATK_FLAT: return status_calc_pseudobuff_matk( BL_CAST( BL_PC, bl ), sc, (int32)in );
		case F_CRI: return status_calc_critical( bl, sc, (int32)in );
		case F_HIT: return status_calc_hit( bl, sc, (int32)in );
		case F_FLEE: return status_calc_flee( bl, sc, (int32)in );
		case F_FLEE2: return status_calc_flee2( bl, sc, (int32)in );
		case F_DEF: return status_calc_def( bl, sc, (int32)in );
		case F_DEF2: return status_calc_def2( bl, sc, (int32)in );
		case F_MDEF: return status_calc_mdef( bl, sc, (int32)in );
		case F_MDEF2: return status_calc_mdef2( bl, sc, (int32)in );
		case F_SPEED: return status_calc_speed( bl, sc, (int32)in );
		case F_ASPD_FIXED: return status_calc_aspd( bl, sc, true );
		case F_ASPD_PCT: return status_calc_aspd( bl, sc, false );
		case F_FIX_ASPD: return status_calc_fix_aspd( bl, sc, (int32)in );
		case F_ASPD_RATE: return status_calc_aspd_rate( bl, sc, (int32)in );
		case F_PATK: return status_calc_patk( bl, sc, (int32)in );
		case F_SMATK: return status_calc_smatk( bl, sc, (int32)in );
		case F_RES: return status_calc_res( bl, sc, (int32)in );
		case F_MRES: return status_calc_mres( bl, sc, (int32)in );
		case F_HPLUS: return status_calc_hplus( bl, sc, (int32)in );
		case F_CRATE: return status_calc_crate( bl, sc, (int32)in );
		case F_MAXHP: return status_calc_maxhp( bl, (uint64)in );
		case F_MAXSP: return status_calc_maxsp( bl, (uint64)in );
		case F_MAXAP: return status_calc_maxap( bl, (uint64)in );
		case F_DEF_ELE: return status_calc_element( bl, sc, (int32)in );
		case F_ELE_LV: return status_calc_element_lv( bl, sc, (int32)in );
		case F_ATK_ELE: return status_calc_attack_element( bl, sc, (int32)in );
		case F_MODE: return status_calc_mode( bl, sc, (int32)in );
		default: break;
	}
	return 0;
}

struct GStCfg { const char* name; int32* ref; int32 def; };
GStCfg g_st_cfg[] = {
	{ "gvg_flee_penalty", &battle_config.gvg_flee_penalty, 0 }, { "bg_flee_penalty", &battle_config.bg_flee_penalty, 0 },
	{ "rental_mount_speed_boost", &battle_config.rental_mount_speed_boost, 0 },
};

block_list* g_st_make( const GStKV& kv, const std::map<std::string, std::string>& sv ){
	const int kind = gst( kv, "bl" );
	const int32 lv = gst( kv, "lv", 1 );
	if( kind == 0 ){
		if( g_st_pc_live ){ map_deliddb( (map_session_data*)g_st_pc_buf ); ( (map_session_data*)g_st_pc_buf )->~map_session_data(); }
		map_session_data* sd = new( g_st_pc_buf ) map_session_data();
		g_st_pc_live = true;
		sd->type = BL_PC; sd->id = 150000;
		sd->m = g_st_map; sd->x = 150; sd->y = 150;
		sd->state.active = 1; sd->fd = 0;
		sd->status.base_level = lv; sd->status.job_level = 1;
		sd->status.sex = gst( kv, "sex" ) ? SEX_MALE : SEX_FEMALE;
		sd->status.weapon = (int16)gst( kv, "weapon" );
		sd->status.class_ = JOB_NOVICE;
		sd->class_ = gst( kv, "class_" );
		for( int i = 0; i < EQI_MAX; i++ ) sd->equip_index[i] = -1;
		unit_dataset( sd );
		g_st_status( sd->battle_status, kv );
		sd->base_status = sd->battle_status;
		sd->sc.option = gst( kv, "option" );
		sd->state.permanent_speed = gst( kv, "perm_speed" ) ? 1 : 0;
		sd->ud.skilltimer = gst( kv, "skilltimer" ) ? 1 : INVALID_TIMER;
		sd->ud.skill_id = gst( kv, "ud_skill_id" ); sd->ud.skill_lv = gst( kv, "ud_skill_lv" );
		sd->bonus.speed_rate = gst( kv, "bonus_speed_rate" ); sd->bonus.speed_add_rate = gst( kv, "bonus_speed_add_rate" );
		sd->bonus.hp = gst( kv, "bonus_hp" ); sd->bonus.sp = gst( kv, "bonus_sp" ); sd->bonus.ap = gst( kv, "bonus_ap" );
		const std::string pref = "skill.";
		for( auto& e : kv ) if( e.first.compare( 0, pref.size(), pref ) == 0 ){
			const uint16 sid = skill_name2id( e.first.substr( pref.size() ).c_str() );
			if( sid == 0 ){ fprintf( stderr, "golden_status: unknown skill %s\n", e.first.c_str() ); exit( 2 ); }
			const uint16 idx = skill_db.get_index( sid, false, __FUNCTION__, __FILE__, __LINE__ );
			if( idx == 0 ){ fprintf( stderr, "golden_status: no skill index for %s\n", e.first.c_str() ); exit( 2 ); }
			sd->status.skill[idx].id = sid; sd->status.skill[idx].lv = e.second;
		}
		if( const long long sea = gst( kv, "summoner_sea" ) ){
			for( uint16 sid : { SU_TUNABELLY, SU_TUNAPARTY, SU_BUNCHOFSHRIMP, SU_FRESHSHRIMP, SU_GROOMING, SU_PURRING, SU_SHRIMPARTY } ){
				const uint16 idx = skill_db.get_index( sid, false, __FUNCTION__, __FILE__, __LINE__ );
				if( idx ){ sd->status.skill[idx].id = sid; sd->status.skill[idx].lv = sea; }
			}
		}
		map_addiddb( sd ); // status_get_hpbonus() resolves the player through map_id2sd()
		return sd;
	}
	if( kind == 1 ){
		if( g_st_mob_live ) ( (mob_data*)g_st_mob_buf )->~mob_data();
		mob_data* md = new( g_st_mob_buf ) mob_data();
		g_st_mob_live = true;
		md->type = BL_MOB; md->id = 200000;
		md->m = g_st_map; md->x = 150; md->y = 150;
		md->mob_id = 1002;
		md->db = mob_db.find( md->mob_id );
		md->level = lv;
		status_set_viewdata( md, md->mob_id );
		unit_dataset( md );
		md->spawn_timer = INVALID_TIMER; md->deletetimer = INVALID_TIMER; md->skill_idx = -1;
		g_st_status( md->status, kv );
		md->base_status = &md->status;
		return md;
	}
	if( g_st_hom_live ) ( (homun_data*)g_st_hom_buf )->~homun_data();
	homun_data* hd = new( g_st_hom_buf ) homun_data();
	g_st_hom_live = true;
	hd->type = BL_HOM; hd->id = 250000;
	hd->m = g_st_map; hd->x = 150; hd->y = 150;
	unit_dataset( hd );
	g_st_status( hd->battle_status, kv );
	hd->base_status = hd->battle_status;
	return hd;
}

} // namespace

// RA_GOLDEN_ST_DUMP: the constants the Luau status layer uses (scripts/status/consts.luau is generated from this dump by gen_status_consts.py)
void g_st_dump( const char* path ){
	FILE* f = fopen( path, "wb" );
	if( !f ) return;
#define GC( n ) fprintf( f, "const " #n " %lld\n", (long long)( n ) );
	GC( RINGNBL_ASPDRATE ) GC( RINGNBL_ATKRATE ) GC( RINGNBL_MATKRATE ) GC( RINGNBL_HPRATE ) GC( RINGNBL_SPRATE ) GC( RINGNBL_ALLSTAT ) GC( RINGNBL_HIT )
	GC( RINGNBL_FLEE ) GC( RINGNBL_SPCONSUM ) GC( RINGNBL_HPREGEN ) GC( RINGNBL_SPREGEN ) GC( SL_HIGH ) GC( SL_BARDDANCER ) GC( BREAK_ANKLE ) GC( BREAK_WRIST ) GC( BREAK_KNEE )
	GC( BREAK_SHOULDER ) GC( BREAK_WAIST ) GC( BREAK_NECK ) GC( BCT_SELF ) GC( BCT_ENEMY ) GC( MANNER_NOCHAT ) GC( MANNER_NOITEM ) GC( OPTION_RIDING )
	GC( OPTION_WUGRIDER ) GC( OPTION_MADOGEAR ) GC( OPTION_DRAGON ) GC( MAPID_SECONDMASK ) GC( MAPID_ASSASSIN ) GC( W_BOW ) GC( W_REVOLVER ) GC( W_RIFLE )
	GC( W_GATLING ) GC( W_SHOTGUN ) GC( W_GRENADE ) GC( MD_MASK ) GC( MD_NONE ) GC( ELE_NEUTRAL ) GC( ELE_WATER ) GC( ELE_EARTH ) GC( ELE_FIRE ) GC( ELE_WIND )
	GC( ELE_POISON ) GC( ELE_HOLY ) GC( ELE_DARK ) GC( ELE_GHOST ) GC( ELE_UNDEAD ) GC( CG_MOONLIT ) GC( CG_HERMODE ) GC( LG_EXEEDBREAK ) GC( DEFTYPE_MIN )
	GC( DEFTYPE_MAX ) GC( MIN_WALK_SPEED ) GC( MAX_WALK_SPEED ) GC( MIN_ASPD ) GC( AMOTION_ZERO_ASPD ) GC( AMOTION_INTERVAL ) GC( SHRT_MAX ) GC( SHRT_MIN )
	GC( USHRT_MAX ) GC( UINT_MAX ) GC( INT_MAX ) GC( UCHAR_MAX ) GC( LR_FLAG_NONE ) GC( BL_PC ) GC( BL_MOB ) GC( BL_HOM ) GC( SCS_MAX )
#undef GC
	for( const GStSc& e : g_st_sc ) fprintf( f, "sc %s %d\n", e.name, (int)e.type );
	fclose( f );
}

void golden_status_main(){
	if( const char* dump = getenv( "RA_GOLDEN_ST_DUMP" ) ) if( *dump ){ g_st_dump( dump ); if( !getenv( "RA_GOLDEN_ST_CASES" ) ) exit( 0 ); }
	const char* cases = getenv( "RA_GOLDEN_ST_CASES" );
	const char* out = getenv( "RA_GOLDEN_ST_OUT" );
	if( !cases || !*cases || !out || !*out ) return;
	std::ifstream in( cases );
	FILE* fo = fopen( out, "wb" );
	if( !in || !fo ){ fprintf( stderr, "golden_status: cannot open files\n" ); exit( 2 ); }

	g_st_map = map_mapname2mapid( "prontera" );
	map_data* mapdata = map_getmapdata( g_st_map );
	for( GStCfg& c : g_st_cfg ) c.def = *c.ref;

	std::string line;
	long n = 0;
	while( std::getline( in, line ) ){
		if( line.empty() || line[0] == '#' ) continue;
		GStKV kv;
		std::map<std::string, std::string> sv;
		std::stringstream ss( line );
		std::string tok;
		while( ss >> tok ){
			const size_t eq = tok.find( '=' );
			if( eq == std::string::npos ) continue;
			const std::string k = tok.substr( 0, eq ), v = tok.substr( eq + 1 );
			if( k.compare( 0, 3, "sc." ) == 0 ) sv[k.substr( 3 )] = v;
			else kv[k] = atoll( v.c_str() );
		}
		for( GStCfg& c : g_st_cfg ) *c.ref = (int32)gst( kv, std::string( "cfg." ) + c.name, c.def );
		const int mapmode = gst( kv, "map" );
		mapdata->setMapFlag( MF_GVG, mapmode == 1 );
		mapdata->setMapFlag( MF_BATTLEGROUND, mapmode == 2 );

		block_list* bl = g_st_make( kv, sv );
		status_change* sc = status_get_sc( bl );
		for( auto& e : sv ) g_st_add_sc( *sc, e.first, e.second );

		fprintf( fo, "id=%lld", gst( kv, "id" ) );
		status_change* arg = gst( kv, "nosc" ) ? nullptr : sc;
		for( int f = 0; f < F_COUNT; f++ ){
			auto it = kv.find( std::string( "in." ) + g_fn[f] );
			if( it == kv.end() ) continue;
			fprintf( fo, " %s=%lld", g_fn[f], g_st_call( (GFn)f, bl, arg, it->second ) );
		}
		if( kv.count( "in.states" ) ){
			// status_calc_state() as the legacy code calls it when a status starts: restrictions are the union over the active set,
			// so visiting every active status with start = true leaves sc->cant as the pure function of the set (BR-RULESET-073)
			sc->cant = {};
			std::vector<sc_type> types;
			for( const auto& e : *sc ) types.push_back( e.first );
			std::sort( types.begin(), types.end() );
			for( sc_type t : types ) if( std::shared_ptr<s_status_change_db> db = status_db.find( t ) ) status_calc_state( *bl, *sc, db, true );
			const bool c[12] = { sc->cant.move, sc->cant.pickup, sc->cant.drop, sc->cant.cast, sc->cant.chat, sc->cant.equip, sc->cant.unequip,
				sc->cant.consume, sc->cant.attack, sc->cant.warp, sc->cant.deathpenalty, sc->cant.interact };
			int mask = 0;
			for( int i = 0; i < 12; i++ ) if( c[i] ) mask |= 1 << i;
			fprintf( fo, " states=%d", mask );
		}
		fputc( '\n', fo );
		if( bl->type == BL_PC ) map_deliddb( bl );
		n++;
	}
	fclose( fo );
	fprintf( stderr, "golden_status: %ld cases written\n", n );
	exit( 0 );
}
