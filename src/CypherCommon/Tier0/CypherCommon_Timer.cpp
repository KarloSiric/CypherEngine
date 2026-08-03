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
#include <mutex>

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
    std::atomic<bool_t> isInitialized;
    std::mutex lifecycleMutex;
};

timer_state_t g_TimerState = {};

bool_t Timer_QueryNativeFrequency(
    timer_frequency_t &nOutFrequency ) noexcept
{
    nOutFrequency = 0u;

#if CYPHER_PLATFORM_WINDOWS
    LARGE_INTEGER frequency = {};
    if ( QueryPerformanceFrequency( &frequency ) == 0 ) {
        return CY_FALSE;
    }
    if ( frequency.QuadPart <= 0 ) {
        return CY_FALSE;
    }
    nOutFrequency = static_cast<timer_frequency_t>( frequency.QuadPart );
    
#elif CYPHER_PLATFORM_LINUX
    nOutFrequency = 1000000000ull;

#elif CYPHER_PLATFORM_MACOS
    nOutFrequency = 1000000000ull;

#else
    nOutFrequency = 1000000000ull;
#endif
    return CY_TRUE;
}

bool_t Timer_QueryNativeTicks( timer_tick_t &nOutTicks ) noexcept
{
    nOutTicks = 0u;
#if CYPHER_PLATFORM_WINDOWS
    LARGE_INTEGER counter = {};
    if ( QueryPerformanceCounter( &counter ) == 0 || counter.QuadPart < 0 ) {
        return CY_FALSE;
    }
    nOutTicks = static_cast<timer_tick_t>( counter.QuadPart );
#elif CYPHER_PLATFORM_LINUX
    timespec ts = {};
    if ( clock_gettime( CLOCK_MONOTONIC, &ts ) != 0 ) {
        return CY_FALSE;
    }
    if ( ts.tv_sec < 0 || ts.tv_nsec < 0 ) {
        return CY_FALSE;
    }
    nOutTicks =
        static_cast<timer_tick_t>( ts.tv_sec ) * 1000000000ull +
        static_cast<timer_tick_t>( ts.tv_nsec );
#elif CYPHER_PLATFORM_MACOS
    static mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info = {};
        if ( mach_timebase_info( &info ) != KERN_SUCCESS ) {
            info = {};
        }
        return info;
    }();
    if ( timebase.numer == 0u || timebase.denom == 0u ) {
        return CY_FALSE;
    }

    const u64 nRawTicks = mach_absolute_time();
    #if defined( __SIZEOF_INT128__ ) 
        const __uint128_t nNanoseconds = ( static_cast<__uint128_t>( nRawTicks ) * timebase.numer ) / timebase.denom;
        if ( nNanoseconds > CY_U64_MAX ) {
            return CY_FALSE;
        }
        nOutTicks = static_cast<timer_tick_t>( nNanoseconds );
    #else
        const u64 nWhole = ( nRawTicks / timebase.denom ) * timebase.numer;
        const u64 nRemainder =
            ( ( nRawTicks % timebase.denom ) * timebase.numer ) /
            timebase.denom;
        if ( nWhole > CY_U64_MAX - nRemainder ) {
            return CY_FALSE;
        }
        nOutTicks = nWhole + nRemainder;
    #endif
#else
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto nNanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>( now ).count();
    if ( nNanoseconds < 0 ) {
        return CY_FALSE;
    }
    nOutTicks = static_cast<timer_tick_t>( nNanoseconds );
#endif
    return CY_TRUE;
}

bool_t Timer_EnsureInitialized() noexcept
{
    if ( Cy_TimerIsInitialized() ) {
        return CY_TRUE;
    }

    return Cy_TimerInit();
}

}       // namespace
    
bool_t Cy_TimerInit() noexcept
{
    if ( Cy_TimerIsInitialized() ) {
        return CY_TRUE;
    }

    std::lock_guard<std::mutex> lock( g_TimerState.lifecycleMutex );
    if ( g_TimerState.isInitialized.load( std::memory_order_acquire ) ) {
        return CY_TRUE;
    }

    timer_frequency_t nFrequency = 0u;
    timer_tick_t nBaseTicks = 0u;
    if ( !Timer_QueryNativeFrequency( nFrequency ) ||
         !Timer_QueryNativeTicks( nBaseTicks ) ||
         nFrequency == 0u ) {
        return CY_FALSE;
    }

    g_TimerState.nFrequency.store( nFrequency, std::memory_order_release );
    g_TimerState.nBaseTicks.store( nBaseTicks, std::memory_order_release );
    g_TimerState.isInitialized.store( CY_TRUE, std::memory_order_release );
    
    return CY_TRUE;
}

void Cy_TimerShutdown() noexcept
{
    std::lock_guard<std::mutex> lock( g_TimerState.lifecycleMutex );
    g_TimerState.isInitialized.store( CY_FALSE, std::memory_order_release );
    g_TimerState.nFrequency.store( 0u, std::memory_order_release );
    g_TimerState.nBaseTicks.store( 0u, std::memory_order_release );
}

bool_t Cy_TimerIsInitialized() noexcept
{
    return g_TimerState.isInitialized.load( std::memory_order_acquire );
}

timer_frequency_t Cy_TimerGetFrequency() noexcept
{
    if ( !Timer_EnsureInitialized() ) {
        return 0;
    }

    return g_TimerState.nFrequency.load( std::memory_order_acquire );
}

