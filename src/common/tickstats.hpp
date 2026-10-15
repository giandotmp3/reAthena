// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef TICKSTATS_HPP
#define TICKSTATS_HPP

#include "cbasetypes.hpp"

/*
 * Main-loop ("sim tick") latency statistics.
 *
 * One loop iteration is the work between two waits in the socket poll: wake up, receive, parse
 * and send, run the due timers, flush, and go back to sleep. Its duration excludes the idle wait
 * itself. Durations go into a log-linear histogram (32 sub-buckets per power of two, so every
 * percentile is within ~3% of the true value) from which p50/p95/p99/max are reported.
 *
 * Off by default. Enabled by the environment:
 *   RATHENA_TICKSTATS=<seconds>      log a one-line summary of each window of this many seconds
 *                                    (0 or unset: off) and a summary of the whole run on shutdown
 *   RATHENA_TICKSTATS_FILE=<path>    also append each window's histogram as one JSON line
 * When off, the hooks cost one predictable branch per loop iteration.
 */

extern bool tickstats_enabled;

void tickstats_init( void );
void tickstats_final( void );
// Called by do_sockets right before and right after it waits for socket events.
void tickstats_wait_begin_( void );
void tickstats_wait_end_( void );
// Optional: how many players are online, reported with each summary (the map server sets it).
void tickstats_set_users_callback( int32 (*callback)( void ) );

inline void tickstats_wait_begin( void ){
	if( tickstats_enabled ){
		tickstats_wait_begin_();
	}
}

inline void tickstats_wait_end( void ){
	if( tickstats_enabled ){
		tickstats_wait_end_();
	}
}

#endif /* TICKSTATS_HPP */
