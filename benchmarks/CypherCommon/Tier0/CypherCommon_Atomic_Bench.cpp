//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Atomic_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 atomic primitive costs.
//  Details: These measurements track basic atomic operations used by counters,
//           flags, indices, and state transitions. Compare results by platform
//           and build mode rather than treating one run as absolute truth.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Atomic.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_AtomicLoadRelaxed( benchmark::State &state )
{
    atomic_u64_t nValue{ 123u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_AtomicLoad( &nValue, CY_MEMORY_ORDER_RELAXED ) );
    }
}

void BM_AtomicStoreRelaxed( benchmark::State &state )
{
    atomic_u64_t nValue{ 0u };
    u64 nCounter = 0u;

    for ( auto _ : state ) {
        Cy_AtomicStore( &nValue, ++nCounter, CY_MEMORY_ORDER_RELAXED );
        benchmark::ClobberMemory();
    }
}

void BM_AtomicFetchAddRelaxed( benchmark::State &state )
{
    atomic_u64_t nValue{ 0u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_AtomicFetchAdd( &nValue, static_cast<u64>( 1u ), CY_MEMORY_ORDER_RELAXED ) );
    }
}

void BM_AtomicExchangeSeqCst( benchmark::State &state )
{
    atomic_u64_t nValue{ 0u };
    u64 nCounter = 0u;

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_AtomicExchange( &nValue, ++nCounter, CY_MEMORY_ORDER_SEQ_CST ) );
    }
}

void BM_AtomicCompareExchangeSeqCst( benchmark::State &state )
{
    atomic_u32_t nValue{ 0u };

    for ( auto _ : state ) {
        u32 nExpected = Cy_AtomicLoad( &nValue, CY_MEMORY_ORDER_RELAXED );
        const u32 nDesired = nExpected + 1u;
        benchmark::DoNotOptimize( Cy_AtomicCompareExchange( &nValue, &nExpected, nDesired ) );
    }
}

void BM_AtomicFenceAcquireRelease( benchmark::State &state )
{
    for ( auto _ : state ) {
        Cy_AtomicFenceAcquire();
        Cy_AtomicFenceRelease();
    }
}

void BM_AtomicFenceSeqCst( benchmark::State &state )
{
    for ( auto _ : state ) {
        Cy_AtomicFenceSeqCst();
    }
}

} // namespace

BENCHMARK( BM_AtomicLoadRelaxed );
BENCHMARK( BM_AtomicStoreRelaxed );
BENCHMARK( BM_AtomicFetchAddRelaxed );
BENCHMARK( BM_AtomicExchangeSeqCst );
BENCHMARK( BM_AtomicCompareExchangeSeqCst );
BENCHMARK( BM_AtomicFenceAcquireRelease );
BENCHMARK( BM_AtomicFenceSeqCst );
