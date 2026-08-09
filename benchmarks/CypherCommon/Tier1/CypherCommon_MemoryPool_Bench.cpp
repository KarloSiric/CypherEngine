//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_MemoryPool_Bench.cpp
//  Purpose: Benchmarks allocator-owned fixed-block pool operations.
//  Details: Measures the owning wrapper's allocation/free dispatch after backing
//           storage has been created outside the timed loop.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryPool.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_MemoryPoolAllocateFree( benchmark::State &state )
{
    memory_pool_t pool{};
    if ( !MemoryPool_Init(
             &pool,
             Allocator_GetSystem(),
             32u,
             16u,
             1024u ) ) {
        state.SkipWithError( "MemoryPool initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        void *pBlock = MemoryPool_Allocate( &pool );
        benchmark::DoNotOptimize( pBlock );
        benchmark::ClobberMemory();
        if ( !MemoryPool_Free( &pool, pBlock ) ) {
            state.SkipWithError( "MemoryPool release failed." );
            MemoryPool_Shutdown( &pool );
            return;
        }
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    MemoryPool_Shutdown( &pool );
}

BENCHMARK( BM_MemoryPoolAllocateFree );
