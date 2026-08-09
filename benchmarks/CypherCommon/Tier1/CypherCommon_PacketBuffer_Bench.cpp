//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_PacketBuffer_Bench.cpp
//  Purpose: Benchmarks packet cursor adaptation and publication.
//  Details: Measures packet writer construction, fixed header emission, commit, and
//           reader reconstruction without socket or allocation overhead.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PacketBuffer.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_PacketBuffer_ByteRoundTrip( benchmark::State &state )
{
    byte storage[1400]{};
    packet_buffer_t packet{};
    benchmark::DoNotOptimize( PacketBuffer_Init( &packet, Span_FromArray( storage ) ) );

    for ( auto _ : state ) {
        byte_writer_t writer = PacketBuffer_ByteWriter( &packet );
        benchmark::DoNotOptimize( ByteWriter_WriteU32( &writer, 0x4359504Bu ) );
        benchmark::DoNotOptimize( ByteWriter_WriteU32( &writer, 42u ) );
        benchmark::DoNotOptimize( ByteWriter_WriteU64( &writer, 0x123456789ABCDEF0ull ) );
        benchmark::DoNotOptimize( PacketBuffer_CommitByteWriter( &packet, writer ) );

        byte_reader_t reader = PacketBuffer_ByteReader( &packet );
        u32 value32 = 0u;
        u64 value64 = 0u;
        benchmark::DoNotOptimize( ByteReader_ReadU32( &reader, &value32 ) );
        benchmark::DoNotOptimize( ByteReader_ReadU32( &reader, &value32 ) );
        benchmark::DoNotOptimize( ByteReader_ReadU64( &reader, &value64 ) );
        benchmark::DoNotOptimize( value32 );
        benchmark::DoNotOptimize( value64 );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * 32 );
}

BENCHMARK( BM_PacketBuffer_ByteRoundTrip );

