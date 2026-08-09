//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_BitReader_Bench.cpp
//  Purpose: Benchmarks bounds-checked bitstream reading.
//  Details: Measures common narrow and word-sized field extraction from warm fixed
//           storage without allocation or transport overhead.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitReader.h"
#include "CypherCommon_BitWriter.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr usize CY_BIT_CURSOR_BENCH_FIELD_COUNT = 1024u;

} // namespace

static void BM_BitReader_5BitFields( benchmark::State &state )
{
    byte storage[( CY_BIT_CURSOR_BENCH_FIELD_COUNT * 5u + 7u ) / 8u]{};
    bit_writer_t writer{};
    benchmark::DoNotOptimize( BitWriter_Init( &writer, Span_FromArray( storage ) ) );
    for ( usize iField = 0u; iField < CY_BIT_CURSOR_BENCH_FIELD_COUNT; ++iField ) {
        benchmark::DoNotOptimize( BitWriter_WriteBits(
            &writer,
            static_cast<u64>( iField & 31u ),
            5u ) );
    }
    const binary_block_t source = BitWriter_Block( &writer );

    for ( auto _ : state ) {
        bit_reader_t reader{};
        benchmark::DoNotOptimize( BitReader_Init(
            &reader,
            source,
            BitWriter_BitsWritten( &writer ) ) );
        u64 value = 0u;
        for ( usize iField = 0u; iField < CY_BIT_CURSOR_BENCH_FIELD_COUNT; ++iField ) {
            benchmark::DoNotOptimize( BitReader_ReadBits( &reader, 5u, &value ) );
            benchmark::DoNotOptimize( value );
        }
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( CY_BIT_CURSOR_BENCH_FIELD_COUNT ) );
}

static void BM_BitReader_32BitFields( benchmark::State &state )
{
    byte storage[CY_BIT_CURSOR_BENCH_FIELD_COUNT * sizeof( u32 )]{};
    bit_writer_t writer{};
    benchmark::DoNotOptimize( BitWriter_Init( &writer, Span_FromArray( storage ) ) );
    for ( usize iField = 0u; iField < CY_BIT_CURSOR_BENCH_FIELD_COUNT; ++iField ) {
        benchmark::DoNotOptimize( BitWriter_WriteBits(
            &writer,
            static_cast<u32>( iField ),
            32u ) );
    }
    const binary_block_t source = BitWriter_Block( &writer );

    for ( auto _ : state ) {
        bit_reader_t reader{};
        benchmark::DoNotOptimize( BitReader_Init(
            &reader,
            source,
            BitWriter_BitsWritten( &writer ) ) );
        u64 value = 0u;
        for ( usize iField = 0u; iField < CY_BIT_CURSOR_BENCH_FIELD_COUNT; ++iField ) {
            benchmark::DoNotOptimize( BitReader_ReadBits( &reader, 32u, &value ) );
            benchmark::DoNotOptimize( value );
        }
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( storage ) ) );
}

BENCHMARK( BM_BitReader_5BitFields );
BENCHMARK( BM_BitReader_32BitFields );

