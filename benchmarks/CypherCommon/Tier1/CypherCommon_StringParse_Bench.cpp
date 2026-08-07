//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StringParse_Bench.cpp
//  Purpose: Benchmarks deterministic Tier1 integer parsing.
//  Details: Measures common decimal paths against std::from_chars and records
//           additional costs for prefixes, separators, whitespace, and overflow.
//
//  History:
//  - Created by Karlo Siric on 2026-08-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringParse.h"

#include <benchmark/benchmark.h>

#include <charconv>

using namespace cypher::common;

namespace
{

constexpr char CY_PARSE_DECIMAL_SHORT[] = "12345678";
constexpr char CY_PARSE_DECIMAL_MAX[] = "18446744073709551615";
constexpr char CY_PARSE_HEX_PREFIX[] = "0xFEDCBA9876543210";
constexpr char CY_PARSE_SEPARATED[] = "18_446_744_073_709_551_615";
constexpr char CY_PARSE_TRIMMED[] = "  \t18446744073709551615\r\n";
constexpr char CY_PARSE_OVERFLOW[] = "18446744073709551616";

template <usize N>
string_view_t BenchView( const char ( &text )[N] ) noexcept
{
    return StringView_FromRange( text, N - 1u );
}

void SetProcessedBytes(
    benchmark::State &state,
    usize cchInput )
{
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cchInput ) );
}

} // namespace

static void BM_StringParse_U64_DecimalShort( benchmark::State &state )
{
    const string_view_t text = BenchView( CY_PARSE_DECIMAL_SHORT );
    const string_parse_options_t options{};

    for ( auto _ : state ) {
        u64 nValue = 0u;
        string_parse_result_t result =
            StringParse_U64( text, options, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }

    SetProcessedBytes( state, text.cchLength );
}

static void BM_StdFromChars_U64_DecimalShort( benchmark::State &state )
{
    const char *pBegin = CY_PARSE_DECIMAL_SHORT;
    const char *pEnd = pBegin + sizeof( CY_PARSE_DECIMAL_SHORT ) - 1u;

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pBegin );
        benchmark::DoNotOptimize( pEnd );
        u64 nValue = 0u;
        std::from_chars_result result =
            std::from_chars( pBegin, pEnd, nValue, 10 );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }

    SetProcessedBytes( state, sizeof( CY_PARSE_DECIMAL_SHORT ) - 1u );
}

static void BM_StringParse_U64_DecimalMaximum( benchmark::State &state )
{
    const string_view_t text = BenchView( CY_PARSE_DECIMAL_MAX );
    const string_parse_options_t options{};

    for ( auto _ : state ) {
        u64 nValue = 0u;
        string_parse_result_t result =
            StringParse_U64( text, options, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }

    SetProcessedBytes( state, text.cchLength );
}

static void BM_StdFromChars_U64_DecimalMaximum( benchmark::State &state )
{
    const char *pBegin = CY_PARSE_DECIMAL_MAX;
    const char *pEnd = pBegin + sizeof( CY_PARSE_DECIMAL_MAX ) - 1u;

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pBegin );
        benchmark::DoNotOptimize( pEnd );
        u64 nValue = 0u;
        std::from_chars_result result =
            std::from_chars( pBegin, pEnd, nValue, 10 );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }

    SetProcessedBytes( state, sizeof( CY_PARSE_DECIMAL_MAX ) - 1u );
}

static void BM_StringParse_U64_HexPrefix( benchmark::State &state )
{
    const string_view_t text = BenchView( CY_PARSE_HEX_PREFIX );
    string_parse_options_t options{};
    options.nBase = 0u;
    options.flags = STRING_PARSE_FLAG_ALLOW_BASE_PREFIX;

    for ( auto _ : state ) {
        u64 nValue = 0u;
        string_parse_result_t result =
            StringParse_U64( text, options, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }

    SetProcessedBytes( state, text.cchLength );
}

static void BM_StringParse_U64_DigitSeparators( benchmark::State &state )
{
    const string_view_t text = BenchView( CY_PARSE_SEPARATED );
    string_parse_options_t options{};
    options.flags = STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR;

    for ( auto _ : state ) {
        u64 nValue = 0u;
        string_parse_result_t result =
            StringParse_U64( text, options, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }

    SetProcessedBytes( state, text.cchLength );
}

static void BM_StringParse_U64_Trimmed( benchmark::State &state )
{
    const string_view_t text = BenchView( CY_PARSE_TRIMMED );
    string_parse_options_t options{};
    options.flags = STRING_PARSE_FLAG_TRIM_WHITESPACE;

    for ( auto _ : state ) {
        u64 nValue = 0u;
        string_parse_result_t result =
            StringParse_U64( text, options, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }

    SetProcessedBytes( state, text.cchLength );
}

static void BM_StringParse_U64_Overflow( benchmark::State &state )
{
    const string_view_t text = BenchView( CY_PARSE_OVERFLOW );
    const string_parse_options_t options{};

    for ( auto _ : state ) {
        u64 nValue = 0u;
        string_parse_result_t result =
            StringParse_U64( text, options, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }

    SetProcessedBytes( state, text.cchLength );
}

BENCHMARK( BM_StringParse_U64_DecimalShort );
BENCHMARK( BM_StdFromChars_U64_DecimalShort );
BENCHMARK( BM_StringParse_U64_DecimalMaximum );
BENCHMARK( BM_StdFromChars_U64_DecimalMaximum );
BENCHMARK( BM_StringParse_U64_HexPrefix );
BENCHMARK( BM_StringParse_U64_DigitSeparators );
BENCHMARK( BM_StringParse_U64_Trimmed );
BENCHMARK( BM_StringParse_U64_Overflow );
