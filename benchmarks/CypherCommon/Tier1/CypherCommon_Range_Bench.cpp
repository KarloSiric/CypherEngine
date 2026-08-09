//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Range_Bench.cpp
//  Purpose: Benchmarks overflow-aware half-open range operations.
//  Details: Measures checked containment and intersection used by streams, binary
//           readers, packet buffers, and serialized resource validation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Range.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_IndexRangeContains( benchmark::State &state )
{
    const index_range_t range{ 1024u, 4096u };
    usize iIndex = 0u;

    for ( auto _ : state ) {
        bool_t bContains = IndexRange_Contains( range, iIndex );
        benchmark::DoNotOptimize( bContains );
        iIndex = ( iIndex + 31u ) & 8191u;
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

static void BM_IndexRangeIntersection( benchmark::State &state )
{
    const index_range_t rangeA{ 1024u, 4096u };
    usize iOffset = 0u;

    for ( auto _ : state ) {
        index_range_t intersection =
            IndexRange_Intersection( rangeA, { iOffset, 2048u } );
        benchmark::DoNotOptimize( intersection );
        iOffset = ( iOffset + 17u ) & 4095u;
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_IndexRangeContains );
BENCHMARK( BM_IndexRangeIntersection );
