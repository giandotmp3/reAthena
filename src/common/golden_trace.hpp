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
#include <utility>

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

#else

#define GOLDEN_TRACE( kind, ... ) ( (void)0 )

#endif

#endif /* GOLDEN_TRACE_HPP */
