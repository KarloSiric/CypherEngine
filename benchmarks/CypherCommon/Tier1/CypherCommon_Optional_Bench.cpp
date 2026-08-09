//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Optional_Bench.cpp
//  Purpose: Benchmarks allocation-free optional-value lifecycle operations.
//  Details: Measures repeated in-place construction, access, and reset for a
//           representative small runtime record without heap allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Optional.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

struct optional_benchmark_value_t {
    u64 words[4]{};

    explicit optional_benchmark_value_t( u64 nSeed ) noexcept
        : words{ nSeed, nSeed + 1u, nSeed + 2u, nSeed + 3u }
    {
    }
};

} // namespace

static void BM_OptionalEmplaceAccessReset( benchmark::State &state )
{
    optional_t<optional_benchmark_value_t> optional{};
    u64 nSeed = 1u;

    for ( auto _ : state ) {
        optional_benchmark_value_t *pValue =
            Optional_EmplaceArgs( &optional, nSeed );
        benchmark::DoNotOptimize( pValue );
        benchmark::DoNotOptimize( pValue->words[3] );
        benchmark::ClobberMemory();
        Optional_Reset( &optional );
        ++nSeed;
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_OptionalEmplaceAccessReset );
