// tooling/migrate/legacy_dump: canonical JSON dump of the legacy YAML databases AFTER load (copy-only patch).
// #include'd at the end of src/map/script.cpp (to reach the file-static constant table) and called from
// map.cpp do_init() right after ScriptHost::run_oninit(). No-op unless RA_DBDUMP_DIR is set.
// Output (one JSON object per line, sorted by key): item_db.jsonl mob_db.jsonl skill_db.jsonl status.jsonl
// size_fix.jsonl attr_fix.jsonl refine.jsonl statpoint.jsonl job_stats.jsonl, constants.tsv, mode.txt
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <bitset>
#include "status.hpp"
#include "skill.hpp"
#include "mob.hpp"
#include "pc.hpp"

namespace dbdump {
static std::string q( const std::string& s ){
	std::string o = "\"";
	for( unsigned char c : s ){
		if( c == '"' ) o += "\\\"";
		else if( c == '\\' ) o += "\\\\";
		else if( c == '\n' ) o += "\\n";
		else if( c == '\r' ) o += "\\r";
		else if( c == '\t' ) o += "\\t";
		else if( c < 0x20 ) { char b[8]; snprintf( b, sizeof(b), "\\u%04x", c ); o += b; }
		else o += (char)c;
	}
	return o + "\"";
}
struct J {
	std::string s; bool first = true;
	void k( const char* key ){ if( !first ) s += ","; first = false; s += q( key ); s += ":"; }
	void i( const char* key, long long v ){ k( key ); s += std::to_string( v ); }
	void u( const char* key, unsigned long long v ){ k( key ); s += std::to_string( v ); }
	void b( const char* key, bool v ){ k( key ); s += v ? "true" : "false"; }
	void t( const char* key, const std::string& v ){ k( key ); s += q( v ); }
	void raw( const char* key, const std::string& v ){ k( key ); s += v; }
};
template<typename T> static std::string arr( const T* a, size_t n ){
	std::string o = "[";
	for( size_t x = 0; x < n; x++ ){ if( x ) o += ","; o += std::to_string( (long long)a[x] ); }
	return o + "]";
}
template<typename V> static std::string vec( const V& v ){
	std::string o = "[";
	bool f = true;
	for( auto x : v ){ if( !f ) o += ","; f = false; o += std::to_string( (long long)x ); }
	return o + "]";
}
template<size_t N> static std::string bits( const std::bitset<N>& b ){
	std::string o = "[";
	bool f = true;
	for( size_t x = 0; x < N; x++ ) if( b[x] ){ if( !f ) o += ","; f = false; o += std::to_string( x ); }
	return o + "]";
}
static std::string drops( const std::vector<std::shared_ptr<s_mob_drop>>& d ){
	std::string o = "[";
	bool f = true;
	for( auto& x : d ){
		if( !f ) o += ","; f = false;
		J j; j.u( "nameid", x->nameid ); j.u( "rate", x->rate ); j.b( "steal", x->steal_protected ); j.u( "rndopt", x->randomopt_group );
		o += "{" + j.s + "}";
	}
	return o + "]";
}
static void emit( FILE* f, const std::string& body ){ fprintf( f, "{%s}\n", body.c_str() ); }
static FILE* open( const std::string& dir, const char* name ){
	std::string p = dir + "/" + name;
	FILE* f = fopen( p.c_str(), "wb" );
	if( !f ) ShowError( "dbdump: cannot open %s\n", p.c_str() );
	return f;
}
}

