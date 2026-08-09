//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_ScratchBuffer_Bench.cpp
//  Purpose: Benchmarks local-first scratch-buffer acquisition and release.
//  Details: Separates the allocation-free local path from explicit system-allocator
//           fallback so temporary-workspace policy costs remain visible.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ScratchBuffer.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_ScratchBufferLocalAcquireRelease( benchmark::State &state )
{
    alignas( 64 ) byte local[4096]{};
    scratch_buffer_t buffer{};

    for ( auto _ : state ) {
        if ( !ScratchBuffer_Acquire(
                 &buffer,
                 { local, sizeof( local ) },
                 Allocator_GetSystem(),
                 256u,
                 64u ) ) {
            state.SkipWithError( "Local scratch acquisition failed." );
            return;
        }
        benchmark::DoNotOptimize( buffer.pData );
        ScratchBuffer_Release( &buffer );
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

static void BM_ScratchBufferFallbackAcquireRelease( benchmark::State &state )
{
    byte local[16]{};
    scratch_buffer_t buffer{};

    for ( auto _ : state ) {
        if ( !ScratchBuffer_Acquire(
                 &buffer,
                 { local, sizeof( local ) },
                 Allocator_GetSystem(),
                 4096u,
                 64u ) ) {
            state.SkipWithError( "Fallback scratch acquisition failed." );
            return;
        }
        benchmark::DoNotOptimize( buffer.pData );
        benchmark::ClobberMemory();
        ScratchBuffer_Release( &buffer );
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_ScratchBufferLocalAcquireRelease );
BENCHMARK( BM_ScratchBufferFallbackAcquireRelease );
