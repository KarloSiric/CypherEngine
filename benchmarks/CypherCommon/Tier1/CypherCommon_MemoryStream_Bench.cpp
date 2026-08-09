//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_MemoryStream_Bench.cpp
//  Purpose: Benchmarks memory-backed stream transfers.
//  Details: Measures callback dispatch, bounds checks, high-water tracking, and
//           contiguous memory copies over a reusable borrowed buffer.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryStream.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_MemoryStream_WriteRead4096( benchmark::State &state )
{
    byte storage[4096]{};
    byte transfer[4096]{};
    memory_stream_t memory{};
    benchmark::DoNotOptimize( MemoryStream_InitWrite(
        &memory,
        Span_FromArray( storage ) ) );
    stream_t stream = MemoryStream_AsStream( &memory );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( MemoryStream_Clear( &memory ) );
        benchmark::DoNotOptimize( Stream_WriteExact(
            &stream,
            transfer,
            sizeof( transfer ) ) );
        MemoryStream_Reset( &memory );
        benchmark::DoNotOptimize( Stream_ReadExact(
            &stream,
            transfer,
            sizeof( transfer ) ) );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( transfer ) * 2u ) );
}

BENCHMARK( BM_MemoryStream_WriteRead4096 );

