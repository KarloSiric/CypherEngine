//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Semaphore_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 semaphore primitive costs.
//  Details: These measurements track uncontended counting operations used by
//           future work queues and async request queues.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Semaphore.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_SemaphorePostTryWait( benchmark::State &state )
{
    cy_semaphore_t semaphore{};
    if ( !Cy_SemaphoreInit( &semaphore, 0u, CY_U32_MAX ) ) {
        state.SkipWithError( "semaphore initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SemaphorePost( &semaphore ) );
        benchmark::DoNotOptimize( Cy_SemaphoreTryWait( &semaphore ) );
    }

    if ( !Cy_SemaphoreShutdown( &semaphore ) ) {
        state.SkipWithError( "semaphore shutdown failed" );
    }
}

void BM_SemaphoreTryWaitAvailable( benchmark::State &state )
{
    cy_semaphore_t semaphore{};
    if ( !Cy_SemaphoreInit( &semaphore, 1u, CY_U32_MAX ) ) {
        state.SkipWithError( "semaphore initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SemaphoreTryWait( &semaphore ) );
        benchmark::DoNotOptimize( Cy_SemaphorePost( &semaphore ) );
    }

    if ( !Cy_SemaphoreShutdown( &semaphore ) ) {
        state.SkipWithError( "semaphore shutdown failed" );
    }
}

void BM_SemaphoreGetCount( benchmark::State &state )
{
    cy_semaphore_t semaphore{};
    if ( !Cy_SemaphoreInit( &semaphore, 8u, CY_U32_MAX ) ) {
        state.SkipWithError( "semaphore initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SemaphoreGetCount( &semaphore ) );
    }

    if ( !Cy_SemaphoreShutdown( &semaphore ) ) {
        state.SkipWithError( "semaphore shutdown failed" );
    }
}

} // namespace

BENCHMARK( BM_SemaphorePostTryWait );
BENCHMARK( BM_SemaphoreTryWaitAvailable );
BENCHMARK( BM_SemaphoreGetCount );
