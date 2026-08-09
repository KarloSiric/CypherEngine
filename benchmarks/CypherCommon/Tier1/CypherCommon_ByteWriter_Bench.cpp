//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_ByteWriter_Bench.cpp
//  Purpose: Benchmarks bounds-checked binary writing.
//  Details: Measures fixed-width and variable-width encoding into warm fixed storage
//           without allocation, file I/O, or unrelated setup in the timed loop.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ByteWriter.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr usize CY_BYTE_CURSOR_BENCH_VALUE_COUNT = 1024u;

} // namespace

static void BM_ByteWriter_U32Sequential( benchmark::State &state )
{
    byte storage[CY_BYTE_CURSOR_BENCH_VALUE_COUNT * sizeof( u32 )]{};
    byte_writer_t writer{};
    benchmark::DoNotOptimize( ByteWriter_Init(
        &writer,
        Span_FromArray( storage ),
        data_byte_order_t::LITTLE ) );

    for ( auto _ : state ) {
        ByteWriter_Reset( &writer );
        for ( usize iValue = 0u; iValue < CY_BYTE_CURSOR_BENCH_VALUE_COUNT; ++iValue ) {
            benchmark::DoNotOptimize( ByteWriter_WriteU32(
                &writer,
                static_cast<u32>( iValue ) ) );
        }
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( storage ) ) );
}

static void BM_ByteWriter_VarU64Sequential( benchmark::State &state )
{
    byte storage[CY_BYTE_CURSOR_BENCH_VALUE_COUNT * 10u]{};
    byte_writer_t writer{};
    benchmark::DoNotOptimize( ByteWriter_Init( &writer, Span_FromArray( storage ) ) );

    for ( auto _ : state ) {
        ByteWriter_Reset( &writer );
        for ( usize iValue = 0u; iValue < CY_BYTE_CURSOR_BENCH_VALUE_COUNT; ++iValue ) {
            const u64 value = static_cast<u64>( iValue ) * 127u;
            benchmark::DoNotOptimize( ByteWriter_WriteVarU64( &writer, value ) );
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( CY_BYTE_CURSOR_BENCH_VALUE_COUNT ) );
}

BENCHMARK( BM_ByteWriter_U32Sequential );
BENCHMARK( BM_ByteWriter_VarU64Sequential );

