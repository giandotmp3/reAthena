// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "random.hpp"

#include <cerrno>
#include <cstdlib>

std::uniform_int_distribution<int32> int31_distribution = std::uniform_int_distribution<int32>(0, SINT32_MAX);

/// Generates a random number in the interval [0, SINT32_MAX]
int32 rnd( void ){
	return int31_distribution( generator );
}

/// Reseeds the global generator (and resets the distribution state) for reproducible runs
void rnd_seed( uint32 seed ){
	generator.seed( seed );
	int31_distribution.reset();
}

/// Reseeds from RATHENA_RNG_SEED when present; ignores unset, empty or malformed values
bool rnd_seed_from_env( void ){
	const char* value = std::getenv( "RATHENA_RNG_SEED" );

	if( value == nullptr || *value == '\0' ){
		return false;
	}

	char* end = nullptr;
	errno = 0;
	unsigned long seed = std::strtoul( value, &end, 10 );

	if( errno != 0 || end == value || *end != '\0' || seed > UINT32_MAX ){
		return false;
	}

	rnd_seed( static_cast<uint32>( seed ) );
	return true;
}
