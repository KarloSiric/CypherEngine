//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Array_Bench.cpp
//  Purpose: Benchmarks allocator-backed exact-size arrays.
//  Details: Measures lifecycle allocation and transactional resizing across
//           representative small, medium, and page-sized element counts.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Array.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_ArrayInitShutdown( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        array_t<u32> array{};
        bool_t bInitialized =
            Array_Init( &array, Allocator_GetSystem(), nCount );
        benchmark::DoNotOptimize( bInitialized );
        benchmark::DoNotOptimize( array.pData );
        benchmark::ClobberMemory();
        Array_Shutdown( &array );
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

BENCHMARK( BM_ArrayInitShutdown )
    ->Arg( 16 )
    ->Arg( 256 )
    ->Arg( 4096 );

static void BM_ArrayResize( benchmark::State &state )
{
    array_t<u32> array{};
    bool_t bInitialized =
        Array_Init( &array, Allocator_GetSystem(), 64u );
    if ( !bInitialized ) {
        state.SkipWithError( "Array initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        bool_t bGrew = Array_Resize( &array, 256u );
        benchmark::DoNotOptimize( bGrew );
        benchmark::ClobberMemory();
        bool_t bShrank = Array_Resize( &array, 64u );
        benchmark::DoNotOptimize( bShrank );
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) * 2 );
}

BENCHMARK( BM_ArrayResize );
