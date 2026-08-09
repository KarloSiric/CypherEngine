//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_ChecksumCRC32_Bench.cpp
//  Purpose: Benchmarks CRC-32 checksum throughput.
//  Details: Measures representative packet, record, and asset-block sizes using
//           warm contiguous data.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ChecksumCRC32.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

const std::array<byte, 65536u> g_crc32Data{};

} // namespace

static void BM_ChecksumCRC32_Data( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const binary_block_t data{ g_crc32Data.data(), cbSize };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ChecksumCRC32_Data( data ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * static_cast<i64>( cbSize ) );
}

BENCHMARK( BM_ChecksumCRC32_Data )->Arg( 64 )->Arg( 1024 )->Arg( 65536 );
