//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_ConCommand_Bench.cpp
//  Purpose: Benchmarks allocation-free console-command parsing.
//  Details: Measures common unquoted and quoted command lines independently from
//           registry lookup and callback execution.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ConCommand.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_ConCommandValidateName( benchmark::State &state )
{
    const string_view_t name = StringView_FromCString( "render.shadow_quality" );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ConCommand_IsValidName( name ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( name.cchLength ) );
}

void BM_ConCommandParseUnquoted( benchmark::State &state )
{
    const string_view_t line = StringView_FromCString(
        "spawn enemy_soldier 128 64 -32 aggressive" );
    for ( auto _ : state ) {
        command_args_t args{};
        benchmark::DoNotOptimize( ConCommand_ParseArgs( line, &args ) );
        benchmark::DoNotOptimize( args.nArgumentCount );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( line.cchLength ) );
}

void BM_ConCommandParseQuoted( benchmark::State &state )
{
    const string_view_t line = StringView_FromCString(
        "map_load \"facility sector 07\" 'night mode'" );
    for ( auto _ : state ) {
        command_args_t args{};
        benchmark::DoNotOptimize( ConCommand_ParseArgs( line, &args ) );
        benchmark::DoNotOptimize( args.nArgumentCount );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( line.cchLength ) );
}

} // namespace

BENCHMARK( BM_ConCommandValidateName );
BENCHMARK( BM_ConCommandParseUnquoted );
BENCHMARK( BM_ConCommandParseQuoted );
