//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_CPUDetect_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 CPU detection query paths.
//  Details: CPU detection itself is cached. These benchmarks measure the hot query
//           helpers that SIMD, math, renderer, and diagnostics will call.
//
//  History:
//  - Created by Karlo Siric on 2026-07-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CPUDetect.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_CPUDetectGetInfoCached( benchmark::State &state )
{
    if ( !Cy_CPUDetectInit() ) {
        state.SkipWithError( "CPU detection initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_CPUDetectGetInfo() );
    }
}

void BM_CPUDetectHasFeature( benchmark::State &state )
{
    const cy_cpu_detect_info_t *pInfo = Cy_CPUDetectGetInfo();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_CPUDetectHasFeature( pInfo->usableFeatures, CY_CPU_FEATURE_NEON ) );
        benchmark::DoNotOptimize( Cy_CPUDetectHasFeature( pInfo->usableFeatures, CY_CPU_FEATURE_AVX2 ) );
    }
}

void BM_CPUDetectFeatureName( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_CPUDetectFeatureName( CY_CPU_FEATURE_SSE2 ) );
        benchmark::DoNotOptimize( Cy_CPUDetectFeatureName( CY_CPU_FEATURE_AVX2 ) );
        benchmark::DoNotOptimize( Cy_CPUDetectFeatureName( CY_CPU_FEATURE_NEON ) );
    }
}

} // namespace

BENCHMARK( BM_CPUDetectGetInfoCached );
BENCHMARK( BM_CPUDetectHasFeature );
BENCHMARK( BM_CPUDetectFeatureName );
