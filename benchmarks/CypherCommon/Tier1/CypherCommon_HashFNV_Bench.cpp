//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_HashFNV_Bench.cpp
//  Purpose: Benchmarks deterministic FNV-1a hashing.
//  Details: Measures identifier-sized and block-sized inputs separately because
//           FNV is primarily intended for stable IDs rather than bulk asset hashing.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_HashFNV.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

const std::array<byte, 4096u> g_hashFNVData = [] {
    std::array<byte, 4096u> data{};
    for ( usize iByte = 0u; iByte < data.size(); ++iByte ) {
        data[iByte] = static_cast<byte>( iByte * 131u + 17u );
    }
    return data;
}();

} // namespace

static void BM_HashFNV1a64_Identifier( benchmark::State &state )
{
    const string_view_t text = StringView_FromCString( "materials/world/metal_panel_01" );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( HashFNV1a64_String( text ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.cchLength ) );
}

static void BM_HashFNV1a64_Block( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const binary_block_t data{ g_hashFNVData.data(), cbSize };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( HashFNV1a64_Data( data ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbSize ) );
}

BENCHMARK( BM_HashFNV1a64_Identifier );
BENCHMARK( BM_HashFNV1a64_Block )->Arg( 64 )->Arg( 1024 )->Arg( 4096 );
