//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/ToolFramework/CypherCommon_ProgressBar_Bench.cpp
//  Purpose: Benchmarks tool-framework progress-state transitions.
//  Details: Measures state mutation only. Terminal rendering and output flushing
//           belong to end-to-end tool benchmarks rather than this small helper.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ProgressBar.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_ProgressBar_StateCycle( benchmark::State &state )
{
    progress_bar_t progress{};
    u64 nCompleted = 0u;
    for ( auto _ : state ) {
        ProgressBar_Begin( &progress, "Cook resources", 1000u );
        ProgressBar_Update( &progress, nCompleted % 1001u );
        ProgressBar_End( &progress );
        benchmark::DoNotOptimize( progress );
        ++nCompleted;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

} // namespace

BENCHMARK( BM_ProgressBar_StateCycle );
