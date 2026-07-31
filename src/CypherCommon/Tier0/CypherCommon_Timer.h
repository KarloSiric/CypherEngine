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
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Timer

Low-level monotonic timer API.

This timer measures elapsed time using native platform ticks. It does not own
frame timing, gameplay timers, calendar time, sleep/yield, or profiler storage.
Higher-level systems build those policies on top of this clock.
================
*/
#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using timer_tick_t = u64;
using timer_frequency_t = u64;

struct cy_timer_t {
    timer_tick_t nStartTicks = 0u;
    timer_tick_t nEndTicks = 0u;
    bool_t isRunning = CY_FALSE;
};

// Initializes the process monotonic clock baseline.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TimerInit() noexcept;

// Clears the baseline. Call only when timer users are quiescent.
CYPHER_COMMON_API void Cy_TimerShutdown() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TimerIsInitialized() noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API timer_frequency_t Cy_TimerGetFrequency() noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API timer_tick_t Cy_TimerNowTicks() noexcept;

// Returns end-start, or zero for a reversed interval.
CYPHER_NODISCARD constexpr timer_tick_t Cy_TimerElapsedTicks(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept
{
    return nEndTicks >= nStartTicks ? nEndTicks - nStartTicks : 0u;
}

CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerTicksToSeconds(
    timer_tick_t nTicks ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerTicksToMilliseconds(
    timer_tick_t nTicks ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerTicksToMicroseconds(
    timer_tick_t nTicks ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerTicksToNanoseconds(
    timer_tick_t nTicks ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerElapsedSeconds(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerElapsedMilliseconds(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerElapsedMicroseconds(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerElapsedNanoseconds(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TimerBegin( cy_timer_t *pTimer ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TimerEnd( cy_timer_t *pTimer ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TimerReset( cy_timer_t *pTimer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API timer_tick_t Cy_TimerGetTicks(
    const cy_timer_t *pTimer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerGetSeconds(
    const cy_timer_t *pTimer ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerGetMilliseconds(
    const cy_timer_t *pTimer ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerGetMicroseconds(
    const cy_timer_t *pTimer ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_TimerGetNanoseconds(
    const cy_timer_t *pTimer ) noexcept;

// Adds a relative tick duration to now, saturating on overflow.
CYPHER_NODISCARD CYPHER_COMMON_API timer_tick_t Cy_TimerDeadlineAfterTicks(
    timer_tick_t nDurationTicks ) noexcept;

// Returns true once the monotonic clock reaches a deadline.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_TimerHasReached(
    timer_tick_t nDeadlineTicks ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TIMER_H
