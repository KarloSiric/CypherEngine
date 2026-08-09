//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Color_Bench.cpp
//  Purpose: Benchmarks packed and transfer-curve color operations.
//  Details: Cheap integer packing is measured separately from transcendental sRGB
//           conversion so renderer and asset-pipeline costs remain interpretable.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Color.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_Color32_PackRGBA8( benchmark::State &state )
{
    color32_t color{ 17u, 63u, 129u, 211u };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( color );
        u32 packed = Color32_PackRGBA8( color );
        benchmark::DoNotOptimize( packed );
    }
}

static void BM_Color_SrgbToLinear( benchmark::State &state )
{
    color32_t color{ 17u, 63u, 129u, 211u };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( color );
        colorf_t linear = Color_SrgbToLinear( color );
        benchmark::DoNotOptimize( linear );
    }
}

static void BM_Color_LinearToSrgb( benchmark::State &state )
{
    colorf_t color{ 0.14f, 0.32f, 0.72f, 0.83f };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( color );
        color32_t srgb = Color_LinearToSrgb( color );
        benchmark::DoNotOptimize( srgb );
    }
}

BENCHMARK( BM_Color32_PackRGBA8 );
BENCHMARK( BM_Color_SrgbToLinear );
BENCHMARK( BM_Color_LinearToSrgb );
