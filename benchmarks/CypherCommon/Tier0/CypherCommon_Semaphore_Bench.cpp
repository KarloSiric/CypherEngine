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
    Cy_SemaphoreInit( &semaphore, 0u, CY_U32_MAX );

    for ( auto _ : state ) {
        Cy_SemaphorePost( &semaphore );
        benchmark::DoNotOptimize( Cy_SemaphoreTryWait( &semaphore ) );
    }
}

void BM_SemaphoreTryWaitAvailable( benchmark::State &state )
{
    cy_semaphore_t semaphore{};
    Cy_SemaphoreInit( &semaphore, 1u, CY_U32_MAX );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SemaphoreTryWait( &semaphore ) );
        Cy_SemaphorePost( &semaphore );
    }
}

void BM_SemaphoreGetCount( benchmark::State &state )
{
    cy_semaphore_t semaphore{};
    Cy_SemaphoreInit( &semaphore, 8u, CY_U32_MAX );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SemaphoreGetCount( &semaphore ) );
    }
}

} // namespace

BENCHMARK( BM_SemaphorePostTryWait );
BENCHMARK( BM_SemaphoreTryWaitAvailable );
BENCHMARK( BM_SemaphoreGetCount );
