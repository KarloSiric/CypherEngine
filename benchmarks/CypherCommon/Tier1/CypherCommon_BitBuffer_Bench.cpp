//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_BitBuffer_Bench.cpp
//  Purpose: Benchmarks fixed-capacity bit storage operations.
//  Details: Measures deterministic range initialization and repeated indexed bit
//           mutation over caller-owned warm memory.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitBuffer.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_BitBuffer_ResizeFill( benchmark::State &state )
{
    byte storage[4096]{};
    bit_buffer_t buffer{};
    benchmark::DoNotOptimize( BitBuffer_Init( &buffer, Span_FromArray( storage ) ) );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( BitBuffer_Resize(
            &buffer,
            sizeof( storage ) * 8u,
            CY_TRUE ) );
        benchmark::DoNotOptimize( BitBuffer_Resize( &buffer, 0u ) );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( storage ) * 2u ) );
}

static void BM_BitBuffer_SetGet( benchmark::State &state )
{
    byte storage[512]{};
    bit_buffer_t buffer{};
    benchmark::DoNotOptimize( BitBuffer_Init( &buffer, Span_FromArray( storage ) ) );
    benchmark::DoNotOptimize( BitBuffer_Resize( &buffer, sizeof( storage ) * 8u ) );

    for ( auto _ : state ) {
        for ( usize iBit = 0u; iBit < sizeof( storage ) * 8u; iBit += 7u ) {
            benchmark::DoNotOptimize( BitBuffer_Set( &buffer, iBit, CY_TRUE ) );
            bool_t value = CY_FALSE;
            benchmark::DoNotOptimize( BitBuffer_Get( &buffer, iBit, &value ) );
            benchmark::DoNotOptimize( value );
        }
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( ( sizeof( storage ) * 8u + 6u ) / 7u ) );
}

BENCHMARK( BM_BitBuffer_ResizeFill );
BENCHMARK( BM_BitBuffer_SetGet );

