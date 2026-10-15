// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "script_host.hpp"

#include <cstring>

#include <common/db.hpp>
#include <common/malloc.hpp>
#include <common/showmsg.hpp>

#include "mapreg.hpp"
#include "npc.hpp"
#include "pc.hpp"
#include "script.hpp"

namespace {

// call_function() stubs whose call paused (a sleep, a dialog); freed once no script state uses them.
std::vector<script_code*> pending_calls;

/// Whether a live script state runs `code` or will return into it.
bool code_in_use( const script_code* code ){
	if( st_db == nullptr ){
		return false;
	}

	bool used = false;
	DBIterator* iter = db_iterator( st_db );

	for( script_state* st = static_cast<script_state*>( dbi_first( iter ) ); dbi_exists( iter ) && !used; st = static_cast<script_state*>( dbi_next( iter ) ) ){
		if( st->script == code ){
			used = true;
		}else if( st->stack != nullptr ){
			for( int32 i = 0; i < st->stack->sp && !used; i++ ){
				const script_data& data = st->stack->stack_data[i];

				used = data.type == C_RETINFO && data.u.ri != nullptr && data.u.ri->script == code;
			}
		}
	}

	dbi_destroy( iter );
	return used;
}

/// Appends `value` as a script string literal.
void append_literal( std::string& out, const std::string& value ){
	out += '"';
	for( char c : value ){
		switch( c ){
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += c; break;
		}
	}
	out += '"';
}

/// Variables ScriptHost can reach without a running script.
bool variable_allowed( const map_session_data* sd, const char* name, const char* who ){
	if( name == nullptr || name[0] == '\0' ){
		ShowError( "%s: empty variable name.\n", who );
		return false;
	}

	if( name[0] == '.' || name[0] == '\'' ){
		ShowError( "%s: NPC and instance variables need a running script ('%s').\n", who, name );
		return false;
	}

	if( name[0] != '$' && sd == nullptr ){
		ShowError( "%s: variable '%s' needs a player.\n", who, name );
		return false;
	}

	return true;
}

/// Reads `name[index]` through the VM's own get_val.
script_data read_variable( map_session_data* sd, const char* name, uint32 index ){
	script_data data = {};

	data.type = C_NAME;
	data.u.num = reference_uid( add_str( name ), index );
	data.ref = nullptr;

	get_val_( nullptr, &data, sd );

	return data;
}

} // namespace

// ---- NPC events and labels ----------------------------------------------------------------------

int32 ScriptHost::run_player_npc_event( map_session_data& sd, const char* name, int32 ontouch ){
	return npc_event( &sd, name, ontouch );
}

size_t ScriptHost::run_player_event( map_session_data& sd, npce_event type ){
	return npc_script_event( sd, type );
}

int32 ScriptHost::run_event( const char* name ){
	return npc_event_do( name );
}

int32 ScriptHost::run_event_as( const char* name, int32 rid ){
	return npc_event_do_id( name, rid );
}

int32 ScriptHost::run_label_everywhere( const char* label ){
	return npc_event_doall( label );
}

int32 ScriptHost::run_label_everywhere_as( const char* label, int32 rid ){
	return npc_event_doall_id( label, rid );
}

void ScriptHost::run_label_everywhere_logged( const char* label ){
	npc_event_runall( label );
}

int32 ScriptHost::run_label_in_file( const char* label, const char* path ){
	return npc_event_doall_path( label, path );
}

void ScriptHost::run_oninit(){
	npc_event_do_oninit();
}

// ---- NPC dialogs --------------------------------------------------------------------------------

int32 ScriptHost::talk_to_npc( map_session_data& sd, npc_data& nd ){
	return npc_click( &sd, &nd );
}

bool ScriptHost::continue_dialog( map_session_data& sd, int32 npc_id, bool closing ){
	return npc_scriptcont( &sd, npc_id, closing );
}

int32 ScriptHost::run_queued_event( map_session_data& sd, bool free_script_stack ){
	return npc_event_dequeue( &sd, free_script_stack );
}

// ---- Scripts ------------------------------------------------------------------------------------

void ScriptHost::run( script_code* code, int32 pos, int32 rid, int32 oid ){
	run_script( code, pos, rid, oid );
}

void ScriptHost::run_item_script( map_session_data& sd, script_code* code ){
	run_script( code, 0, sd.id, 0 );
}

void ScriptHost::run_as_server_npc( map_session_data& sd, script_code* code ){
	run_script( code, 0, sd.id, fake_nd->id );
}