void dbdump_main(){
	const char* d = getenv( "RA_DBDUMP_DIR" );
	if( d == nullptr || !*d ) return;
	std::string dir = d;
	using namespace dbdump;
	FILE* f;
	if( ( f = open( dir, "mode.txt" ) ) ){
#ifdef RENEWAL
		fprintf( f, "renewal\n" );
#else
		fprintf( f, "prerenewal\n" );
#endif
		fclose( f );
	}
	if( ( f = open( dir, "constants.tsv" ) ) ){
		for( int32 i = LABEL_START; i < str_num; i++ )
			if( str_data[i].type == C_INT ) fprintf( f, "%s\t%lld\n", get_str( i ), (long long)str_data[i].val );
		fclose( f );
	}
	if( ( f = open( dir, "limits.tsv" ) ) ){ // compile-time limits and battle.conf values the loaders use (not script constants)
#define LIM(x) fprintf( f, "%s\t%lld\n", #x, (long long)( x ) )
		LIM( SC_MAX ); LIM( EFST_MAX ); LIM( EFST_BLANK ); LIM( SCS_MAX ); LIM( SCB_MAX ); LIM( SCF_MAX ); LIM( OPT1_MAX ); LIM( OPT2_MAX ); LIM( OPT3_MAX ); LIM( OPTION_MAX );
		LIM( IT_MAX ); LIM( MAX_WEAPON_TYPE ); LIM( MAX_AMMO_TYPE ); LIM( MAX_CARD_TYPE ); LIM( DEFTYPE_MIN ); LIM( DEFTYPE_MAX );
		LIM( MAX_LEVEL ); LIM( MAX_REFINE ); LIM( MAX_WEAPON_LEVEL ); LIM( MAX_ARMOR_LEVEL ); LIM( MAX_ZENY ); LIM( MAX_SLOTS );
		LIM( RC2_MAX ); LIM( RC_MAX ); LIM( ELE_MAX ); LIM( MAX_ELE_LEVEL ); LIM( MD_MASK ); LIM( MAX_SKILL_LEVEL ); LIM( MAX_SKILL_ITEM_REQUIRE );
		LIM( MIN_WALK_SPEED ); LIM( MAX_WALK_SPEED ); LIM( DEFAULT_WALK_SPEED ); LIM( MAX_ASPD_NOPC ); LIM( MIN_ASPD ); LIM( AMOTION_DIVIDER_NOPC );
		LIM( AMOTION_ZERO_ASPD ); LIM( AMOTION_INTERVAL ); LIM( MAX_MOB_DROP ); LIM( MAX_MVP_DROP ); LIM( NAME_LENGTH ); LIM( ITEM_NAME_LENGTH );
		LIM( SKILL_NAME_LENGTH ); LIM( SKILL_DESC_LENGTH ); LIM( MAX_EXP ); LIM( NK_MAX ); LIM( INF2_MAX ); LIM( UF_MAX ); LIM( PARAM_MAX );
		LIM( JOBL_2_1 ); LIM( JOBL_2_2 ); LIM( MAPID_FIRSTMASK ); LIM( ITEMJ_ALL ); LIM( EQP_SHADOW_GEAR ); LIM( DROPEFFECT_MAX ); LIM( SC_NONE );
		LIM( ITEMID_DUMMY ); LIM( UNKNOWN_ITEM_ID ); LIM( WEDDING_RING_M ); LIM( WEDDING_RING_F ); LIM( DELAYCONSUME_TEMP ); LIM( DELAYCONSUME_NOCONSUME );
		LIM( RC_NONE_ ); LIM( ELE_NONE ); LIM( SCF_NONE ); LIM( OPT1_NONE ); LIM( OPT2_NONE ); LIM( DMG_NORMAL ); LIM( SKILL_COPY_PLAGIARISM ); LIM( SKILL_COPY_REPRODUCE );
		LIM( SZ_SMALL ); LIM( BL_CHAR ); LIM( BL_SKILL ); LIM( BL_PC ); LIM( BL_MOB ); LIM( EQP_SHIELD ); LIM( BF_NONE ); LIM( BF_MISC ); LIM( CLASS_EVENT ); LIM( CLASS_NORMAL );
		LIM( battle_config.base_exp_rate ); LIM( battle_config.job_exp_rate ); LIM( battle_config.mvp_exp_rate );
		LIM( battle_config.monster_damage_delay_rate ); LIM( battle_config.view_range_rate ); LIM( battle_config.chase_range_rate );
		LIM( battle_config.monster_active_enable ); LIM( battle_config.mvp_hp_rate ); LIM( battle_config.monster_hp_rate );
		LIM( battle_config.display_hallucination ); LIM( battle_config.defnotenemy ); LIM( battle_config.area_size ); LIM( battle_config.use_statpoint_table ); LIM( battle_config.use_traitpoint_table ); LIM( inter_config.start_status_points ); LIM( SCB_DYE ); LIM( SCB_BASE );
		fclose( f );
	}
	if( const char* probe = getenv( "RA_DBDUMP_PROBE" ) ){ // debugging aid: resolve the given comma-separated constant names with script_get_constant
		if( ( f = open( dir, "probe.tsv" ) ) ){
			std::string list = probe, tok;
			for( size_t x = 0; x <= list.size(); x++ ){
				if( x == list.size() || list[x] == ',' ){
					int64 v = 0;
					bool ok = !tok.empty() && script_get_constant( tok.c_str(), &v );
					fprintf( f, "%s\t%s\t%lld\n", tok.c_str(), ok ? "found" : "missing", (long long)v );
					tok.clear();
				}else tok += list[x];
			}
			fclose( f );
		}
	}
	// ---- item_db
	if( ( f = open( dir, "item_db.jsonl" ) ) ){
		std::vector<t_itemid> ids;
		for( auto& e : item_db ) ids.push_back( e.first );
		std::sort( ids.begin(), ids.end() );
		for( t_itemid id : ids ){
			auto it = item_db.find( id );
			J j; j.u( "id", it->nameid ); j.t( "aegis", it->name ); j.t( "name", it->ename );
			j.i( "type", it->type ); j.i( "subtype", it->subtype ); j.u( "buy", it->value_buy ); j.u( "sell", it->value_sell );
			j.u( "weight", it->weight ); j.u( "atk", it->atk );
#ifdef RENEWAL
			j.u( "matk", it->matk );
#endif
			j.u( "def", it->def ); j.u( "range", it->range ); j.u( "slots", it->slots );
			j.raw( "class_base", "[" + std::to_string( it->class_base[0] ) + "," + std::to_string( it->class_base[1] ) + "," + std::to_string( it->class_base[2] ) + "]" );
			j.u( "class_upper", it->class_upper ); j.i( "sex", it->sex ); j.u( "equip", it->equip );
			j.u( "wlv", it->weapon_level ); j.u( "alv", it->armor_level ); j.u( "elv", it->elv ); j.u( "elvmax", it->elvmax );
			j.b( "no_refine", it->flag.no_refine ); j.b( "gradable", it->flag.gradable ); j.u( "look", it->look ); j.u( "view_id", it->view_id );
			j.b( "buyingstore", it->flag.buyingstore ); j.b( "dead_branch", it->flag.dead_branch ); j.b( "group", it->flag.group );
			j.b( "guid", it->flag.guid ); j.b( "bindOnEquip", it->flag.bindOnEquip ); j.b( "broadcast", it->flag.broadcast );
			j.u( "delay_consume", it->flag.delay_consume ); j.i( "dropEffect", it->flag.dropEffect );
			j.u( "delay_duration", it->delay.duration ); j.i( "delay_sc", it->delay.sc );
			j.u( "stack_amount", it->stack.amount ); j.b( "stack_inventory", it->stack.inventory ); j.b( "stack_cart", it->stack.cart );
			j.b( "stack_storage", it->stack.storage ); j.b( "stack_guild_storage", it->stack.guild_storage );
			j.u( "nouse_override", it->item_usage.override ); j.b( "nouse_sitting", it->item_usage.sitting );
			j.i( "trade_override", it->gm_lv_trade_override );
			j.b( "tr_drop", it->flag.trade_restriction.drop ); j.b( "tr_trade", it->flag.trade_restriction.trade );
			j.b( "tr_trade_partner", it->flag.trade_restriction.trade_partner ); j.b( "tr_sell", it->flag.trade_restriction.sell );
			j.b( "tr_cart", it->flag.trade_restriction.cart ); j.b( "tr_storage", it->flag.trade_restriction.storage );
			j.b( "tr_guild_storage", it->flag.trade_restriction.guild_storage ); j.b( "tr_mail", it->flag.trade_restriction.mail );
			j.b( "tr_auction", it->flag.trade_restriction.auction );
			j.b( "has_script", it->script != nullptr ); j.b( "has_equip_script", it->equip_script != nullptr ); j.b( "has_unequip_script", it->unequip_script != nullptr );
			emit( f, j.s );
		}
		fclose( f );
	}
	// ---- mob_db
	if( ( f = open( dir, "mob_db.jsonl" ) ) ){
		std::vector<uint32> ids;
		for( auto& e : mob_db ) ids.push_back( e.first );
		std::sort( ids.begin(), ids.end() );
		for( uint32 id : ids ){
			auto m = mob_db.find( id );
			const status_data& s = m->status;
			J j; j.u( "id", m->id ); j.t( "sprite", m->sprite ); j.t( "name", m->name ); j.t( "jname", m->jname );
			j.u( "lv", m->lv ); j.u( "max_hp", s.max_hp ); j.u( "max_sp", s.max_sp ); j.u( "base_exp", m->base_exp ); j.u( "job_exp", m->job_exp ); j.u( "mexp", m->mexp );
			j.u( "atk", s.rhw.atk ); j.u( "atk2", s.rhw.atk2 );
#ifdef RENEWAL
			j.u( "matk", s.rhw.matk );
#endif
			j.i( "def", s.def ); j.i( "mdef", s.mdef ); j.i( "res", s.res ); j.i( "mres", s.mres );
			j.i( "str", s.str ); j.i( "agi", s.agi ); j.i( "vit", s.vit ); j.i( "int", s.int_ ); j.i( "dex", s.dex ); j.i( "luk", s.luk );
			j.u( "range", s.rhw.range ); j.u( "range2", m->range2 ); j.u( "range3", m->range3 );
			j.u( "size", s.size ); j.u( "race", s.race ); j.raw( "race2", vec( m->race2 ) );
			j.u( "def_ele", s.def_ele ); j.u( "ele_lv", s.ele_lv ); j.u( "speed", s.speed );
			j.u( "adelay", s.adelay ); j.u( "amotion", s.amotion ); j.u( "clientamotion", s.clientamotion ); j.u( "dmotion", s.dmotion );
			j.u( "damagetaken", m->damagetaken ); j.i( "group_id", m->group_id ); j.t( "title", m->title );
			j.i( "mode", s.mode ); j.u( "class", s.class_ );
			j.raw( "mvpdrops", drops( m->mvpitem ) ); j.raw( "drops", drops( m->dropitem ) );
			emit( f, j.s );
		}
		fclose( f );
	}
	// ---- skill_db
	if( ( f = open( dir, "skill_db.jsonl" ) ) ){
		std::vector<uint16> ids;
		for( auto& e : skill_db ) ids.push_back( e.first );
		std::sort( ids.begin(), ids.end() );
		for( uint16 id : ids ){
			auto s = skill_db.find( id );
			const s_skill_require& r = s->require;
			J j; j.u( "id", s->nameid ); j.t( "name", s->name ); j.t( "desc", s->desc ); j.u( "max", s->max );
			j.i( "skill_type", s->skill_type ); j.u( "inf", s->inf ); j.raw( "nk", bits( s->nk ) ); j.raw( "inf2", bits( s->inf2 ) );
			j.raw( "range", arr( s->range, MAX_SKILL_LEVEL ) ); j.i( "hit", s->hit ); j.raw( "num", arr( s->num, MAX_SKILL_LEVEL ) );
			j.raw( "element", arr( s->element, MAX_SKILL_LEVEL ) ); j.raw( "splash", arr( s->splash, MAX_SKILL_LEVEL ) );
			j.raw( "maxcount", arr( s->maxcount, MAX_SKILL_LEVEL ) ); j.raw( "blewcount", arr( s->blewcount, MAX_SKILL_LEVEL ) );
			j.u( "copy_option", s->copyable.option ); j.u( "copy_req_opt", s->copyable.req_opt );
			j.u( "nonear_range", s->unit_nonearnpc_range ); j.u( "nonear_type", s->unit_nonearnpc_type );
			j.b( "castcancel", s->castcancel ); j.u( "cast_def_rate", s->cast_def_rate );
			j.raw( "cast", arr( s->cast, MAX_SKILL_LEVEL ) ); j.raw( "delay", arr( s->delay, MAX_SKILL_LEVEL ) );
			j.raw( "walkdelay", arr( s->walkdelay, MAX_SKILL_LEVEL ) ); j.raw( "upkeep_time", arr( s->upkeep_time, MAX_SKILL_LEVEL ) );
			j.raw( "upkeep_time2", arr( s->upkeep_time2, MAX_SKILL_LEVEL ) ); j.raw( "cooldown", arr( s->cooldown, MAX_SKILL_LEVEL ) );
#ifdef RENEWAL_CAST
			j.raw( "fixed_cast", arr( s->fixed_cast, MAX_SKILL_LEVEL ) );
#endif
			j.u( "castnodex", s->castnodex ); j.u( "delaynodex", s->delaynodex );
			j.raw( "req_hp", arr( r.hp, MAX_SKILL_LEVEL ) ); j.raw( "req_mhp", arr( r.mhp, MAX_SKILL_LEVEL ) ); j.raw( "req_sp", arr( r.sp, MAX_SKILL_LEVEL ) );
			j.raw( "req_ap", arr( r.ap, MAX_SKILL_LEVEL ) ); j.raw( "req_hp_rate", arr( r.hp_rate, MAX_SKILL_LEVEL ) );
			j.raw( "req_sp_rate", arr( r.sp_rate, MAX_SKILL_LEVEL ) ); j.raw( "req_ap_rate", arr( r.ap_rate, MAX_SKILL_LEVEL ) );
			j.raw( "req_zeny", arr( r.zeny, MAX_SKILL_LEVEL ) ); j.i( "req_weapon", r.weapon ); j.i( "req_ammo", r.ammo );
			j.raw( "req_ammo_qty", arr( r.ammo_qty, MAX_SKILL_LEVEL ) ); j.i( "req_state", r.state );
			j.raw( "req_spiritball", arr( r.spiritball, MAX_SKILL_LEVEL ) );
			j.raw( "req_itemid", arr( r.itemid, MAX_SKILL_ITEM_REQUIRE ) ); j.raw( "req_amount", arr( r.amount, MAX_SKILL_ITEM_REQUIRE ) );
			j.raw( "req_eqitem", vec( r.eqItem ) ); j.raw( "req_status", vec( r.status ) ); j.b( "req_item_level_dep", r.itemid_level_dependent );
			j.raw( "giveap", arr( s->giveap, MAX_SKILL_LEVEL ) );
			j.u( "unit_id", s->unit_id ); j.u( "unit_id2", s->unit_id2 ); j.raw( "unit_layout", arr( s->unit_layout_type, MAX_SKILL_LEVEL ) );
			j.raw( "unit_range", arr( s->unit_range, MAX_SKILL_LEVEL ) ); j.i( "unit_interval", s->unit_interval );
			j.i( "unit_target", s->unit_target ); j.raw( "unit_flag", bits( s->unit_flag ) );
			j.i( "sc", s->sc );
			emit( f, j.s );
		}
		fclose( f );
	}
	// ---- status
	if( ( f = open( dir, "status.jsonl" ) ) ){
		std::vector<uint16> ids;
		for( auto& e : status_db ) ids.push_back( e.first );
		std::sort( ids.begin(), ids.end() );
		for( uint16 id : ids ){
			auto s = status_db.find( id );
			J j; j.i( "type", s->type ); j.i( "icon", s->icon ); j.raw( "state", bits( s->state ) ); j.raw( "calc_flag", bits( s->calc_flag ) );
			j.u( "opt1", s->opt1 ); j.u( "opt2", s->opt2 ); j.u( "opt3", s->opt3 ); j.u( "look", s->look ); j.raw( "flag", bits( s->flag ) );
			j.u( "skill_id", s->skill_id ); j.raw( "endonstart", vec( s->endonstart ) ); j.raw( "fail", vec( s->fail ) );
			j.raw( "endreturn", vec( s->endreturn ) ); j.raw( "endonend", vec( s->endonend ) );
			j.i( "min_duration", s->min_duration ); j.u( "min_rate", s->min_rate ); j.b( "has_script", s->script != nullptr );
			emit( f, j.s );
		}
		fclose( f );
	}
	// ---- size_fix
	if( ( f = open( dir, "size_fix.jsonl" ) ) ){
		std::vector<int32> ids;
		for( auto& e : size_fix_db ) ids.push_back( e.first );
		std::sort( ids.begin(), ids.end() );
		for( int32 id : ids ){
			auto s = size_fix_db.find( id );
			J j; j.i( "weapon", id ); j.u( "small", s->small ); j.u( "medium", s->medium ); j.u( "large", s->large );
			emit( f, j.s );
		}
		fclose( f );
	}
	// ---- attr_fix (full table; non-default cells are what matters)
	if( ( f = open( dir, "attr_fix.jsonl" ) ) ){
		for( int32 lv = 1; lv <= MAX_ELE_LEVEL; lv++ ){
			J j; j.i( "level", lv );
			std::string rows = "[";
			for( int32 a = 0; a < ELE_MAX; a++ ){
				if( a ) rows += ",";
				rows += "[";
				for( int32 df = 0; df < ELE_MAX; df++ ){
					if( df ) rows += ",";
					rows += std::to_string( elemental_attribute_db.getAttribute( lv, a, df ) );
				}
				rows += "]";
			}
			j.raw( "table", rows + "]" );
			emit( f, j.s );
		}
		fclose( f );
	}
	// ---- refine
	if( ( f = open( dir, "refine.jsonl" ) ) ){
		std::vector<uint16> gids;
		for( auto& e : refine_db ) gids.push_back( e.first );
		std::sort( gids.begin(), gids.end() );
		for( uint16 g : gids ){
			auto info = refine_db.find( g );
			std::map<uint16, std::shared_ptr<s_refine_levels_info>> lv( info->levels.begin(), info->levels.end() );
			for( auto& l : lv ){
				std::map<uint16, std::shared_ptr<s_refine_level_info>> rl( l.second->levels.begin(), l.second->levels.end() );
				for( auto& r : rl ){
					J j; j.u( "group", g ); j.u( "level", l.first ); j.u( "refine_level", r.first );
					j.u( "bonus", r.second->bonus ); j.u( "randombonus_max", r.second->randombonus_max ); j.u( "blessing_amount", r.second->blessing_amount );
					j.b( "broadcast_success", r.second->broadcast_success ); j.b( "broadcast_failure", r.second->broadcast_failure );
					std::map<uint16, std::shared_ptr<s_refine_cost>> cs( r.second->costs.begin(), r.second->costs.end() );
					std::string o = "[";
					bool first = true;
					for( auto& c : cs ){
						if( !first ) o += ","; first = false;
						J k; k.u( "index", c.first ); k.u( "nameid", c.second->nameid ); k.u( "chance", c.second->chance ); k.u( "zeny", c.second->zeny );
						k.u( "breaking_rate", c.second->breaking_rate ); k.u( "downgrade_amount", c.second->downgrade_amount );
						o += "{" + k.s + "}";
					}
					j.raw( "costs", o + "]" );
					emit( f, j.s );
				}
			}
		}
		fclose( f );
	}
	// ---- statpoint (raw table before the loader's extrapolation is not reachable: dump what it holds)
	if( ( f = open( dir, "statpoint.jsonl" ) ) ){
		std::vector<uint16> ids;
		for( auto& e : statpoint_db ) ids.push_back( e.first );
		std::sort( ids.begin(), ids.end() );
		for( uint16 id : ids ){
			auto s = statpoint_db.find( id );
			J j; j.u( "level", s->level ); j.u( "statpoints", s->statpoints ); j.u( "traitpoints", s->traitpoints );
			emit( f, j.s );
		}
		fclose( f );
	}
	// ---- job_stats
	if( ( f = open( dir, "job_stats.jsonl" ) ) ){
		std::vector<uint16> ids;
		for( auto& e : job_db ) ids.push_back( e.first );
		std::sort( ids.begin(), ids.end() );
		for( uint16 id : ids ){
			auto s = job_db.find( id );
			J j; j.u( "job", s->job_id ); j.u( "hp_factor", s->hp_factor ); j.u( "hp_increase", s->hp_increase );
			j.u( "sp_factor", s->sp_factor ); j.u( "sp_increase", s->sp_increase ); j.u( "ap_factor", s->ap_factor ); j.u( "ap_increase", s->ap_increase );
			j.u( "max_weight_base", s->max_weight_base ); j.raw( "aspd_base", vec( s->aspd_base ) );
			j.u( "max_base_level", s->max_base_level ); j.u( "max_job_level", s->max_job_level );
			j.raw( "base_exp", arr( s->base_exp, MAX_LEVEL ) ); j.raw( "job_exp", arr( s->job_exp, MAX_LEVEL ) );
			std::string jb = "[";
			for( size_t x = 0; x < s->job_bonus.size(); x++ ){ if( x ) jb += ","; jb += arr( s->job_bonus[x].data(), PARAM_MAX ); }
			j.raw( "job_bonus", jb + "]" ); j.raw( "alternate_outfits", vec( s->alternate_outfits ) );
			j.raw( "base_hp", vec( s->base_hp ) ); j.raw( "base_sp", vec( s->base_sp ) ); j.raw( "base_ap", vec( s->base_ap ) );
			j.raw( "max_param", arr( s->max_param, PARAM_MAX ) );
			emit( f, j.s );
		}
		fclose( f );
	}
	ShowStatus( "dbdump: wrote dumps to %s\n", dir.c_str() );
}
