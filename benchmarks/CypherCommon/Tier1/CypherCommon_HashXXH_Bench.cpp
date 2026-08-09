//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_HashXXH_Bench.cpp
//  Purpose: Benchmarks high-throughput xxHash adapters.
//  Details: Separates identifier-sized and bulk-data workloads while measuring the
//           real out-of-line Cypher adapter used by runtime and tools.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_HashXXH.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

const std::array<byte, 65536u> g_hashXXHData = [] {
    std::array<byte, 65536u> data{};
    for ( usize iByte = 0u; iByte < data.size(); ++iByte ) {
        data[iByte] = static_cast<byte>( iByte * 131u + 17u );
    }
    return data;
}();

} // namespace

static void BM_HashXXH64_Data( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const binary_block_t data{ g_hashXXHData.data(), cbSize };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( HashXXH64_Data( data ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * static_cast<i64>( cbSize ) );
}

static void BM_HashXXH3_64_Data( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const binary_block_t data{ g_hashXXHData.data(), cbSize };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( HashXXH3_64_Data( data ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * static_cast<i64>( cbSize ) );
}

static void BM_HashXXH3_128_Data( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const binary_block_t data{ g_hashXXHData.data(), cbSize };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( HashXXH3_128_Data( data ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * static_cast<i64>( cbSize ) );
}

BENCHMARK( BM_HashXXH64_Data )->Arg( 32 )->Arg( 1024 )->Arg( 65536 );
BENCHMARK( BM_HashXXH3_64_Data )->Arg( 32 )->Arg( 1024 )->Arg( 65536 );
BENCHMARK( BM_HashXXH3_128_Data )->Arg( 32 )->Arg( 1024 )->Arg( 65536 );
