//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Timer.cpp
//  Purpose: Implements CypherCommon Tier0 monotonic timer support.
//  Details: This file hides platform timer APIs behind the small Timer.h
//           contract used by profiling, diagnostics, benchmarks and later
//           frame timing code.
//
//  History:
//  - Created by Karlo Siric on 2026-07-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Timer.h"
#include "CypherCommon_Platform.h"

#include <atomic>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif CYPHER_PLATFORM_LINUX
    #include <time.h>
#elif CYPHER_PLATFORM_MACOS
    #include <mach/mach_time.h>
#else
    #include <chrono>
#endif

namespace cypher::common
{
    
namespace 
{

struct timer_state_t {
    std::atomic<timer_frequency_t> nFrequency;
    std::atomic<timer_tick_t> nBaseTicks;
    std::atomic<bool_t> bInitialized;
};

timer_state_t g_TimerState = {};

timer_frequency_t Timer_QueryNativeFrequency()
{

#if CYPHER_PLATFORM_WINDOWS
    LARGE_INTEGER frequency = {};
    if ( QueryPerformanceFrequency( &frequency ) == 0 ) {
        return 0;
    }
    
    return static_cast<timer_frequency_t>( frequency.QuadPart );
    
#elif CYPHER_PLATFORM_LINUX
    return 1000000000LL;

#elif CYPHER_PLATFORM_MACOS
    return 1000000000LL;

#else
    return 1000000000LL;
#endif
}

timer_tick_t Timer_QueryNativeTicks()
{
#if CYPHER_PLATFORM_WINDOWS
    LARGE_INTEGER counter = {};
    if ( QueryPerformanceCounter( &counter ) == 0 ) {
        return 0;
    }
    return static_cast<timer_tick_t>( counter.QuadPart );
#elif CYPHER_PLATFORM_LINUX
    timespec ts = {};
    if ( clock_gettime( CLOCK_MONOTONIC, &ts ) != 0 ) {
        return 0;
    }
    return static_cast<timer_tick_t>( ts.tv_sec ) * 1000000000LL + static_cast<timer_tick_t>( ts.tv_nsec );
#elif CYPHER_PLATFORM_MACOS
    static mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info = {};
        mach_timebase_info( &info );
        return info;
    }();
    
    const u64 nRawTicks = mach_absolute_time();
    #if defined( __SIZEOF_INT128__ ) 
        const __uint128_t nNanoseconds = ( static_cast<__uint128_t>( nRawTicks ) * timebase.numer ) / timebase.denom;
        return static_cast<timer_tick_t>( nNanoseconds );
    #else
        return static_cast<timer_tick_t>( ( nRawTicks / timebase.denom ) * timebase.numer );
    #endif
#else
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<timer_tick_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>( now ).count()
    );
#endif
}

bool_t Timer_EnsureInitialized()
{
    if ( Timer_IsInitialized() ) {
        return CY_TRUE;
    }

    return Timer_Init();
}

}       // namespace
    
bool_t Timer_Init()
{
    if ( Timer_IsInitialized() ) {
        return CY_TRUE;
    }

    const timer_frequency_t nFrequency = Timer_QueryNativeFrequency();
    if ( nFrequency <= 0 ) {
        return CY_FALSE;
    }
    
    const timer_tick_t nBaseTicks = Timer_QueryNativeTicks();
    g_TimerState.nFrequency.store( nFrequency, std::memory_order_release );
    g_TimerState.nBaseTicks.store( nBaseTicks, std::memory_order_release );
    g_TimerState.bInitialized.store( CY_TRUE, std::memory_order_release );
    
    return CY_TRUE;
}

void Timer_Shutdown()
{
    g_TimerState.bInitialized.store( CY_FALSE, std::memory_order_release );
    g_TimerState.nFrequency.store( 0, std::memory_order_release );
    g_TimerState.nBaseTicks.store( 0, std::memory_order_release );
    return ;
}

bool_t Timer_IsInitialized()
{
    return g_TimerState.bInitialized.load( std::memory_order_acquire );
}

