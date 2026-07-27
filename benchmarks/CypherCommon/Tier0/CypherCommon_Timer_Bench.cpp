//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Timer_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 timer performance.
//  Details: This benchmark measures monotonic clock query and tick conversion costs
//           that matter for profiling, frame timing, and diagnostic instrumentation.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Timer.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

constexpr usize kTimerValueCount = 1024u;

struct timer_bench_values_t {
    std::array<timer_tick_t, kTimerValueCount> nStartTicks{};
    std::array<timer_tick_t, kTimerValueCount> nEndTicks{};
};

timer_bench_values_t BuildTimerValues()
{
    timer_bench_values_t values{};

    for ( usize i = 0u; i < kTimerValueCount; ++i ) {
        values.nStartTicks[i] = static_cast<timer_tick_t>( i * 1000000u );
        values.nEndTicks[i] = values.nStartTicks[i] + static_cast<timer_tick_t>( ( i + 1u ) * 1000u );
    }

    return values;
}

const timer_bench_values_t &GetTimerValues()
{
    static const timer_bench_values_t values = BuildTimerValues();
    return values;
}

void BM_TimerNowTicks( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_TimerNowTicks() );
    }
}

void BM_TimerNowMilliseconds( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_TimerTicksToMilliseconds( Cy_TimerNowTicks() ) );
    }
}

void BM_TimerTicksToSeconds_1024Values( benchmark::State &state )
{
    const timer_bench_values_t &values = GetTimerValues();

    for ( auto _ : state ) {
        f64 flAccum = 0.0;
        for ( timer_tick_t nTicks : values.nEndTicks ) {
            flAccum += Cy_TimerTicksToSeconds( nTicks );
        }
        benchmark::DoNotOptimize( flAccum );
    }
}

void BM_TimerElapsedSeconds_1024Values( benchmark::State &state )
{
    const timer_bench_values_t &values = GetTimerValues();

    for ( auto _ : state ) {
        f64 flAccum = 0.0;
        for ( usize i = 0u; i < kTimerValueCount; ++i ) {
            flAccum += Cy_TimerElapsedSeconds( values.nStartTicks[i], values.nEndTicks[i] );
        }
        benchmark::DoNotOptimize( flAccum );
    }
}

} // namespace

BENCHMARK( BM_TimerNowTicks );
BENCHMARK( BM_TimerNowMilliseconds );
BENCHMARK( BM_TimerTicksToSeconds_1024Values );
BENCHMARK( BM_TimerElapsedSeconds_1024Values );
