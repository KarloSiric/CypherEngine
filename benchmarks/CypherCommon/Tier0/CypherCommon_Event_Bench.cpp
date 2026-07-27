//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Event_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 event primitive costs.
//  Details: These measurements track signal/reset and already-signaled wait
//           overhead for future worker wakeups and async completion signals.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Event.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_EventSignalResetManual( benchmark::State &state )
{
    cy_event_t event{};
    if ( !Cy_EventInit( &event, cy_event_reset_mode_t::Manual, CY_FALSE ) ) {
        state.SkipWithError( "event initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_EventSignal( &event ) );
        benchmark::DoNotOptimize( Cy_EventReset( &event ) );
    }

    if ( !Cy_EventShutdown( &event ) ) {
        state.SkipWithError( "event shutdown failed" );
    }
}

void BM_EventWaitAlreadySignaledManual( benchmark::State &state )
{
    cy_event_t event{};
    if ( !Cy_EventInit( &event, cy_event_reset_mode_t::Manual, CY_TRUE ) ) {
        state.SkipWithError( "event initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_EventWaitTimeoutMs( &event, 0u ) );
    }

    if ( !Cy_EventShutdown( &event ) ) {
        state.SkipWithError( "event shutdown failed" );
    }
}

void BM_EventWaitAlreadySignaledAuto( benchmark::State &state )
{
    cy_event_t event{};
    if ( !Cy_EventInit( &event, cy_event_reset_mode_t::Auto, CY_FALSE ) ) {
        state.SkipWithError( "event initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_EventSignal( &event ) );
        benchmark::DoNotOptimize( Cy_EventWaitTimeoutMs( &event, 0u ) );
    }

    if ( !Cy_EventShutdown( &event ) ) {
        state.SkipWithError( "event shutdown failed" );
    }
}

} // namespace

BENCHMARK( BM_EventSignalResetManual );
BENCHMARK( BM_EventWaitAlreadySignaledManual );
BENCHMARK( BM_EventWaitAlreadySignaledAuto );