bool_t Cy_TimerTryNowTicks( timer_tick_t *pOutTicks ) noexcept
{
    if ( pOutTicks == nullptr ) {
        return CY_FALSE;
    }
    *pOutTicks = 0u;

    if ( !Timer_EnsureInitialized() ) {
        return CY_FALSE;
    }

    timer_tick_t nNativeTicks = 0u;
    if ( !Timer_QueryNativeTicks( nNativeTicks ) ) {
        return CY_FALSE;
    }
    const timer_tick_t nBaseTicks = g_TimerState.nBaseTicks.load( std::memory_order_acquire );
    if ( nNativeTicks < nBaseTicks ) {
        return CY_FALSE;
    }

    *pOutTicks = nNativeTicks - nBaseTicks;
    return CY_TRUE;
}

timer_tick_t Cy_TimerNowTicks() noexcept
{
    timer_tick_t nTicks = 0u;
    static_cast<void>( Cy_TimerTryNowTicks( &nTicks ) );
    return nTicks;
}

f64 Cy_TimerTicksToSeconds( timer_tick_t nTicks ) noexcept
{
    const timer_frequency_t nFrequency = Cy_TimerGetFrequency();
    if ( nFrequency == 0u ) {
        return 0.0;
    }
    return static_cast<f64>( nTicks ) / static_cast<f64>( nFrequency );
}

f64 Cy_TimerTicksToMilliseconds( timer_tick_t nTicks ) noexcept
{
    return Cy_TimerTicksToSeconds( nTicks ) * 1000.0;
}

f64 Cy_TimerTicksToMicroseconds( timer_tick_t nTicks ) noexcept
{
    return Cy_TimerTicksToSeconds( nTicks ) * 1000000.0;
}

f64 Cy_TimerTicksToNanoseconds( timer_tick_t nTicks ) noexcept
{
    return Cy_TimerTicksToSeconds( nTicks ) * 1000000000.0;
}

f64 Cy_TimerElapsedSeconds(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept
{
    return Cy_TimerTicksToSeconds(
        Cy_TimerElapsedTicks( nStartTicks, nEndTicks ) );
}

f64 Cy_TimerElapsedMilliseconds(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept
{
    return Cy_TimerTicksToMilliseconds(
        Cy_TimerElapsedTicks( nStartTicks, nEndTicks ) );
}

f64 Cy_TimerElapsedMicroseconds(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept
{
    return Cy_TimerTicksToMicroseconds(
        Cy_TimerElapsedTicks( nStartTicks, nEndTicks ) );
}

f64 Cy_TimerElapsedNanoseconds(
    timer_tick_t nStartTicks,
    timer_tick_t nEndTicks ) noexcept
{
    return Cy_TimerTicksToNanoseconds(
        Cy_TimerElapsedTicks( nStartTicks, nEndTicks ) );
}

bool_t Cy_TimerBegin( cy_timer_t *pTimer ) noexcept
{
    if ( pTimer == nullptr ) {
        return CY_FALSE;
    }

    timer_tick_t nStartTicks = 0u;
    if ( !Cy_TimerTryNowTicks( &nStartTicks ) ) {
        *pTimer = {};
        return CY_FALSE;
    }

    pTimer->nStartTicks = nStartTicks;
    pTimer->nEndTicks = pTimer->nStartTicks;
    pTimer->isRunning = CY_TRUE;
    return CY_TRUE;
}

bool_t Cy_TimerEnd( cy_timer_t *pTimer ) noexcept
{
    if ( pTimer == nullptr || !pTimer->isRunning ) {
        return CY_FALSE;
    }

    timer_tick_t nEndTicks = 0u;
    if ( !Cy_TimerTryNowTicks( &nEndTicks ) ) {
        return CY_FALSE;
    }

    pTimer->nEndTicks = nEndTicks;
    pTimer->isRunning = CY_FALSE;
    return CY_TRUE;
}

bool_t Cy_TimerReset( cy_timer_t *pTimer ) noexcept
{
    return Cy_TimerBegin( pTimer );
}

timer_tick_t Cy_TimerGetTicks( const cy_timer_t *pTimer ) noexcept
{
    if ( pTimer == nullptr ) {
        return 0;
    }

    timer_tick_t nEndTicks = pTimer->nEndTicks;
    if ( pTimer->isRunning && !Cy_TimerTryNowTicks( &nEndTicks ) ) {
        return 0u;
    }
    return Cy_TimerElapsedTicks( pTimer->nStartTicks, nEndTicks );
}

f64 Cy_TimerGetSeconds( const cy_timer_t *pTimer ) noexcept
{
    return Cy_TimerTicksToSeconds( Cy_TimerGetTicks( pTimer ) );
}

f64 Cy_TimerGetMilliseconds( const cy_timer_t *pTimer ) noexcept
{
    return Cy_TimerTicksToMilliseconds( Cy_TimerGetTicks( pTimer ) );
}

f64 Cy_TimerGetMicroseconds( const cy_timer_t *pTimer ) noexcept
{
    return Cy_TimerTicksToMicroseconds( Cy_TimerGetTicks( pTimer ) );
}

f64 Cy_TimerGetNanoseconds( const cy_timer_t *pTimer ) noexcept
{
    return Cy_TimerTicksToNanoseconds( Cy_TimerGetTicks( pTimer ) );
}

timer_tick_t Cy_TimerDeadlineAfterTicks(
    timer_tick_t nDurationTicks ) noexcept
{
    const timer_tick_t nNowTicks = Cy_TimerNowTicks();
    return nDurationTicks > CY_U64_MAX - nNowTicks
        ? CY_U64_MAX
        : nNowTicks + nDurationTicks;
}

bool_t Cy_TimerHasReached( timer_tick_t nDeadlineTicks ) noexcept
{
    return Cy_TimerNowTicks() >= nDeadlineTicks;
}

}       // namespace cypher::common
