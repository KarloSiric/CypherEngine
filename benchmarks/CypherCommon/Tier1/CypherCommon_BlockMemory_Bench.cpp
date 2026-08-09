//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_BlockMemory_Bench.cpp
//  Purpose: Benchmarks constant-time fixed-block allocation and release.
//  Details: Measures free-list and occupancy-bit overhead for representative
//           small engine objects without including backing-storage allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BlockMemory.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_BlockMemoryAllocateFree( benchmark::State &state )
{
    alignas( 64 ) byte storage[65536]{};
    block_memory_t memory{};
    if ( !BlockMemory_Init(
             &memory,
             { storage, sizeof( storage ) },
             32u,
             16u,
             1024u ) ) {
        state.SkipWithError( "BlockMemory initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        void *pBlock = BlockMemory_Allocate( &memory );
        benchmark::DoNotOptimize( pBlock );
        benchmark::ClobberMemory();
        if ( !BlockMemory_Free( &memory, pBlock ) ) {
            state.SkipWithError( "BlockMemory release failed." );
            return;
        }
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

static void BM_BlockMemoryBurst( benchmark::State &state )
{
    alignas( 64 ) byte storage[65536]{};
    block_memory_t memory{};
    void *blocks[128]{};
    if ( !BlockMemory_Init(
             &memory,
             { storage, sizeof( storage ) },
             32u,
             16u,
             1024u ) ) {
        state.SkipWithError( "BlockMemory initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        for ( void *&pBlock : blocks ) {
            pBlock = BlockMemory_Allocate( &memory );
            benchmark::DoNotOptimize( pBlock );
        }
        benchmark::ClobberMemory();
        for ( void *pBlock : blocks ) {
            if ( !BlockMemory_Free( &memory, pBlock ) ) {
                state.SkipWithError( "BlockMemory burst release failed." );
                return;
            }
        }
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) * 128 );
}

BENCHMARK( BM_BlockMemoryAllocateFree );
BENCHMARK( BM_BlockMemoryBurst );
