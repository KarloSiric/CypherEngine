//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StringBuilder_Bench.cpp
//  Purpose: Benchmarks bounded non-owning text construction.
//  Details: Measures fragment assembly separately from printf formatting while
//           keeping all output in caller-provided stack storage.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringBuilder.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_StringBuilderAppendFragments( benchmark::State &state )
{
    const string_view_t fragment = StringView_FromCString( "shader/" );
    char storage[256]{};
    string_builder_t builder{};

    for ( auto _ : state ) {
        bool_t bInitialized =
            StringBuilder_Init( &builder, storage, sizeof( storage ) );
        benchmark::DoNotOptimize( bInitialized );
        for ( usize iFragment = 0u; iFragment < 16u; ++iFragment ) {
            string_builder_status_t status =
                StringBuilder_Append( &builder, fragment );
            benchmark::DoNotOptimize( status );
        }
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        16 * static_cast<i64>( fragment.cchLength ) );
}

BENCHMARK( BM_StringBuilderAppendFragments );

static void BM_StringBuilderAppendFormat( benchmark::State &state )
{
    char storage[128]{};
    string_builder_t builder{};

    for ( auto _ : state ) {
        bool_t bInitialized =
            StringBuilder_Init( &builder, storage, sizeof( storage ) );
        string_builder_status_t status = StringBuilder_AppendFormat(
            &builder,
            "entity=%u origin=(%.2f, %.2f, %.2f)",
            8192u,
            10.0,
            20.0,
            30.0 );
        benchmark::DoNotOptimize( bInitialized );
        benchmark::DoNotOptimize( status );
        benchmark::DoNotOptimize( storage );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_StringBuilderAppendFormat );