void ScriptHost::run_autobonus( map_session_data& sd, const char* script, uint32 pos ){
	script_run_autobonus( script, &sd, pos );
}

void ScriptHost::run_pet_autobonus( map_session_data& sd, const std::string& script ){
	script_run_petautobonus( script, sd );
}

bool ScriptHost::function_exists( const char* name ){
	DBMap* functions = script_get_userfunc_db();

	return name != nullptr && functions != nullptr && strdb_get( functions, name ) != nullptr;
}

bool ScriptHost::call_function( const char* name, const std::vector<Arg>& args, int32 rid, int32 oid ){
	collect_finished_calls();

	if( !function_exists( name ) ){
		ShowError( "ScriptHost::call_function: function '%s' not found.\n", name ? name : "(null)" );
		return false;
	}

	std::string source = "callfunc ";

	append_literal( source, name );

	for( const Arg& arg : args ){
		source += ", ";

		if( arg.is_str ){
			append_literal( source, arg.str );
		}else{
			source += "(" + std::to_string( arg.num ) + ")";
		}
	}

	source += "; end;";

	script_code* code = parse_script( source.c_str(), "ScriptHost::call_function", 0, SCRIPT_IGNORE_EXTERNAL_BRACKETS );

	if( code == nullptr ){
		return false;
	}

	run_script( code, 0, rid, oid );

	if( code_in_use( code ) ){
		pending_calls.push_back( code );
	}else{
		script_free_code( code );
	}

	return true;
}

void ScriptHost::collect_finished_calls( bool all ){
	for( auto it = pending_calls.begin(); it != pending_calls.end(); ){
		if( all || !code_in_use( *it ) ){
			script_free_code( *it );
			it = pending_calls.erase( it );
		}else{
			++it;
		}
	}
}

// ---- Variables ----------------------------------------------------------------------------------

bool ScriptHost::set_var_num( map_session_data* sd, const char* name, int64 value, uint32 index ){
	if( !variable_allowed( sd, name, "ScriptHost::set_var_num" ) ){
		return false;
	}

	return set_reg_num( nullptr, sd, reference_uid( add_str( name ), index ), name, value, nullptr );
}

bool ScriptHost::set_var_str( map_session_data* sd, const char* name, const char* value, uint32 index ){
	if( !variable_allowed( sd, name, "ScriptHost::set_var_str" ) ){
		return false;
	}

	return set_reg_str( nullptr, sd, reference_uid( add_str( name ), index ), name, value != nullptr ? value : "", nullptr );
}

int64 ScriptHost::get_var_num( map_session_data* sd, const char* name, uint32 index ){
	if( !variable_allowed( sd, name, "ScriptHost::get_var_num" ) ){
		return 0;
	}

	if( is_string_variable( name ) ){
		ShowError( "ScriptHost::get_var_num: '%s' is a string variable.\n", name );
		return 0;
	}

	script_data data = read_variable( sd, name, index );

	return data.type == C_INT ? data.u.num : 0;
}

std::string ScriptHost::get_var_str( map_session_data* sd, const char* name, uint32 index ){
	if( !variable_allowed( sd, name, "ScriptHost::get_var_str" ) ){
		return "";
	}

	if( !is_string_variable( name ) ){
		ShowError( "ScriptHost::get_var_str: '%s' is not a string variable.\n", name );
		return "";
	}

	script_data data = read_variable( sd, name, index );
	std::string value;

	if( data.type == C_STR ){
		value = data.u.str;
		aFree( data.u.str );
	}else if( data.type == C_CONSTSTR ){
		value = data.u.str;
	}

	return value;
}

bool ScriptHost::set_server_var( const char* name, int64 value, uint32 index ){
	return mapreg_setreg( reference_uid( add_str( name ), index ), value );
}

int64 ScriptHost::get_server_var( const char* name, uint32 index ){
	return mapreg_readreg( reference_uid( add_str( name ), index ) );
}

// ---- Lifecycle ----------------------------------------------------------------------------------

void ScriptHost::reload(){
	script_reload();
	collect_finished_calls(); // script_reload ended every script state
	npc_reload();
}

bool ScriptHost::load_file( const char* path ){
	if( !npc_addsrcfile( path, true ) ){
		return false;
	}

	npc_read_event_script();
	return true;
}

bool ScriptHost::unload_file( const char* path ){
	return npc_unloadfile( path );
}

void ScriptHost::unload_npc( npc_data& nd ){
	npc_unload_duplicates( &nd );
	npc_unload( &nd, true );
	npc_read_event_script();
}
