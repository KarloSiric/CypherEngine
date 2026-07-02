//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Timer.h
//  Purpose: Declares CypherCommon Tier0 Timer support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_TIMER_H
#define CYPHER_COMMON_TIER0_TIMER_H
#pragma once

/*
================
CypherCommon Timer

Monotonic timing helpers for profiling, frame timing and timeout code.
================
*/

#include "CypherCommon_BaseTypes.h"

#include <chrono>

namespace cypher::common
{

using timer_tick_t = u64;

constexpr f64 CYPHER_TIMER_NANOSECONDS_PER_SECOND = 1000000000.0;

// Returns monotonic timer ticks in nanoseconds.
inline timer_tick_t TimerNowTicks()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<timer_tick_t>( std::chrono::duration_cast<std::chrono::nanoseconds>( now ).count() );
}

// Converts nanosecond timer ticks to seconds.
inline f64 TimerTicksToSeconds( timer_tick_t ticks )
{
    return static_cast<f64>( ticks ) / CYPHER_TIMER_NANOSECONDS_PER_SECOND;
}

// Computes elapsed seconds between two timer tick values.
inline f64 TimerElapsedSeconds( timer_tick_t start_ticks, timer_tick_t end_ticks )
{
    return TimerTicksToSeconds( end_ticks - start_ticks );
}

// Returns current monotonic time in seconds.
inline f64 TimerNowSeconds()
{
    return TimerTicksToSeconds( TimerNowTicks() );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TIMER_H
