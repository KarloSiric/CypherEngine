//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Lexer_Bench.cpp
//  Purpose: Benchmarks Tier1 lexical scanning throughput.
//  Details: Measures representative CYDF-style token streams with trivia skipped,
//           trivia emitted, and numeric-heavy input using allocation-free scanning.
//
//  History:
//  - Created by Karlo Siric on 2026-08-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Lexer.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr char CY_LEXER_BENCH_DOCUMENT[] =
    "// renderer configuration\n"
    "renderer { backend = \"opengl\"; debug = false; }\n"
    "window { width = 1920; height = 1080; fullscreen = true; }\n"
    "player { name = \"ranger\"; speed = 320.0; gravity = 800; }\n"
    "/* package search policy */\n"
    "mounts = [ \"game\", \"engine\", \"mods/current\" ];\n";

constexpr char CY_LEXER_BENCH_NUMBERS[] =
    "0 1 -1 42 255 1024 65535 0x2A 0b101010 0o52 "
    "3.14159265 .5 1. 6.022e23 -2.5e-8 1_000_000 0xFFFF_FFFF";

usize ScanAll( string_view_t source, const lexer_rules_t &rules ) noexcept
{
    lexer_t lexer{};
    if ( !Lexer_Init( &lexer, source, rules ) ) {
        return 0u;
    }

    usize cTokens = 0u;
    token_t token{};
    for ( ;; ) {
        const lexer_status_t status = Lexer_Read( &lexer, &token );
        if ( status == lexer_status_t::END_OF_INPUT ) {
            break;
        }
        if ( status != lexer_status_t::OK ) {
            return 0u;
        }
        ++cTokens;
        benchmark::DoNotOptimize( token.lexeme.pData );
    }
    return cTokens;
}

void SetProcessedBytes( benchmark::State &state, usize cBytes )
{
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * static_cast<i64>( cBytes ) );
}

} // namespace

static void BM_Lexer_CydfLike_SkipTrivia( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_LEXER_BENCH_DOCUMENT,
        sizeof( CY_LEXER_BENCH_DOCUMENT ) - 1u );
    const lexer_rules_t rules = Lexer_DefaultRules();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ScanAll( source, rules ) );
    }
    SetProcessedBytes( state, source.cchLength );
}

static void BM_Lexer_CydfLike_EmitTrivia( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_LEXER_BENCH_DOCUMENT,
        sizeof( CY_LEXER_BENCH_DOCUMENT ) - 1u );
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_EMIT_COMMENTS | LEXER_FLAG_EMIT_NEWLINES;

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ScanAll( source, rules ) );
    }
    SetProcessedBytes( state, source.cchLength );
}

static void BM_Lexer_NumericHeavy( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_LEXER_BENCH_NUMBERS,
        sizeof( CY_LEXER_BENCH_NUMBERS ) - 1u );
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_SIGN_IS_NUMBER_PART;

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ScanAll( source, rules ) );
    }
    SetProcessedBytes( state, source.cchLength );
}

BENCHMARK( BM_Lexer_CydfLike_SkipTrivia );
BENCHMARK( BM_Lexer_CydfLike_EmitTrivia );
BENCHMARK( BM_Lexer_NumericHeavy );
