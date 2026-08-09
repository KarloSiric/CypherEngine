//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Variant_Bench.cpp
//  Purpose: Benchmarks primitive variant construction, access, and equality.
//  Details: Numeric access measures tag dispatch while borrowed string equality
//           includes the bounded content comparison required by exact semantics.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Variant.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_VariantU64ConstructGet( benchmark::State &state )
{
    for ( auto _ : state ) {
        variant_t value = Variant_FromU64( 0x123456789ABCDEF0ull );
        u64 output = 0u;
        bool_t bRead = Variant_GetU64( value, &output );
        benchmark::DoNotOptimize( bRead );
        benchmark::DoNotOptimize( output );
    }
}

BENCHMARK( BM_VariantU64ConstructGet );

static void BM_VariantStringEquals( benchmark::State &state )
{
    const variant_t left = Variant_FromString( StringView_FromCString(
        "materials/facility/industrial_wall_panel_damaged_01.cymat" ) );
    const variant_t right = Variant_FromString( StringView_FromCString(
        "materials/facility/industrial_wall_panel_damaged_01.cymat" ) );
    for ( auto _ : state ) {
        bool_t bEqual = Variant_Equals( left, right );
        benchmark::DoNotOptimize( bEqual );
    }
}

BENCHMARK( BM_VariantStringEquals );
