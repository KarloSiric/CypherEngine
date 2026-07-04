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

Low-level monotonic timer API.

This timer measures elapsed time using native platform ticks. It does not own
frame timing, gameplay timers, calendar time, sleep/yield, or profiler storage.
Higher-level systems build those policies on top of this clock.
================
*/
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using timer_tick_t = i64;
using timer_frequency_t = i64;

struct timer_t {
    timer_tick_t nStartTicks;
    timer_tick_t nEndTicks;
};

bool_t Timer_Init();
void Timer_Shutdown();
bool_t Timer_IsInitialized();

timer_frequency_t Timer_GetFrequency();

timer_tick_t Timer_NowTicks();

timer_tick_t Timer_ElapsedTicks( timer_tick_t nStartTicks, timer_tick_t nEndTicks );

f64 Timer_TicksToSeconds( timer_tick_t nTicks );
f64 Timer_TicksToMilliseconds( timer_tick_t nTicks );
f64 Timer_TicksToMicroseconds( timer_tick_t nTicks );
f64 Timer_TicksToNanoseconds( timer_tick_t nTicks );

f64 Timer_ElapsedSeconds( timer_tick_t nStartTicks, timer_tick_t nEndTicks );
f64 Timer_ElapsedMilliseconds( timer_tick_t nStartTicks, timer_tick_t nEndTicks );
f64 Timer_ElapsedMicroseconds( timer_tick_t nStartTicks, timer_tick_t nEndTicks );
f64 Timer_ElapsedNanoseconds( timer_tick_t nStartTicks, timer_tick_t nEndTicks );

void Timer_Begin( timer_t *pTimer );
void Timer_End( timer_t *pTimer );
void Timer_Reset( timer_t *pTimer );

timer_tick_t Timer_GetTicks( const timer_t *pTimer );

f64 Timer_GetSeconds( const timer_t *pTimer );
f64 Timer_GetMilliseconds( const timer_t *pTimer );
f64 Timer_GetMicroseconds( const timer_t *pTimer );
f64 Timer_GetNanoseconds( const timer_t *pTimer );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TIMER_H
