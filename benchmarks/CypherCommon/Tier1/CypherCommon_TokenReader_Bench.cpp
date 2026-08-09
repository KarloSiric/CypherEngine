//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_TokenReader_Bench.cpp
//  Purpose: Benchmarks Tier1 TokenReader parser-facing operations.
//  Details: Measures sequential consumption, fixed lookahead, and typed primitive
//           conversion over representative command and configuration input.
//
//  History:
//  - Created by Karlo Siric on 2026-08-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TokenReader.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr char CY_TOKEN_READER_BENCH_TEXT[] =
    "set width 1920 set height 1080 set fullscreen true "
    "set sensitivity 2.5 set max_players 16";

void SetProcessedBytes( benchmark::State &state )
{
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( CY_TOKEN_READER_BENCH_TEXT ) - 1u ) );
}

} // namespace

static void BM_TokenReader_SequentialRead( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_TOKEN_READER_BENCH_TEXT,
        sizeof( CY_TOKEN_READER_BENCH_TEXT ) - 1u );
    const lexer_rules_t rules = Lexer_DefaultRules();

    for ( auto _ : state ) {
        token_reader_t reader{};
        benchmark::DoNotOptimize( TokenReader_Init( &reader, source, rules ) );

        token_t token{};
        usize cTokens = 0u;
        while ( TokenReader_Read( &reader, &token ) == token_reader_status_t::OK ) {
            ++cTokens;
            benchmark::DoNotOptimize( token.lexeme.pData );
        }
        benchmark::DoNotOptimize( cTokens );
    }
    SetProcessedBytes( state );
}

static void BM_TokenReader_PeekThenRead( benchmark::State &state )
{
    const string_view_t source = StringView_FromRange(
        CY_TOKEN_READER_BENCH_TEXT,
        sizeof( CY_TOKEN_READER_BENCH_TEXT ) - 1u );
    const lexer_rules_t rules = Lexer_DefaultRules();

    for ( auto _ : state ) {
        token_reader_t reader{};
        benchmark::DoNotOptimize( TokenReader_Init( &reader, source, rules ) );

        token_t token{};
        usize cTokens = 0u;
        while ( TokenReader_Peek( &reader, 0u, &token ) == token_reader_status_t::OK ) {
            benchmark::DoNotOptimize( token.lexeme.pData );
            benchmark::DoNotOptimize( TokenReader_Read( &reader, &token ) );
            ++cTokens;
        }
        benchmark::DoNotOptimize( cTokens );
    }
    SetProcessedBytes( state );
}

static void BM_TokenReader_TypedReads( benchmark::State &state )
{
    const string_view_t source = StringView_FromCString( "1920 -7 3.5 true" );
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags |= LEXER_FLAG_SIGN_IS_NUMBER_PART;
    const string_parse_options_t integerOptions{};

    for ( auto _ : state ) {
        token_reader_t reader{};
        benchmark::DoNotOptimize( TokenReader_Init( &reader, source, rules ) );

        u64 nUnsigned = 0u;
        i64 nSigned = 0;
        f64 nFloat = 0.0;
        bool_t bValue = CY_FALSE;
        benchmark::DoNotOptimize( TokenReader_ReadU64( &reader, integerOptions, &nUnsigned ) );
        benchmark::DoNotOptimize( TokenReader_ReadI64( &reader, integerOptions, &nSigned ) );
        benchmark::DoNotOptimize( TokenReader_ReadF64( &reader, STRING_PARSE_FLAG_NONE, &nFloat ) );
        benchmark::DoNotOptimize( TokenReader_ReadBool( &reader, STRING_PARSE_FLAG_NONE, &bValue ) );
        benchmark::DoNotOptimize( nUnsigned );
        benchmark::DoNotOptimize( nSigned );
        benchmark::DoNotOptimize( nFloat );
        benchmark::DoNotOptimize( bValue );
    }
}

BENCHMARK( BM_TokenReader_SequentialRead );
BENCHMARK( BM_TokenReader_PeekThenRead );
BENCHMARK( BM_TokenReader_TypedReads );
