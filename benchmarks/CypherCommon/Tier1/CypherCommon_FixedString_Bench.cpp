//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_FixedString_Bench.cpp
//  Purpose: Benchmarks bounded fixed-string mutation.
//  Details: Measures overlap-safe assignment and incremental append without heap
//           allocation so regressions in the core text path remain visible.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_FixedString.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_FixedStringAssign( benchmark::State &state )
{
    const char source[] =
        "CypherEngine fixed strings keep bounded text on the stack.";
    const string_view_t view = StringView_FromCString( source );
    fixed_string_t<128u> string{};

    for ( auto _ : state ) {
        usize cchRequired = FixedString_Assign( &string, view );
        benchmark::DoNotOptimize( cchRequired );
        benchmark::DoNotOptimize( string.data );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( view.cchLength ) );
}

BENCHMARK( BM_FixedStringAssign );

static void BM_FixedStringAppend( benchmark::State &state )
{
    const string_view_t fragment = StringView_FromCString( "shader/" );
    fixed_string_t<256u> string{};

    for ( auto _ : state ) {
        FixedString_Clear( &string );
        for ( usize iAppend = 0u; iAppend < 16u; ++iAppend ) {
            usize cchRequired = FixedString_Append( &string, fragment );
            benchmark::DoNotOptimize( cchRequired );
        }
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        16 * static_cast<i64>( fragment.cchLength ) );
}

BENCHMARK( BM_FixedStringAppend );
