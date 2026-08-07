//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StringSplit_Bench.cpp
//  Purpose: Benchmarks Tier1 StringSplit performance.
//  Details: Measures array-writing, count-only, delimiter-set, multi-byte,
//           and visitor scan paths over representative command-style text.
//
//  History:
//  - Created by Karlo Siric on 2026-08-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringSplit.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr char CY_SPLIT_BENCH_TEXT[] =
    "renderer=opengl;window_width=1920;window_height=1080;"
    "fullscreen=false;audio=openal;language=en;profile=developer;"
    "asset_root=game/assets;shader_root=game/shaders;threads=8";

constexpr usize CY_SPLIT_BENCH_TOKEN_CAPACITY = 32u;

bool_t ConsumeToken(
    string_view_t token,
    usize iToken,
    void *pUserData ) noexcept
{
    benchmark::DoNotOptimize( token.pData );
    benchmark::DoNotOptimize( token.cchLength );
    benchmark::DoNotOptimize( iToken );
    benchmark::DoNotOptimize( pUserData );
    return CY_TRUE;
}

void SetProcessedBytes( benchmark::State &state )
{
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( CY_SPLIT_BENCH_TEXT ) - 1u ) );
}

} // namespace

static void BM_StringSplit_ByChar_ArrayOutput( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_SPLIT_BENCH_TEXT,
        sizeof( CY_SPLIT_BENCH_TEXT ) - 1u );
    string_view_t tokens[CY_SPLIT_BENCH_TOKEN_CAPACITY]{};

    for ( auto _ : state ) {
        string_split_result_t result = StringSplit_ByChar(
            source,
            ';',
            STRING_SPLIT_FLAG_NONE,
            tokens,
            CY_SPLIT_BENCH_TOKEN_CAPACITY );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }

    SetProcessedBytes( state );
}

static void BM_StringSplit_ByChar_CountOnly( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_SPLIT_BENCH_TEXT,
        sizeof( CY_SPLIT_BENCH_TEXT ) - 1u );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringSplit_ByChar(
            source,
            ';',
            STRING_SPLIT_FLAG_NONE,
            nullptr,
            0u ) );
    }

    SetProcessedBytes( state );
}

static void BM_StringSplit_BySet_ArrayOutput( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_SPLIT_BENCH_TEXT,
        sizeof( CY_SPLIT_BENCH_TEXT ) - 1u );
    character_set_t delimiters{};
    CharacterSet_Add( &delimiters, ';' );
    CharacterSet_Add( &delimiters, '=' );
    string_view_t tokens[CY_SPLIT_BENCH_TOKEN_CAPACITY]{};

    for ( auto _ : state ) {
        string_split_result_t result = StringSplit_BySet(
            source,
            &delimiters,
            STRING_SPLIT_FLAG_NONE,
            tokens,
            CY_SPLIT_BENCH_TOKEN_CAPACITY );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }

    SetProcessedBytes( state );
}

static void BM_StringSplit_ByString_ArrayOutput( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_SPLIT_BENCH_TEXT,
        sizeof( CY_SPLIT_BENCH_TEXT ) - 1u );
    const string_view_t delimiter = StringView_FromCString( ";" );
    string_view_t tokens[CY_SPLIT_BENCH_TOKEN_CAPACITY]{};

    for ( auto _ : state ) {
        string_split_result_t result = StringSplit_ByString(
            source,
            delimiter,
            STRING_SPLIT_FLAG_NONE,
            tokens,
            CY_SPLIT_BENCH_TOKEN_CAPACITY );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }

    SetProcessedBytes( state );
}

static void BM_StringSplit_VisitByChar( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_SPLIT_BENCH_TEXT,
        sizeof( CY_SPLIT_BENCH_TEXT ) - 1u );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( StringSplit_VisitByChar(
            source,
            ';',
            STRING_SPLIT_FLAG_NONE,
            ConsumeToken,
            nullptr ) );
    }

    SetProcessedBytes( state );
}

BENCHMARK( BM_StringSplit_ByChar_ArrayOutput );
BENCHMARK( BM_StringSplit_ByChar_CountOnly );
BENCHMARK( BM_StringSplit_BySet_ArrayOutput );
BENCHMARK( BM_StringSplit_ByString_ArrayOutput );
BENCHMARK( BM_StringSplit_VisitByChar );
