//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StringView_Bench.cpp
//  Purpose: Benchmarks Tier1 StringView performance.
//  Details: These benchmarks separate constant-time view operations from bounded
//           comparison, search, trimming, and copy workloads.
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
constexpr char kMediumTextCopy[] = "textures/world/industrial/wall_panel_01_albedo.dds";
constexpr char kMediumTextCase[] = "TEXTURES/WORLD/INDUSTRIAL/WALL_PANEL_01_ALBEDO.DDS";
constexpr char kMediumTextLateMismatch[] =
    "textures/world/industrial/wall_panel_01_albedo.png";
constexpr char kLongText[] =
    "assets/materials/world/industrial/facility/sector_07/"
    "wall_panel_reinforced_warning_stripe_albedo_variant_03.dds";
constexpr char kTrimmedText[] = " \t\ntextures/world/wall.dds\r\n ";

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

void BM_StringView_Compare_Equal( benchmark::State &state )
{
    const string_view_t viewA{ kMediumText, sizeof( kMediumText ) - 1u };
    const string_view_t viewB{ kMediumTextCopy, sizeof( kMediumTextCopy ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Compare( viewA, viewB ) );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( kMediumText ) - 1u ) );
}

void BM_StringView_Compare_LateMismatch( benchmark::State &state )
{
    const string_view_t viewA{ kMediumText, sizeof( kMediumText ) - 1u };
    const string_view_t viewB{
        kMediumTextLateMismatch,
        sizeof( kMediumTextLateMismatch ) - 1u
    };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Compare( viewA, viewB ) );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( kMediumText ) - 1u ) );
}

void BM_StringView_CompareInsensitiveAscii( benchmark::State &state )
{
    const string_view_t viewA{ kMediumText, sizeof( kMediumText ) - 1u };
    const string_view_t viewB{ kMediumTextCase, sizeof( kMediumTextCase ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            StringView_CompareInsensitiveAscii( viewA, viewB ) );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( kMediumText ) - 1u ) );
}

void BM_StringView_Equals_Equal( benchmark::State &state )
{
    const string_view_t viewA{ kMediumText, sizeof( kMediumText ) - 1u };
    const string_view_t viewB{ kMediumTextCopy, sizeof( kMediumTextCopy ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Equals( viewA, viewB ) );
    }
}

void BM_StringView_StartsWith( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };
    const string_view_t prefix{ kMediumTextCopy, 25u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_StartsWith( view, prefix ) );
    }
}

void BM_StringView_EndsWith( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };
    constexpr char kSuffix[] = "_albedo.dds";
    const string_view_t suffix{ kSuffix, sizeof( kSuffix ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_EndsWith( view, suffix ) );
    }
}

void BM_StringView_Subview( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Subview( view, 15u, 20u ) );
    }
}

void BM_StringView_FindChar_Late( benchmark::State &state )
{
    const string_view_t view{ kMediumText, sizeof( kMediumText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_FindChar( view, '.' ) );
    }
}

void BM_StringView_Find_PresentLate( benchmark::State &state )
{
    const string_view_t view{ kLongText, sizeof( kLongText ) - 1u };
    constexpr char kSearch[] = "albedo";
    const string_view_t search{ kSearch, sizeof( kSearch ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Find( view, search ) );
    }
}

void BM_StringView_Find_Missing( benchmark::State &state )
{
    const string_view_t view{ kLongText, sizeof( kLongText ) - 1u };
    constexpr char kSearch[] = "normalmap";
    const string_view_t search{ kSearch, sizeof( kSearch ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Find( view, search ) );
    }
}

void BM_StringView_FindInsensitiveAscii( benchmark::State &state )
{
    const string_view_t view{ kLongText, sizeof( kLongText ) - 1u };
    constexpr char kSearch[] = "ALBEDO";
    const string_view_t search{ kSearch, sizeof( kSearch ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            StringView_FindInsensitiveAscii( view, search ) );
    }
}

void BM_StringView_Trim( benchmark::State &state )
{
    const string_view_t view{ kTrimmedText, sizeof( kTrimmedText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringView_Trim( view ) );
    }
}

void BM_StringView_CopyToCString( benchmark::State &state )
{
    const string_view_t view{ kLongText, sizeof( kLongText ) - 1u };
    char dest[sizeof( kLongText )]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            StringView_CopyToCString( view, dest, sizeof( dest ) ) );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( kLongText ) - 1u ) );
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
BENCHMARK( BM_StringView_Compare_Equal );
BENCHMARK( BM_StringView_Compare_LateMismatch );
BENCHMARK( BM_StringView_CompareInsensitiveAscii );
BENCHMARK( BM_StringView_Equals_Equal );
BENCHMARK( BM_StringView_StartsWith );
BENCHMARK( BM_StringView_EndsWith );
BENCHMARK( BM_StringView_Subview );
BENCHMARK( BM_StringView_FindChar_Late );
BENCHMARK( BM_StringView_Find_PresentLate );
BENCHMARK( BM_StringView_Find_Missing );
BENCHMARK( BM_StringView_FindInsensitiveAscii );
BENCHMARK( BM_StringView_Trim );
BENCHMARK( BM_StringView_CopyToCString );
