//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_CommandLine_Bench.cpp
//  Purpose: Benchmarks allocation-free process switch lookup.
//  Details: Early, late, and missing lookups expose fixed parsing cost and the linear
//           scan expected from the small process command-line argument domain.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandLine.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

const char *g_arguments[]{
    "CypherEngine", "--renderer=opengl", "--windowed", "--width=1920",
    "--height=1080", "--game=facility", "--mod=survival", "--threads=8",
    "--audio=openal", "--device=default", "--rate=48000", "--vsync=1",
    "--debug-draw=0", "--profile=0", "--console=1", "--developer=1",
    "--map=arena_01", "--bots=12", "--difficulty=hard", "--language=en",
    "--pak=base.cypak", "--pak=game.cypak", "--log=CypherEngine.log",
    "--config=user.cfg", "--network-port=27960", "--server-name=local",
    "--max-clients=16", "--tick-rate=60", "--snapshot-rate=20",
    "--capture=0", "--validation=1", "--last-option=present"
};

command_line_t MakeCommandLine()
{
    command_line_t commandLine{};
    bool_t bInitialized = CommandLine_Init(
        &commandLine,
        static_cast<i32>( sizeof( g_arguments ) / sizeof( g_arguments[0] ) ),
        g_arguments );
    benchmark::DoNotOptimize( bInitialized );
    return commandLine;
}

} // namespace

static void BM_CommandLineFindEarly( benchmark::State &state )
{
    const command_line_t commandLine = MakeCommandLine();
    const string_view_t name = StringView_FromCString( "renderer" );
    for ( auto _ : state ) {
        i32 iArgument = CommandLine_FindSwitch( &commandLine, name );
        benchmark::DoNotOptimize( iArgument );
    }
}

BENCHMARK( BM_CommandLineFindEarly );

static void BM_CommandLineFindLate( benchmark::State &state )
{
    const command_line_t commandLine = MakeCommandLine();
    const string_view_t name = StringView_FromCString( "last-option" );
    for ( auto _ : state ) {
        i32 iArgument = CommandLine_FindSwitch( &commandLine, name );
        benchmark::DoNotOptimize( iArgument );
    }
}

BENCHMARK( BM_CommandLineFindLate );

static void BM_CommandLineFindMissing( benchmark::State &state )
{
    const command_line_t commandLine = MakeCommandLine();
    const string_view_t name = StringView_FromCString( "missing-option" );
    for ( auto _ : state ) {
        i32 iArgument = CommandLine_FindSwitch( &commandLine, name );
        benchmark::DoNotOptimize( iArgument );
    }
}

BENCHMARK( BM_CommandLineFindMissing );
