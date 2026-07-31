//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StringView_Bench.cpp
//  Purpose: Benchmarks Tier1 StringView performance.
//  Details: These benchmarks separate null-terminated scanning from constant-time
//           range construction and state queries.
//
//  History:
//  - Created by Karlo Siric on 2026-07-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringView.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr char kShortText[] = "cypher";
constexpr char kMediumText[] = "textures/world/industrial/wall_panel_01_albedo.dds";
constexpr char kLongText[] =
    "assets/materials/world/industrial/facility/sector_07/"
    "wall_panel_reinforced_warning_stripe_albedo_variant_03.dds";

template <usize cchText>
void BenchmarkFromCString( benchmark::State &state, const char ( &text )[cchText] )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_FromCString( text ) );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * static_cast<i64>( cchText - 1u ) );
}

void BM_StringView_FromCString_Short( benchmark::State &state )
{
    BenchmarkFromCString( state, kShortText );
}

void BM_StringView_FromCString_Medium( benchmark::State &state )
{
    BenchmarkFromCString( state, kMediumText );
}

void BM_StringView_FromCString_Long( benchmark::State &state )
{
    BenchmarkFromCString( state, kLongText );
}

void BM_StringView_FromRange_Medium( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            StringView_FromRange( kMediumText, sizeof( kMediumText ) - 1u ) );
    }
}

void BM_StringView_IsValid_Valid( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_IsValid( view ) );
    }
}

void BM_StringView_IsValid_Invalid( benchmark::State &state )
{
    const string_view_t view{ nullptr, 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_IsValid( view ) );
    }
}

void BM_StringView_IsEmpty_Empty( benchmark::State &state )
{
    const string_view_t view{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_IsEmpty( view ) );
    }
}

void BM_StringView_IsEmpty_Populated( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_IsEmpty( view ) );
    }
}

void BM_StringView_Length( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Length( view ) );
    }
}

void BM_StringView_DirectLengthBaseline( benchmark::State &state )
{
    string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( view.cchLength );
    }
}

void BM_StringView_Begin( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Begin( view ) );
    }
}

void BM_StringView_End( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_End( view ) );
    }
}

void BM_StringView_At( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_At( view, 16u ) );
    }
}

void BM_StringView_Front( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Front( view ) );
    }
}

void BM_StringView_Back( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Back( view ) );
    }
}

} // namespace

BENCHMARK( BM_StringView_FromCString_Short );
BENCHMARK( BM_StringView_FromCString_Medium );
BENCHMARK( BM_StringView_FromCString_Long );
BENCHMARK( BM_StringView_FromRange_Medium );
BENCHMARK( BM_StringView_IsValid_Valid );
BENCHMARK( BM_StringView_IsValid_Invalid );
BENCHMARK( BM_StringView_IsEmpty_Empty );
BENCHMARK( BM_StringView_IsEmpty_Populated );
BENCHMARK( BM_StringView_Length );
BENCHMARK( BM_StringView_DirectLengthBaseline );
BENCHMARK( BM_StringView_Begin );
BENCHMARK( BM_StringView_End );
BENCHMARK( BM_StringView_At );
BENCHMARK( BM_StringView_Front );
BENCHMARK( BM_StringView_Back );
