//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_CallQueue_Bench.cpp
//  Purpose: Benchmarks deferred callback queue dispatch.
//  Details: Pre-reserved push and drain cycles measure queue bookkeeping and indirect
//           callback dispatch without mixing allocation growth into steady state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CallQueue.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void EmptyCall( void *pUserData ) noexcept
{
    benchmark::DoNotOptimize( pUserData );
}

} // namespace

static void BM_CallQueue_PushDrain( benchmark::State &state )
{
    const usize nCalls = static_cast<usize>( state.range( 0 ) );
    call_queue_t *pQueue = CallQueue_Create( Allocator_GetSystem(), nCalls );
    if ( pQueue == nullptr ) {
        state.SkipWithError( "CallQueue creation failed." );
        return;
    }
    const call_queue_entry_t entry{ EmptyCall, nullptr, 0u };

    for ( auto _ : state ) {
        for ( usize iCall = 0u; iCall < nCalls; ++iCall ) {
            benchmark::DoNotOptimize( CallQueue_Push( pQueue, entry ) );
        }
        benchmark::DoNotOptimize( CallQueue_Drain( pQueue ) );
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCalls ) );
    CallQueue_Destroy( pQueue );
}

BENCHMARK( BM_CallQueue_PushDrain )->Arg( 16 )->Arg( 256 )->Arg( 4096 );