timer_frequency_t Timer_GetFrequency()
{
    if ( !Timer_EnsureInitialized() ) {
        return 0;
    }

    return g_TimerState.nFrequency.load( std::memory_order_acquire );
}

timer_tick_t Timer_NowTicks()
{
    if ( !Timer_EnsureInitialized() ) {
        return 0;
    }

    const timer_tick_t nNativeTicks = Timer_QueryNativeTicks();
    const timer_tick_t nBaseTicks = g_TimerState.nBaseTicks.load( std::memory_order_acquire );

    return nNativeTicks - nBaseTicks;
}

timer_tick_t Timer_ElapsedTicks( timer_tick_t nStartTicks, timer_tick_t nEndTicks )
{
    return nEndTicks - nStartTicks;
}

f64 Timer_TicksToSeconds( timer_tick_t nTicks )
{
    const timer_frequency_t nFrequency = Timer_GetFrequency();
    if ( nFrequency <= 0 ) {
        return 0.0;
    }
    return static_cast<f64>( nTicks ) / static_cast<f64>( nFrequency );
}

f64 Timer_TicksToMilliseconds( timer_tick_t nTicks )
{
    return Timer_TicksToSeconds( nTicks ) * 1000.0;
}

f64 Timer_TicksToMicroseconds( timer_tick_t nTicks )
{
    return Timer_TicksToSeconds( nTicks ) * 1000000.0;
}

f64 Timer_TicksToNanoseconds( timer_tick_t nTicks )
{
    return Timer_TicksToSeconds( nTicks ) * 1000000000.0;
}

f64 Timer_ElapsedSeconds( timer_tick_t nStartTicks, timer_tick_t nEndTicks )
{
    return Timer_TicksToSeconds( Timer_ElapsedTicks( nStartTicks, nEndTicks ) );
}

f64 Timer_ElapsedMilliseconds( timer_tick_t nStartTicks, timer_tick_t nEndTicks )
{
    return Timer_TicksToMilliseconds( Timer_ElapsedTicks( nStartTicks, nEndTicks ) );
}

f64 Timer_ElapsedMicroseconds( timer_tick_t nStartTicks, timer_tick_t nEndTicks )
{
    return Timer_TicksToMicroseconds( Timer_ElapsedTicks( nStartTicks, nEndTicks ) );
}

f64 Timer_ElapsedNanoseconds( timer_tick_t nStartTicks, timer_tick_t nEndTicks )
{
    return Timer_TicksToNanoseconds( Timer_ElapsedTicks( nStartTicks, nEndTicks ) );
}

void Timer_Begin( cy_timer_t *pTimer )
{
    if ( pTimer == nullptr ) {
        return;
    }

    pTimer->nStartTicks = Timer_NowTicks();
    pTimer->nEndTicks = pTimer->nStartTicks;
}

void Timer_End( cy_timer_t *pTimer )
{
    if ( pTimer == nullptr ) {
        return;
    }

    pTimer->nEndTicks = Timer_NowTicks();
}

void Timer_Reset( cy_timer_t *pTimer )
{
    Timer_Begin( pTimer );
}

timer_tick_t Timer_GetTicks( const cy_timer_t *pTimer )
{
    if ( pTimer == nullptr ) {
        return 0;
    }

    return Timer_ElapsedTicks( pTimer->nStartTicks, pTimer->nEndTicks );
}

f64 Timer_GetSeconds( const cy_timer_t *pTimer )
{
    return Timer_TicksToSeconds( Timer_GetTicks( pTimer ) );
}

f64 Timer_GetMilliseconds( const cy_timer_t *pTimer )
{
    return Timer_TicksToMilliseconds( Timer_GetTicks( pTimer ) );
}

f64 Timer_GetMicroseconds( const cy_timer_t *pTimer )
{
    return Timer_TicksToMicroseconds( Timer_GetTicks( pTimer ) );
}

f64 Timer_GetNanoseconds( const cy_timer_t *pTimer )
{
    return Timer_TicksToNanoseconds( Timer_GetTicks( pTimer ) );
}

}       // namespace cypher::common
