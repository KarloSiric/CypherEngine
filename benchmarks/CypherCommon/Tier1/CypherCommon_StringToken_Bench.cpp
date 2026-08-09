//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StringToken_Bench.cpp
//  Purpose: Benchmarks compact string token generation and comparison.
//  Details: Generation measures the underlying fast content hash while comparison
//           isolates the fixed-size token path used after candidate lookup.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringToken.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_StringTokenGenerate( benchmark::State &state )
{
    const string_view_t text = StringView_FromCString(
        "materials/facility/industrial_wall_panel_01.cymat" );
    for ( auto _ : state ) {
        string_token_t token = StringToken_FromView( text );
        benchmark::DoNotOptimize( token );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.cchLength ) );
}

BENCHMARK( BM_StringTokenGenerate );

static void BM_StringTokenCompare( benchmark::State &state )
{
    const string_token_t left = StringToken_FromView(
        StringView_FromCString( "renderer.opengl" ) );
    const string_token_t right = left;
    for ( auto _ : state ) {
        bool_t bEqual = StringToken_Equals( left, right );
        benchmark::DoNotOptimize( bEqual );
    }
}

BENCHMARK( BM_StringTokenCompare );
