//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_MemoryStack_Bench.cpp
//  Purpose: Benchmarks aligned linear stack allocation and rewind.
//  Details: Measures the allocation/marker path used by frame scratch data,
//           parser workspaces, command generation, and temporary tool operations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryStack.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_MemoryStackAllocateRestore( benchmark::State &state )
{
    alignas( 64 ) byte storage[65536]{};
    memory_stack_t stack{};
    if ( !MemoryStack_Init( &stack, { storage, sizeof( storage ) } ) ) {
        state.SkipWithError( "MemoryStack initialization failed." );
        return;
    }
    const usize cbSize = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        void *pMemory = MemoryStack_Allocate( &stack, cbSize, 16u );
        benchmark::DoNotOptimize( pMemory );
        benchmark::ClobberMemory();
        if ( !MemoryStack_Restore( &stack, 0u ) ) {
            state.SkipWithError( "MemoryStack restore failed." );
            return;
        }
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

static void BM_MemoryStackBurstReset( benchmark::State &state )
{
    alignas( 64 ) byte storage[65536]{};
    memory_stack_t stack{};
    if ( !MemoryStack_Init( &stack, { storage, sizeof( storage ) } ) ) {
        state.SkipWithError( "MemoryStack initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        for ( usize iAllocation = 0u; iAllocation < 128u; ++iAllocation ) {
            void *pMemory = MemoryStack_Allocate( &stack, 32u, 16u );
            benchmark::DoNotOptimize( pMemory );
        }
        benchmark::ClobberMemory();
        MemoryStack_Reset( &stack );
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) * 128 );
}

BENCHMARK( BM_MemoryStackAllocateRestore )->Arg( 16 )->Arg( 64 )->Arg( 256 );
BENCHMARK( BM_MemoryStackBurstReset );
