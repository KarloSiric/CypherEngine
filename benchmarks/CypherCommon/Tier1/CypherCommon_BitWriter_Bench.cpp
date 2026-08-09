//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_BitWriter_Bench.cpp
//  Purpose: Benchmarks bounds-checked bitstream writing.
//  Details: Measures common narrow and word-sized field packing into warm fixed
//           storage without allocation or transport overhead.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitWriter.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr usize CY_BIT_CURSOR_BENCH_FIELD_COUNT = 1024u;

} // namespace

static void BM_BitWriter_5BitFields( benchmark::State &state )
{
    byte storage[( CY_BIT_CURSOR_BENCH_FIELD_COUNT * 5u + 7u ) / 8u]{};
    bit_writer_t writer{};
    benchmark::DoNotOptimize( BitWriter_Init( &writer, Span_FromArray( storage ) ) );

    for ( auto _ : state ) {
        BitWriter_Reset( &writer );
        for ( usize iField = 0u; iField < CY_BIT_CURSOR_BENCH_FIELD_COUNT; ++iField ) {
            benchmark::DoNotOptimize( BitWriter_WriteBits(
                &writer,
                static_cast<u64>( iField & 31u ),
                5u ) );
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( CY_BIT_CURSOR_BENCH_FIELD_COUNT ) );
}

static void BM_BitWriter_32BitFields( benchmark::State &state )
{
    byte storage[CY_BIT_CURSOR_BENCH_FIELD_COUNT * sizeof( u32 )]{};
    bit_writer_t writer{};
    benchmark::DoNotOptimize( BitWriter_Init( &writer, Span_FromArray( storage ) ) );

    for ( auto _ : state ) {
        BitWriter_Reset( &writer );
        for ( usize iField = 0u; iField < CY_BIT_CURSOR_BENCH_FIELD_COUNT; ++iField ) {
            benchmark::DoNotOptimize( BitWriter_WriteBits(
                &writer,
                static_cast<u32>( iField ),
                32u ) );
        }
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( storage ) ) );
}

BENCHMARK( BM_BitWriter_5BitFields );
BENCHMARK( BM_BitWriter_32BitFields );

