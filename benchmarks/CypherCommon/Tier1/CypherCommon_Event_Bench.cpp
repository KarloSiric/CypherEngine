//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Event_Bench.cpp
//  Purpose: Benchmarks synchronous event fan-out.
//  Details: Reused dispatch storage keeps steady-state emission allocation-free while
//           measuring snapshot copy, handle validation, and callback dispatch.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Event.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void CountEvent(
    event_id_t,
    const event_payload_t &,
    void *pUserData ) noexcept
{
    ++*static_cast<u64 *>( pUserData );
}

} // namespace

static void BM_EventBus_Emit( benchmark::State &state )
{
    const usize nSubscriptions = static_cast<usize>( state.range( 0 ) );
    event_bus_t *pBus = EventBus_Create(
        { Allocator_GetSystem(), nSubscriptions } );
    if ( pBus == nullptr ) {
        state.SkipWithError( "EventBus creation failed." );
        return;
    }

    u64 cCallbacks = 0u;
    for ( usize iSubscription = 0u;
          iSubscription < nSubscriptions;
          ++iSubscription ) {
        const event_subscription_t subscription = EventBus_Subscribe(
            pBus,
            1u,
            static_cast<i32>( iSubscription & 7u ),
            EVENT_SUBSCRIPTION_FLAG_NONE,
            CountEvent,
            &cCallbacks );
        if ( !Cy_Handle32IsValid( subscription ) ) {
            state.SkipWithError( "EventBus subscription failed." );
            EventBus_Destroy( pBus );
            return;
        }
    }
    static_cast<void>( EventBus_Emit( pBus, 1u, {} ) );
    cCallbacks = 0u;

    for ( auto _ : state ) {
        usize nInvoked = EventBus_Emit( pBus, 1u, {} );
        benchmark::DoNotOptimize( nInvoked );
        benchmark::ClobberMemory();
    }
    benchmark::DoNotOptimize( cCallbacks );
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nSubscriptions ) );
    EventBus_Destroy( pBus );
}

BENCHMARK( BM_EventBus_Emit )->Arg( 1 )->Arg( 16 )->Arg( 256 );
