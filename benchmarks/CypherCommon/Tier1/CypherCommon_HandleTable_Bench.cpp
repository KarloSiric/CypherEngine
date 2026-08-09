//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_HandleTable_Bench.cpp
//  Purpose: Benchmarks generational handle lookup and slot reuse.
//  Details: Lookup measures validation plus direct slot access; reuse measures the
//           no-allocation remove/emplace path after capacity has been reserved.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_HandleTable.h"

#include <benchmark/benchmark.h>
#include <vector>

using namespace cypher::common;

static void BM_HandleTable_Get( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    handle_table_t<u32> table{};
    if ( !HandleTable_Init( &table, Allocator_GetSystem(), nCount ) ) {
        state.SkipWithError( "HandleTable initialization failed." );
        return;
    }

    std::vector<handle32_t> handles( nCount );
    for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
        handles[iValue] = HandleTable_Emplace(
            &table,
            static_cast<u32>( iValue ) );
    }

    usize iLookup = 0u;
    for ( auto _ : state ) {
        iLookup = ( iLookup + 7919u ) % nCount;
        const u32 *pValue = HandleTable_Get( &table, handles[iLookup] );
        benchmark::DoNotOptimize( pValue );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_HandleTable_Get )->Arg( 4096 )->Arg( 65536 );

static void BM_HandleTable_RemoveEmplace( benchmark::State &state )
{
    handle_table_t<u32> table{};
    if ( !HandleTable_Init( &table, Allocator_GetSystem(), 1u ) ) {
        state.SkipWithError( "HandleTable initialization failed." );
        return;
    }
    handle32_t handle = HandleTable_Emplace( &table, 1u );

    for ( auto _ : state ) {
        bool_t bRemoved = HandleTable_Remove( &table, handle );
        handle = HandleTable_Emplace( &table, 1u );
        benchmark::DoNotOptimize( bRemoved );
        benchmark::DoNotOptimize( handle.value );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_HandleTable_RemoveEmplace );
