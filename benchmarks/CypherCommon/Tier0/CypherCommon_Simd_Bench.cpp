//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Simd_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 SIMD capability policy queries.
//  Details: These benchmarks measure cached SIMD capability lookups and policy
//           helpers. Actual SIMD math and memory operation benchmarks belong to
//           Mathlib, renderer, physics, and memory-specific benchmark files later.
//
//  History:
//  - Created by Karlo Siric on 2026-07-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Simd.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_SimdGetCapsCached( benchmark::State &state )
{
    Cy_SimdInit();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SimdGetCaps() );
    }
}

void BM_SimdCanUse( benchmark::State &state )
{
    Cy_SimdInit();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SimdCanUse( CY_SIMD_FEATURE_SSE2 ) );
        benchmark::DoNotOptimize( Cy_SimdCanUse( CY_SIMD_FEATURE_AVX2 ) );
        benchmark::DoNotOptimize( Cy_SimdCanUse( CY_SIMD_FEATURE_NEON ) );
    }
}

void BM_SimdBestLevelAndWidth( benchmark::State &state )
{
    Cy_SimdInit();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SimdGetBestLevel() );
        benchmark::DoNotOptimize( Cy_SimdGetVectorRegisterBytes() );
    }
}

void BM_SimdNames( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SimdFeatureName( CY_SIMD_FEATURE_SSE2 ) );
        benchmark::DoNotOptimize( Cy_SimdFeatureName( CY_SIMD_FEATURE_AVX2 ) );
        benchmark::DoNotOptimize( Cy_SimdLevelName( CY_SIMD_LEVEL_NEON ) );
    }
}

} // namespace

BENCHMARK( BM_SimdGetCapsCached );
BENCHMARK( BM_SimdCanUse );
BENCHMARK( BM_SimdBestLevelAndWidth );
BENCHMARK( BM_SimdNames );
