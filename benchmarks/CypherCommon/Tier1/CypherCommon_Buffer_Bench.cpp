//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Buffer_Bench.cpp
//  Purpose: Benchmarks bounded writes into caller-owned byte storage.
//  Details: Measures validation and logical-size tracking around representative
//           append-copy and zero-fill workloads.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Buffer.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_BufferAppend( benchmark::State &state )
{
    alignas( 64 ) byte storage[65536]{};
    alignas( 64 ) byte source[4096]{};
    const usize cbWrite = static_cast<usize>( state.range( 0 ) );
    buffer_t buffer{};
    if ( !Buffer_Init( &buffer, { storage, sizeof( storage ) } ) ) {
        state.SkipWithError( "Buffer initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        Buffer_Clear( &buffer );
        if ( !Buffer_Append( &buffer, source, cbWrite ) ) {
            state.SkipWithError( "Buffer append failed." );
            return;
        }
        benchmark::DoNotOptimize( storage );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbWrite ) );
}

static void BM_BufferAppendZero( benchmark::State &state )
{
    alignas( 64 ) byte storage[65536]{};
    const usize cbWrite = static_cast<usize>( state.range( 0 ) );
    buffer_t buffer{};
    if ( !Buffer_Init( &buffer, { storage, sizeof( storage ) } ) ) {
        state.SkipWithError( "Buffer initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        Buffer_Clear( &buffer );
        if ( !Buffer_AppendZero( &buffer, cbWrite ) ) {
            state.SkipWithError( "Buffer zero append failed." );
            return;
        }
        benchmark::DoNotOptimize( storage );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbWrite ) );
}

BENCHMARK( BM_BufferAppend )->Arg( 64 )->Arg( 256 )->Arg( 4096 );
BENCHMARK( BM_BufferAppendZero )->Arg( 64 )->Arg( 256 )->Arg( 4096 );
