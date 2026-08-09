//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Queue_Bench.cpp
//  Purpose: Benchmarks circular FIFO queue operation.
//  Details: Uses reserved storage to measure wrapped push/pop throughput without
//           mixing allocator cost into the steady-state queue path.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Queue.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_QueueReservedPushPop( benchmark::State &state )
{
    queue_t<u32> queue{};
    if ( !Queue_Init( &queue, Allocator_GetSystem(), 256u ) ) {
        state.SkipWithError( "Queue initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        for ( u32 nValue = 0u; nValue < 256u; ++nValue ) {
            bool_t bPushed = Queue_Push( &queue, nValue );
            benchmark::DoNotOptimize( bPushed );
        }
        for ( usize iValue = 0u; iValue < 256u; ++iValue ) {
            bool_t bPopped = Queue_Pop( &queue );
            benchmark::DoNotOptimize( bPopped );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) * 512 );
}

BENCHMARK( BM_QueueReservedPushPop );
