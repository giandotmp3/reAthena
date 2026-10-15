// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "tickstats.hpp"

#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "showmsg.hpp"

bool tickstats_enabled = false;

namespace {

using steady = std::chrono::steady_clock;

// Log-linear buckets over microseconds: values below 64 get one bucket each, above that every
// power of two is split into 32 buckets (relative width <= 1/32). Values are clamped at 2^41 us.
constexpr uint64 SUB_BITS = 5;
constexpr uint64 SUB = 1ull << SUB_BITS;
constexpr uint64 MAX_VALUE = ( 1ull << 41 ) - 1;
constexpr size_t BUCKETS = ( 41 - SUB_BITS ) * SUB + 2 * SUB;

struct Histogram{
	std::array<uint64, BUCKETS> counts{};
	uint64 total = 0;
	uint64 sum_us = 0;
	uint64 max_us = 0;

	static size_t index( uint64 v ){
		if( v < 2 * SUB ){
			return static_cast<size_t>( v );
		}
		uint64 msb = 63;
		while( !( v >> msb ) ){
			msb--;
		}
		uint64 shift = msb - SUB_BITS;
		return static_cast<size_t>( shift * SUB + ( v >> shift ) );
	}

	static uint64 lower( size_t i ){
		if( i < 2 * SUB ){
			return i;
		}
		uint64 shift = i / SUB - 1;
		return ( i - shift * SUB ) << shift;
	}

	static uint64 upper( size_t i ){
		if( i < 2 * SUB ){
			return i;
		}
		uint64 shift = i / SUB - 1;
		return ( ( i - shift * SUB + 1 ) << shift ) - 1;
	}

	void add( uint64 v ){
		if( v > MAX_VALUE ){
			v = MAX_VALUE;
		}
		counts[index( v )]++;
		total++;
		sum_us += v;
		if( v > max_us ){
			max_us = v;
		}
	}

	void merge( const Histogram& o ){
		for( size_t i = 0; i < BUCKETS; i++ ){
			counts[i] += o.counts[i];
		}
		total += o.total;
		sum_us += o.sum_us;
		if( o.max_us > max_us ){
			max_us = o.max_us;
		}
	}

	// Highest value equivalent to the p-th percentile (0 < p <= 1), capped at the maximum seen.
	uint64 percentile( double p ) const{
		if( total == 0 ){
			return 0;
		}
		uint64 rank = static_cast<uint64>( p * static_cast<double>( total ) + 0.999999 );
		if( rank < 1 ){
			rank = 1;
		}
		uint64 seen = 0;
		for( size_t i = 0; i < BUCKETS; i++ ){
			seen += counts[i];
			if( seen >= rank ){
				uint64 u = upper( i );
				return u < max_us ? u : max_us;
			}
		}
		return max_us;
	}
};

Histogram window_hist;
Histogram run_hist;
steady::time_point iteration_start;
bool in_iteration = false;
steady::time_point window_start;
steady::time_point run_start;
std::chrono::seconds interval{ 0 };
int32 ( *users_callback )( void ) = nullptr;
std::string json_path;

double ms( uint64 us ){
	return static_cast<double>( us ) / 1000.0;
}

void report( const Histogram& h, double seconds, bool final ){
	int32 users = users_callback != nullptr ? users_callback() : -1;
	uint64 p50 = h.percentile( 0.50 ), p95 = h.percentile( 0.95 ), p99 = h.percentile( 0.99 );
	double busy = seconds > 0 ? 100.0 * ms( h.sum_us ) / ( seconds * 1000.0 ) : 0.0;

	ShowInfo( "Tick stats (%s %.0fs): %" PRIu64 " iterations, p50 %.3f ms, p95 %.3f ms, p99 %.3f ms, max %.3f ms, busy %.1f%%, %d users\n",
		final ? "run" : "window", seconds, h.total, ms( p50 ), ms( p95 ), ms( p99 ), ms( h.max_us ), busy, users );

	if( json_path.empty() ){
		return;
	}
	FILE* fp = fopen( json_path.c_str(), "a" );
	if( fp == nullptr ){
		return;
	}
	fprintf( fp, "{\"time\":%lld,\"final\":%s,\"seconds\":%.3f,\"users\":%d,\"iterations\":%" PRIu64 ",\"busy_us\":%" PRIu64
		",\"p50_us\":%" PRIu64 ",\"p95_us\":%" PRIu64 ",\"p99_us\":%" PRIu64 ",\"max_us\":%" PRIu64 ",\"buckets\":[",
		static_cast<long long>( time( nullptr ) ), final ? "true" : "false", seconds, users, h.total, h.sum_us, p50, p95, p99, h.max_us );
	bool first = true;
	for( size_t i = 0; i < BUCKETS; i++ ){
		if( h.counts[i] == 0 ){
			continue;
		}
		fprintf( fp, "%s[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]", first ? "" : ",", Histogram::lower( i ), Histogram::upper( i ), h.counts[i] );
		first = false;
	}
	fprintf( fp, "]}\n" );
	fclose( fp );
}

double seconds_between( steady::time_point a, steady::time_point b ){
	return std::chrono::duration<double>( b - a ).count();
}

}

void tickstats_init( void ){
	const char* env = getenv( "RATHENA_TICKSTATS" );
	if( env == nullptr || atoi( env ) <= 0 ){
		return;
	}
	interval = std::chrono::seconds( atoi( env ) );
	const char* file = getenv( "RATHENA_TICKSTATS_FILE" );
	if( file != nullptr ){
		json_path = file;
	}
	run_start = window_start = steady::now();
	tickstats_enabled = true;
	ShowInfo( "Tick stats enabled: a summary every %d seconds%s%s.\n", atoi( env ), json_path.empty() ? "" : ", histograms to ", json_path.c_str() );
}

void tickstats_set_users_callback( int32 ( *callback )( void ) ){
	users_callback = callback;
}

void tickstats_wait_end_( void ){
	iteration_start = steady::now();
	in_iteration = true;
}

void tickstats_wait_begin_( void ){
	steady::time_point now = steady::now();
	if( in_iteration ){
		uint64 us = static_cast<uint64>( std::chrono::duration_cast<std::chrono::microseconds>( now - iteration_start ).count() );
		window_hist.add( us );
		in_iteration = false;
	}
	if( now - window_start >= interval ){
		report( window_hist, seconds_between( window_start, now ), false );
		run_hist.merge( window_hist );
		window_hist = Histogram();
		window_start = now;
	}
}

void tickstats_final( void ){
	if( !tickstats_enabled ){
		return;
	}
	run_hist.merge( window_hist );
	window_hist = Histogram();
	report( run_hist, seconds_between( run_start, steady::now() ), true );
	tickstats_enabled = false;
}
