//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_BinaryBlock_Bench.cpp
//  Purpose: Benchmarks immutable binary-block slicing.
//  Details: Measures the out-of-line checked semantic wrapper used by resource,
//           packet, hashing, and serialization paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BinaryBlock.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

const std::array<byte, 4096u> g_binaryData{};

} // namespace

static void BM_BinaryBlockSubblock( benchmark::State &state )
{
    const binary_block_t block =
        BinaryBlock_FromData( g_binaryData.data(), g_binaryData.size() );
    const usize cbSlice = static_cast<usize>( state.range( 0 ) );
    usize iOffset = 0u;

    for ( auto _ : state ) {
        binary_block_t slice =
            BinaryBlock_Subblock( block, iOffset, cbSlice );
        benchmark::DoNotOptimize( slice );
        iOffset = ( iOffset + 31u ) & 2047u;
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_BinaryBlockSubblock )->Arg( 16 )->Arg( 256 )->Arg( 2048 );
