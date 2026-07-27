//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Thread_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 thread helper costs.
//  Details: These measurements track low-level thread identity and scheduling
//           helpers used by diagnostics, profiling, and future worker systems.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Thread.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_ThreadGetCurrentId( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_ThreadGetCurrentId() );
    }
}

void BM_ThreadIsMainThread( benchmark::State &state )
{
    if ( !Cy_ThreadCaptureMainThread() ) {
        state.SkipWithError( "main thread capture failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_ThreadIsMainThread() );
    }
}

void BM_ThreadGetLogicalCount( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_ThreadGetLogicalCount() );
    }
}

void BM_ThreadYield( benchmark::State &state )
{
    for ( auto _ : state ) {
        Cy_ThreadYield();
    }
}

void BM_ThreadSleepUs_Zero( benchmark::State &state )
{
    for ( auto _ : state ) {
        Cy_ThreadSleepUs( 0u );
    }
}

} // namespace

BENCHMARK( BM_ThreadGetCurrentId );
BENCHMARK( BM_ThreadIsMainThread );
BENCHMARK( BM_ThreadGetLogicalCount );
BENCHMARK( BM_ThreadYield );
BENCHMARK( BM_ThreadSleepUs_Zero );
