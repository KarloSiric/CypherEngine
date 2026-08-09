//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_RingBuffer_Bench.cpp
//  Purpose: Benchmarks fixed circular-buffer operations.
//  Details: Measures allocation-free push/pop and full-buffer overwrite paths used
//           by bounded telemetry, diagnostics, and recent-event histories.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RingBuffer.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_RingBufferPushPop( benchmark::State &state )
{
    u32 storage[256]{};
    ring_buffer_t<u32> buffer{};
    bool_t bInitialized = RingBuffer_Init( &buffer, { storage, 256u } );
    if ( !bInitialized ) {
        state.SkipWithError( "RingBuffer initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        for ( u32 nValue = 0u; nValue < 256u; ++nValue ) {
            bool_t bPushed = RingBuffer_Push( &buffer, nValue );
            benchmark::DoNotOptimize( bPushed );
        }
        for ( usize iValue = 0u; iValue < 256u; ++iValue ) {
            bool_t bPopped = RingBuffer_Pop( &buffer );
            benchmark::DoNotOptimize( bPopped );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) * 512 );
}

BENCHMARK( BM_RingBufferPushPop );

static void BM_RingBufferOverwrite( benchmark::State &state )
{
    u32 storage[256]{};
    ring_buffer_t<u32> buffer{};
    bool_t bInitialized = RingBuffer_Init( &buffer, { storage, 256u } );
    if ( !bInitialized ) {
        state.SkipWithError( "RingBuffer initialization failed." );
        return;
    }
    for ( u32 nValue = 0u; nValue < 256u; ++nValue ) {
        bool_t bPushed = RingBuffer_Push( &buffer, nValue );
        benchmark::DoNotOptimize( bPushed );
    }

    u32 nValue = 0u;
    for ( auto _ : state ) {
        bool_t bPushed = RingBuffer_PushOverwrite( &buffer, nValue++ );
        benchmark::DoNotOptimize( bPushed );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_RingBufferOverwrite );
