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
        benchmark::DoNotOptimize( &slot );
        Cy_TLSDestroySlot( slot );
    }
}

void BM_TLSSetGetClearValue( benchmark::State &state )
{
    const tls_slot_t slot = Cy_TLSCreateSlot();
    i32 nValue = 7;

    for ( auto _ : state ) {
        Cy_TLSSetValue( slot, &nValue );
        benchmark::DoNotOptimize( Cy_TLSGetValue( slot ) );
        Cy_TLSClearValue( slot );
    }

    Cy_TLSDestroySlot( slot );
}

void BM_TLSGetValue( benchmark::State &state )
{
    const tls_slot_t slot = Cy_TLSCreateSlot();
    i32 nValue = 7;
    Cy_TLSSetValue( slot, &nValue );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_TLSGetValue( slot ) );
    }

    Cy_TLSDestroySlot( slot );
}

} // namespace

BENCHMARK( BM_TLSCreateDestroySlot );
BENCHMARK( BM_TLSSetGetClearValue );
BENCHMARK( BM_TLSGetValue );
