//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Hash_Bench.cpp
//  Purpose: Benchmarks engine-default hash helpers.
//  Details: Measures ordinary identifiers, ASCII-folded paths, and hash combining
//           through the public facade used by containers and resource systems.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Hash.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_Hash64_String( benchmark::State &state )
{
    const string_view_t text =
        StringView_FromCString( "materials/world/metal_panel_01" );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Hash64_String( text ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.cchLength ) );
}

static void BM_Hash64_StringInsensitiveAscii( benchmark::State &state )
{
    const string_view_t text =
        StringView_FromCString( "Materials/World/Metal_Panel_01" );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Hash64_StringInsensitiveAscii( text ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.cchLength ) );
}

static void BM_Hash64_Combine( benchmark::State &state )
{
    hash64_t hash = 0x0123456789ABCDEFull;
    for ( auto _ : state ) {
        hash = Hash64_Combine( hash, 0x9E3779B97F4A7C15ull );
        benchmark::DoNotOptimize( hash );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_Hash64_String );
BENCHMARK( BM_Hash64_StringInsensitiveAscii );
BENCHMARK( BM_Hash64_Combine );
