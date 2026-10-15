// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// Golden-vector capture hooks for the reimagined rules (modernized/rathena-reimagined).
//
// Compiled out unless the build defines RATHENA_GOLDEN_TRACE, so normal servers carry no code
// for it. A capture build appends one JSON object per call to the file named by the
// RATHENA_GOLDEN_TRACE_FILE environment variable, e.g.
//   {"kind":"def_pre_re_pc","def2":30,"rnd":0,"vit_def":24}
// tools/golden/capture.sh builds such a server, drives it and collects the vectors.
#ifndef GOLDEN_TRACE_HPP
#define GOLDEN_TRACE_HPP

#ifdef RATHENA_GOLDEN_TRACE

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

inline void golden_trace( const char* kind, std::initializer_list<std::pair<const char*, int64_t>> fields ){
	static FILE* out = [](){
		const char* path = std::getenv( "RATHENA_GOLDEN_TRACE_FILE" );
		return ( path != nullptr && *path != '\0' ) ? std::fopen( path, "a" ) : nullptr;
	}();

	if( out == nullptr ){
		return;
	}

	std::fprintf( out, "{\"kind\":\"%s\"", kind );
	for( const auto& field : fields ){
		std::fprintf( out, ",\"%s\":%" PRId64, field.first, field.second );
	}
	std::fputs( "}\n", out );
	std::fflush( out );
}

#define GOLDEN_TRACE( kind, ... ) golden_trace( kind, { __VA_ARGS__ } )

/// The same record with field names built at run time (a variable number of fields).
inline void golden_trace_fields( const char* kind, const std::vector<std::pair<std::string, int64_t>>& fields ){
	static FILE* out = [](){
		const char* path = std::getenv( "RATHENA_GOLDEN_TRACE_FILE" );
		return ( path != nullptr && *path != '\0' ) ? std::fopen( path, "a" ) : nullptr;
	}();

	if( out == nullptr ){
		return;
	}

	std::fprintf( out, "{\"kind\":\"%s\"", kind );
	for( const auto& field : fields ){
		std::fprintf( out, ",\"%s\":%" PRId64, field.first.c_str(), field.second );
	}
	std::fputs( "}\n", out );
	std::fflush( out );
}

/// Records `text` (a SQL statement) as {"kind":kind,"seq":n,"text":"..."}, numbered in call order.
inline void golden_trace_text( const char* kind, const char* text ){
	static FILE* out = [](){
		const char* path = std::getenv( "RATHENA_GOLDEN_TRACE_FILE" );
		return ( path != nullptr && *path != '\0' ) ? std::fopen( path, "a" ) : nullptr;
	}();
	static int64_t seq = 0;

	if( out == nullptr ){
		return;
	}

	std::fprintf( out, "{\"kind\":\"%s\",\"seq\":%" PRId64 ",\"text\":\"", kind, seq++ );
	for( const char* c = text; *c != '\0'; c++ ){
		if( *c == '"' || *c == '\\' ){
			std::fputc( '\\', out );
			std::fputc( *c, out );
		}else if( static_cast<unsigned char>( *c ) < 0x20 ){
			std::fputc( ' ', out );
		}else{
			std::fputc( *c, out );
		}
	}
	std::fputs( "\"}\n", out );
	std::fflush( out );
}

/// While non-zero, the SQL layer records every statement it executes (GOLDEN_SQL_SCOPE).
inline int32_t golden_sql_scope = 0;

/// Records the SQL statements executed until the end of the enclosing block under `kind`.
struct golden_sql_scope_guard {
	explicit golden_sql_scope_guard( const char* kind ){ golden_sql_kind() = kind; golden_sql_scope++; }
	~golden_sql_scope_guard(){ golden_sql_scope--; }
	static const char*& golden_sql_kind(){ static const char* kind = ""; return kind; }
};

#define GOLDEN_SQL_SCOPE( kind ) golden_sql_scope_guard golden_sql_scope_guard_instance( kind )
#define GOLDEN_TRACE_SQL( text ) \
	do{ if( golden_sql_scope > 0 ) golden_trace_text( golden_sql_scope_guard::golden_sql_kind(), text ); }while( 0 )

#else

#define GOLDEN_TRACE( kind, ... ) ( (void)0 )
#define GOLDEN_SQL_SCOPE( kind ) ( (void)0 )
#define GOLDEN_TRACE_SQL( text ) ( (void)0 )

#endif

#endif /* GOLDEN_TRACE_HPP */
