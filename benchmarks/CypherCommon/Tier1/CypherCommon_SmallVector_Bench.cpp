//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_SmallVector_Bench.cpp
//  Purpose: Benchmarks inline and spilled small-vector operation.
//  Details: Separates the allocation-free inline path from heap spill so the
//           intended optimization remains measurable in Release builds.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SmallVector.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_SmallVectorInlinePush( benchmark::State &state )
{
    small_vector_t<u32, 16u> vector{};
    bool_t bInitialized =
        SmallVector_Init( &vector, Allocator_GetSystem() );
    if ( !bInitialized ) {
        state.SkipWithError( "SmallVector initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        SmallVector_Clear( &vector );
        for ( u32 nValue = 0u; nValue < 16u; ++nValue ) {
            bool_t bPushed = SmallVector_PushBack( &vector, nValue );
            benchmark::DoNotOptimize( bPushed );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) * 16 );
}

BENCHMARK( BM_SmallVectorInlinePush );

static void BM_SmallVectorPushWithSpill( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    for ( auto _ : state ) {
        small_vector_t<u32, 16u> vector{};
        bool_t bInitialized =
            SmallVector_Init( &vector, Allocator_GetSystem() );
        benchmark::DoNotOptimize( bInitialized );
        for ( usize iIndex = 0u; iIndex < nCount; ++iIndex ) {
            u32 nValue = static_cast<u32>( iIndex );
            bool_t bPushed = SmallVector_PushBack( &vector, nValue );
            benchmark::DoNotOptimize( bPushed );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

BENCHMARK( BM_SmallVectorPushWithSpill )
    ->Arg( 32 )
    ->Arg( 256 );
