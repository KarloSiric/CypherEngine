//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Vector_Bench.cpp
//  Purpose: Benchmarks allocator-backed growable arrays.
//  Details: Separates reserved push throughput from geometric growth cost so
//           capacity-policy overhead remains visible in Release measurements.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Vector.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_VectorPushBackReserved( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    vector_t<u32> vector{};
    bool_t bInitialized =
        Vector_Init( &vector, Allocator_GetSystem(), nCount );
    if ( !bInitialized ) {
        state.SkipWithError( "Vector initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        Vector_Clear( &vector );
        for ( usize iIndex = 0u; iIndex < nCount; ++iIndex ) {
            u32 nValue = static_cast<u32>( iIndex );
            bool_t bPushed = Vector_PushBack( &vector, nValue );
            benchmark::DoNotOptimize( bPushed );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

BENCHMARK( BM_VectorPushBackReserved )
    ->Arg( 16 )
    ->Arg( 256 )
    ->Arg( 4096 );

static void BM_VectorPushBackWithGrowth( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        vector_t<u32> vector{};
        bool_t bInitialized =
            Vector_Init( &vector, Allocator_GetSystem() );
        benchmark::DoNotOptimize( bInitialized );
        for ( usize iIndex = 0u; iIndex < nCount; ++iIndex ) {
            u32 nValue = static_cast<u32>( iIndex );
            bool_t bPushed = Vector_PushBack( &vector, nValue );
            benchmark::DoNotOptimize( bPushed );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

BENCHMARK( BM_VectorPushBackWithGrowth )
    ->Arg( 16 )
    ->Arg( 256 )
    ->Arg( 4096 );
