//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_ByteReader_Bench.cpp
//  Purpose: Benchmarks bounds-checked binary reading.
//  Details: Measures fixed-width and variable-width decoding over warm contiguous
//           buffers representative of cooked asset records and network messages.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr usize CY_BYTE_CURSOR_BENCH_VALUE_COUNT = 1024u;

} // namespace

static void BM_ByteReader_U32Sequential( benchmark::State &state )
{
    byte storage[CY_BYTE_CURSOR_BENCH_VALUE_COUNT * sizeof( u32 )]{};
    byte_writer_t writer{};
    benchmark::DoNotOptimize( ByteWriter_Init(
        &writer,
        Span_FromArray( storage ),
        data_byte_order_t::LITTLE ) );
    for ( usize iValue = 0u; iValue < CY_BYTE_CURSOR_BENCH_VALUE_COUNT; ++iValue ) {
        benchmark::DoNotOptimize( ByteWriter_WriteU32(
            &writer,
            static_cast<u32>( iValue ) ) );
    }
    const binary_block_t source = ByteWriter_Block( &writer );

    for ( auto _ : state ) {
        byte_reader_t reader{};
        benchmark::DoNotOptimize( ByteReader_Init(
            &reader,
            source,
            data_byte_order_t::LITTLE ) );
        u32 value = 0u;
        for ( usize iValue = 0u; iValue < CY_BYTE_CURSOR_BENCH_VALUE_COUNT; ++iValue ) {
            benchmark::DoNotOptimize( ByteReader_ReadU32( &reader, &value ) );
            benchmark::DoNotOptimize( value );
        }
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( source.cbSize ) );
}

static void BM_ByteReader_VarU64Sequential( benchmark::State &state )
{
    byte storage[CY_BYTE_CURSOR_BENCH_VALUE_COUNT * 10u]{};
    byte_writer_t writer{};
    benchmark::DoNotOptimize( ByteWriter_Init( &writer, Span_FromArray( storage ) ) );
    for ( usize iValue = 0u; iValue < CY_BYTE_CURSOR_BENCH_VALUE_COUNT; ++iValue ) {
        const u64 value = static_cast<u64>( iValue ) * 127u;
        benchmark::DoNotOptimize( ByteWriter_WriteVarU64( &writer, value ) );
    }
    const binary_block_t source = ByteWriter_Block( &writer );

    for ( auto _ : state ) {
        byte_reader_t reader{};
        benchmark::DoNotOptimize( ByteReader_Init( &reader, source ) );
        u64 value = 0u;
        for ( usize iValue = 0u; iValue < CY_BYTE_CURSOR_BENCH_VALUE_COUNT; ++iValue ) {
            benchmark::DoNotOptimize( ByteReader_ReadVarU64( &reader, &value ) );
            benchmark::DoNotOptimize( value );
        }
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( source.cbSize ) );
}

BENCHMARK( BM_ByteReader_U32Sequential );
BENCHMARK( BM_ByteReader_VarU64Sequential );

