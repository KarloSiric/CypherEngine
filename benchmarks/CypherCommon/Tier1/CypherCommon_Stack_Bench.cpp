//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Stack_Bench.cpp
//  Purpose: Benchmarks vector-backed stack operations.
//  Details: Uses reserved storage to isolate LIFO policy overhead from allocation
//           and vector growth, which are benchmarked independently.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Stack.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_StackReservedPushPop( benchmark::State &state )
{
    cy_stack_t<u32> stack{};
    if ( !Stack_Init( &stack, Allocator_GetSystem(), 256u ) ) {
        state.SkipWithError( "Stack initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        for ( u32 nValue = 0u; nValue < 256u; ++nValue ) {
            bool_t bPushed = Stack_Push( &stack, nValue );
            benchmark::DoNotOptimize( bPushed );
        }
        for ( usize iValue = 0u; iValue < 256u; ++iValue ) {
            bool_t bPopped = Stack_Pop( &stack );
            benchmark::DoNotOptimize( bPopped );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) * 512 );
}

BENCHMARK( BM_StackReservedPushPop );
