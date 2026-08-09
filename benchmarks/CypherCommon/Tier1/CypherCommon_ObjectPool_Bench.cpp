//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_ObjectPool_Bench.cpp
//  Purpose: Benchmarks typed fixed-pool object lifecycle operations.
//  Details: Measures the constant-time hot create/destroy path independently
//           from one-time pool allocation and complete reset traversal.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ObjectPool.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

struct pool_benchmark_value_t {
    u64 words[4]{};

    explicit pool_benchmark_value_t( u64 nSeed ) noexcept
        : words{ nSeed, nSeed + 1u, nSeed + 2u, nSeed + 3u }
    {
    }
};

} // namespace

static void BM_ObjectPoolCreateDestroy( benchmark::State &state )
{
    object_pool_t<pool_benchmark_value_t> pool{};
    bool_t bInitialized =
        ObjectPool_Init( &pool, Allocator_GetSystem(), 1024u );
    if ( !bInitialized ) {
        state.SkipWithError( "ObjectPool initialization failed." );
        return;
    }

    u64 nSeed = 1u;
    for ( auto _ : state ) {
        pool_benchmark_value_t *pValue =
            ObjectPool_Create( &pool, nSeed );
        benchmark::DoNotOptimize( pValue );
        benchmark::DoNotOptimize( pValue->words[3] );
        bool_t bDestroyed = ObjectPool_Destroy( &pool, pValue );
        benchmark::DoNotOptimize( bDestroyed );
        ++nSeed;
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_ObjectPoolCreateDestroy );

static void BM_ObjectPoolFillReset( benchmark::State &state )
{
    constexpr usize nCapacity = 256u;
    object_pool_t<pool_benchmark_value_t> pool{};
    bool_t bInitialized =
        ObjectPool_Init( &pool, Allocator_GetSystem(), nCapacity );
    if ( !bInitialized ) {
        state.SkipWithError( "ObjectPool initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        for ( usize iObject = 0u; iObject < nCapacity; ++iObject ) {
            pool_benchmark_value_t *pValue =
                ObjectPool_Create( &pool, iObject );
            benchmark::DoNotOptimize( pValue );
        }
        ObjectPool_Reset( &pool );
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCapacity ) );
}

BENCHMARK( BM_ObjectPoolFillReset );
