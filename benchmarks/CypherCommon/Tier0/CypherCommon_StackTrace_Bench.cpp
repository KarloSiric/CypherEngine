//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_StackTrace_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 stack trace capture cost.
//  Details: Stack capture is diagnostic-path work, not hot-path gameplay work.
//           This benchmark keeps the cost visible for asserts, validators,
//           crash reporting, and future editor diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon/Tier0/CypherCommon_StackTrace.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_StackTraceClear( benchmark::State &state )
{
    stack_trace_t trace{};
    for ( auto _ : state ) {
        Cy_StackTraceClear( &trace );
        benchmark::DoNotOptimize( trace.frame_count );
    }
}

void BM_StackTraceCapture8( benchmark::State &state )
{
    stack_trace_t trace{};
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_StackTraceCapture( &trace, 8u, 0u ) );
        benchmark::ClobberMemory();
    }
}

void BM_StackTraceCapture32( benchmark::State &state )
{
    stack_trace_t trace{};
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_StackTraceCapture( &trace, 32u, 0u ) );
        benchmark::ClobberMemory();
    }
}

} // namespace

BENCHMARK( BM_StackTraceClear );
BENCHMARK( BM_StackTraceCapture8 );
BENCHMARK( BM_StackTraceCapture32 );
