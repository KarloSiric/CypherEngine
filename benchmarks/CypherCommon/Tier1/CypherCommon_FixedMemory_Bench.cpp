//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_FixedMemory_Bench.cpp
//  Purpose: Benchmarks bounded non-owning memory-region operations.
//  Details: Measures address containment and bounded slicing used by allocators,
//           packet storage, binary readers, and asset-processing buffers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_FixedMemory.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_FixedMemoryContainsRange( benchmark::State &state )
{
    alignas( 64 ) byte storage[4096]{};
    const fixed_memory_t memory = FixedMemory_Make( storage, sizeof( storage ) );
    usize iOffset = 0u;

    for ( auto _ : state ) {
        bool_t bContained =
            FixedMemory_ContainsRange( memory, storage + iOffset, 32u );
        benchmark::DoNotOptimize( bContained );
        iOffset = ( iOffset + 31u ) & 2047u;
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

static void BM_FixedMemorySubspan( benchmark::State &state )
{
    alignas( 64 ) byte storage[4096]{};
    const fixed_memory_t memory = FixedMemory_Make( storage, sizeof( storage ) );
    usize iOffset = 0u;

    for ( auto _ : state ) {
        byte_span_t span = FixedMemory_Subspan( memory, iOffset, 128u );
        benchmark::DoNotOptimize( span );
        iOffset = ( iOffset + 17u ) & 2047u;
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_FixedMemoryContainsRange );
BENCHMARK( BM_FixedMemorySubspan );
