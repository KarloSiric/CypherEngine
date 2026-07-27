//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_TLS_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 TLS slot costs.
//  Details: These measurements track per-thread value lookup and slot lifecycle
//           overhead used by scratch memory, profiler state, and worker context.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TLS.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_TLSCreateDestroySlot( benchmark::State &state )
{
    for ( auto _ : state ) {
        const tls_slot_t slot = Cy_TLSCreateSlot();
        if ( slot == CY_TLS_INVALID_SLOT ) {
            state.SkipWithError( "TLS slot creation failed" );
            break;
        }
        benchmark::DoNotOptimize( &slot );
        benchmark::DoNotOptimize( Cy_TLSDestroySlot( slot ) );
    }
}

void BM_TLSSetGetClearValue( benchmark::State &state )
{
    const tls_slot_t slot = Cy_TLSCreateSlot();
    if ( slot == CY_TLS_INVALID_SLOT ) {
        state.SkipWithError( "TLS slot creation failed" );
        return;
    }
    i32 nValue = 7;

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_TLSSetValue( slot, &nValue ) );
        benchmark::DoNotOptimize( Cy_TLSGetValue( slot ) );
        benchmark::DoNotOptimize( Cy_TLSClearValue( slot ) );
    }

    if ( !Cy_TLSDestroySlot( slot ) ) {
        state.SkipWithError( "TLS slot destruction failed" );
    }
}

void BM_TLSGetValue( benchmark::State &state )
{
    const tls_slot_t slot = Cy_TLSCreateSlot();
    if ( slot == CY_TLS_INVALID_SLOT ) {
        state.SkipWithError( "TLS slot creation failed" );
        return;
    }
    i32 nValue = 7;
    if ( !Cy_TLSSetValue( slot, &nValue ) ) {
        state.SkipWithError( "TLS value setup failed" );
        benchmark::DoNotOptimize( Cy_TLSDestroySlot( slot ) );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_TLSGetValue( slot ) );
    }

    if ( !Cy_TLSDestroySlot( slot ) ) {
        state.SkipWithError( "TLS slot destruction failed" );
    }
}

} // namespace

BENCHMARK( BM_TLSCreateDestroySlot );
BENCHMARK( BM_TLSSetGetClearValue );
BENCHMARK( BM_TLSGetValue );
