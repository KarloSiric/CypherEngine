//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StringFormat_Bench.cpp
//  Purpose: Benchmarks bounded text formatting.
//  Details: Measures general printf formatting and allocation-free integer grouping
//           separately because they serve different diagnostics hot paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringFormat.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_StringFormatPrintf( benchmark::State &state )
{
    char output[128]{};
    for ( auto _ : state ) {
        string_format_result_t result = StringFormat_Printf(
            output,
            sizeof( output ),
            "entity=%u position=(%.2f, %.2f, %.2f)",
            4096u,
            12.5,
            -3.25,
            91.0 );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_StringFormatPrintf );

static void BM_StringFormatGroupedInteger( benchmark::State &state )
{
    char output[64]{};
    for ( auto _ : state ) {
        string_format_result_t result = StringFormat_GroupedInteger(
            9223372036854775807LL,
            ',',
            output,
            sizeof( output ) );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_StringFormatGroupedInteger );
