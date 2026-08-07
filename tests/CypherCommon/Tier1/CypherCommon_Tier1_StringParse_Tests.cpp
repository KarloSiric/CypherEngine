//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringParse_Tests.cpp
//  Purpose: Tests deterministic Tier1 string-to-value conversion.
//  Details: These tests protect primitive grammars, bounded input behavior,
//           range detection, result offsets, and output preservation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_String.h"
#include "CypherCommon_StringParse.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace cypher::common;

namespace
{

u32 g_stringParseAssertCount = 0u;

assert_action_t CaptureStringParseAssert( const assert_info_t & ) noexcept
{
    ++g_stringParseAssertCount;
    return assert_action_t::Continue;
}

string_parse_options_t IntegerOptions(
    u8 nBase = 10u,
    flags32_t flags = STRING_PARSE_FLAG_NONE ) noexcept
{
    string_parse_options_t options{};
    options.nBase = nBase;
    options.flags = flags;
    return options;
}

} // namespace

TEST_CASE( "StringParse status helpers expose stable names",
           "[CypherCommon][Tier1][StringParse]" )
{
    REQUIRE( StringParse_Succeeded( { string_parse_status_t::OK, 0u, CY_INVALID_SIZE } ) );
    REQUIRE_FALSE( StringParse_Succeeded( {} ) );

    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::OK ), "OK" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::EMPTY_INPUT ), "EMPTY_INPUT" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::INVALID_ARGUMENT ), "INVALID_ARGUMENT" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::INVALID_BASE ), "INVALID_BASE" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::INVALID_CHARACTER ), "INVALID_CHARACTER" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::NUMERIC_OVERFLOW ), "NUMERIC_OVERFLOW" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::NUMERIC_UNDERFLOW ), "NUMERIC_UNDERFLOW" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::TRAILING_CHARACTERS ), "TRAILING_CHARACTERS" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( string_parse_status_t::NON_FINITE_VALUE ), "NON_FINITE_VALUE" ) == 0 );
    REQUIRE( Cy_strcmp( StringParse_StatusName( static_cast<string_parse_status_t>( 0xffu ) ), "UNKNOWN" ) == 0 );
}

TEST_CASE( "StringParse_U64 parses decimal and the full unsigned range",
           "[CypherCommon][Tier1][StringParse]" )
{
    u64 nValue = 99u;

    const string_parse_result_t zeroResult = StringParse_U64(
        StringView_FromCString( "0" ), IntegerOptions(), &nValue );
    REQUIRE( zeroResult.status == string_parse_status_t::OK );
    REQUIRE( zeroResult.cchConsumed == 1u );
    REQUIRE( zeroResult.iError == CY_INVALID_SIZE );
    REQUIRE( nValue == 0u );

    const string_parse_result_t decimalResult = StringParse_U64(
        StringView_FromCString( "123456789" ), IntegerOptions(), &nValue );
    REQUIRE( decimalResult.status == string_parse_status_t::OK );
    REQUIRE( decimalResult.cchConsumed == 9u );
    REQUIRE( nValue == 123456789u );

    const string_parse_result_t maximumResult = StringParse_U64(
        StringView_FromCString( "18446744073709551615" ), IntegerOptions(), &nValue );
    REQUIRE( maximumResult.status == string_parse_status_t::OK );
    REQUIRE( maximumResult.cchConsumed == 20u );
    REQUIRE( nValue == CY_U64_MAX );
}

TEST_CASE( "StringParse_U64 handles explicit bases and enabled prefixes",
           "[CypherCommon][Tier1][StringParse]" )
{
    u64 nValue = 0u;
    const flags32_t prefixFlags = STRING_PARSE_FLAG_ALLOW_BASE_PREFIX;

    REQUIRE( StringParse_U64(
        StringView_FromCString( "101010" ), IntegerOptions( 2u ), &nValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( nValue == 42u );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "0b101010" ), IntegerOptions( 0u, prefixFlags ), &nValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( nValue == 42u );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "0o52" ), IntegerOptions( 0u, prefixFlags ), &nValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( nValue == 42u );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "0x2A" ), IntegerOptions( 0u, prefixFlags ), &nValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( nValue == 42u );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "z" ), IntegerOptions( 36u ), &nValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( nValue == 35u );
}

TEST_CASE( "StringParse_U64 applies plus and digit-separator policy",
           "[CypherCommon][Tier1][StringParse]" )
{
    u64 nValue = 777u;
    const flags32_t flags =
        STRING_PARSE_FLAG_ALLOW_PLUS_SIGN |
        STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR;

    const string_parse_result_t validResult = StringParse_U64(
        StringView_FromCString( "+1_000_000" ),
        IntegerOptions( 10u, flags ),
        &nValue );
    REQUIRE( validResult.status == string_parse_status_t::OK );
    REQUIRE( validResult.cchConsumed == 10u );
    REQUIRE( nValue == 1000000u );

    nValue = 777u;
    REQUIRE( StringParse_U64(
        StringView_FromCString( "_1" ), IntegerOptions( 10u, flags ), &nValue ).status ==
        string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( nValue == 777u );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "1_" ), IntegerOptions( 10u, flags ), &nValue ).status ==
        string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( nValue == 777u );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "1__0" ), IntegerOptions( 10u, flags ), &nValue ).status ==
        string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( nValue == 777u );
}

TEST_CASE( "StringParse_U64 reports whitespace and trailing-input positions",
           "[CypherCommon][Tier1][StringParse]" )
{
    u64 nValue = 0u;

    const string_parse_result_t trimmedResult = StringParse_U64(
        StringView_FromCString( " \t42\r\n" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_TRIM_WHITESPACE ),
        &nValue );
    REQUIRE( trimmedResult.status == string_parse_status_t::OK );
    REQUIRE( trimmedResult.cchConsumed == 6u );
    REQUIRE( nValue == 42u );

    nValue = 777u;
    const string_parse_result_t trailingResult = StringParse_U64(
        StringView_FromCString( "42px" ), IntegerOptions(), &nValue );
    REQUIRE( trailingResult.status == string_parse_status_t::TRAILING_CHARACTERS );
    REQUIRE( trailingResult.cchConsumed == 2u );
    REQUIRE( trailingResult.iError == 2u );
    REQUIRE( nValue == 777u );

    const string_parse_result_t partialResult = StringParse_U64(
        StringView_FromCString( "42px" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS ),
        &nValue );
    REQUIRE( partialResult.status == string_parse_status_t::OK );
    REQUIRE( partialResult.cchConsumed == 2u );
    REQUIRE( nValue == 42u );
}

TEST_CASE( "StringParse_U64 detects overflow without changing output",
           "[CypherCommon][Tier1][StringParse]" )
{
    u64 nValue = 777u;
    const string_parse_result_t result = StringParse_U64(
        StringView_FromCString( "18446744073709551616" ),
        IntegerOptions(),
        &nValue );

    REQUIRE( result.status == string_parse_status_t::NUMERIC_OVERFLOW );
    REQUIRE( result.cchConsumed == 19u );
    REQUIRE( result.iError == 19u );
    REQUIRE( nValue == 777u );
}

TEST_CASE( "StringParse_U64 preserves bounded input and embedded null bytes",
           "[CypherCommon][Tier1][StringParse]" )
{
    const char text[] = { '4', '2', '\0', '9' };
    u64 nValue = 777u;

    const string_parse_result_t rejected = StringParse_U64(
        StringView_FromRange( text, sizeof( text ) ), IntegerOptions(), &nValue );
    REQUIRE( rejected.status == string_parse_status_t::TRAILING_CHARACTERS );
    REQUIRE( rejected.iError == 2u );
    REQUIRE( nValue == 777u );

    const string_parse_result_t partial = StringParse_U64(
        StringView_FromRange( text, sizeof( text ) ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS ),
        &nValue );
    REQUIRE( partial.status == string_parse_status_t::OK );
    REQUIRE( partial.cchConsumed == 2u );
    REQUIRE( nValue == 42u );
}

TEST_CASE( "StringParse_U64 rejects invalid contracts and unknown flags",
           "[CypherCommon][Tier1][StringParse]" )
{
    const string_view_t invalidView{ nullptr, 1u };
    u64 nValue = 777u;

    g_stringParseAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringParseAssert );

    const string_parse_result_t invalidViewResult = StringParse_U64(
        invalidView, IntegerOptions(), &nValue );
    const string_parse_result_t invalidOutputResult = StringParse_U64(
        StringView_FromCString( "1" ), IntegerOptions(), nullptr );
    const string_parse_result_t invalidBaseResult = StringParse_U64(
        StringView_FromCString( "1" ), IntegerOptions( 1u ), &nValue );
    const string_parse_result_t invalidFlagResult = StringParse_U64(
        StringView_FromCString( "1" ),
        IntegerOptions( 10u, CYPHER_BIT32( 31 ) ),
        &nValue );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( invalidViewResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( invalidOutputResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( invalidBaseResult.status == string_parse_status_t::INVALID_BASE );
    REQUIRE( invalidFlagResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( nValue == 777u );
    REQUIRE( g_stringParseAssertCount == 4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "StringParse_U64 distinguishes empty input and malformed prefixes",
           "[CypherCommon][Tier1][StringParse]" )
{
    u64 nValue = 777u;

    REQUIRE( StringParse_U64(
        {}, IntegerOptions(), &nValue ).status == string_parse_status_t::EMPTY_INPUT );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "   " ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_TRIM_WHITESPACE ),
        &nValue ).status == string_parse_status_t::EMPTY_INPUT );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "0x" ),
        IntegerOptions( 0u, STRING_PARSE_FLAG_ALLOW_BASE_PREFIX ),
        &nValue ).status == string_parse_status_t::INVALID_CHARACTER );

    REQUIRE( StringParse_U64(
        StringView_FromCString( "0x10" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_ALLOW_BASE_PREFIX ),
        &nValue ).status == string_parse_status_t::INVALID_CHARACTER );

    REQUIRE( nValue == 777u );
}

TEST_CASE( "StringParse_I64 parses positive negative and explicit-plus values",
           "[CypherCommon][Tier1][StringParse]" )
{
    i64 nValue = 0;

    REQUIRE( StringParse_I64(
        StringView_FromCString( "123" ), IntegerOptions(), &nValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( nValue == 123 );

    REQUIRE( StringParse_I64(
        StringView_FromCString( "-123" ), IntegerOptions(), &nValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( nValue == -123 );

    REQUIRE( StringParse_I64(
        StringView_FromCString( "+123" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_ALLOW_PLUS_SIGN ),
        &nValue ).status == string_parse_status_t::OK );
    REQUIRE( nValue == 123 );

    REQUIRE( StringParse_I64(
        StringView_FromCString( "-0" ), IntegerOptions(), &nValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( nValue == 0 );
}

TEST_CASE( "StringParse_I64 accepts both signed boundaries",
           "[CypherCommon][Tier1][StringParse]" )
{
    i64 nValue = 0;

    const string_parse_result_t maximumResult = StringParse_I64(
        StringView_FromCString( "9223372036854775807" ),
        IntegerOptions(),
        &nValue );
    REQUIRE( maximumResult.status == string_parse_status_t::OK );
    REQUIRE( maximumResult.cchConsumed == 19u );
    REQUIRE( nValue == CY_I64_MAX );

    const string_parse_result_t minimumResult = StringParse_I64(
        StringView_FromCString( "-9223372036854775808" ),
        IntegerOptions(),
        &nValue );
    REQUIRE( minimumResult.status == string_parse_status_t::OK );
    REQUIRE( minimumResult.cchConsumed == 20u );
    REQUIRE( nValue == CY_I64_MIN );
}

TEST_CASE( "StringParse_I64 distinguishes positive overflow and negative underflow",
           "[CypherCommon][Tier1][StringParse]" )
{
    i64 nValue = 777;

    const string_parse_result_t overflowResult = StringParse_I64(
        StringView_FromCString( "9223372036854775808" ),
        IntegerOptions(),
        &nValue );
    REQUIRE( overflowResult.status == string_parse_status_t::NUMERIC_OVERFLOW );
    REQUIRE( overflowResult.cchConsumed == 19u );
    REQUIRE( overflowResult.iError == 0u );
    REQUIRE( nValue == 777 );

    const string_parse_result_t underflowResult = StringParse_I64(
        StringView_FromCString( "-9223372036854775809" ),
        IntegerOptions(),
        &nValue );
    REQUIRE( underflowResult.status == string_parse_status_t::NUMERIC_UNDERFLOW );
    REQUIRE( underflowResult.cchConsumed == 20u );
    REQUIRE( underflowResult.iError == 0u );
    REQUIRE( nValue == 777 );

    const string_parse_result_t magnitudeOverflowResult = StringParse_I64(
        StringView_FromCString( "-18446744073709551616" ),
        IntegerOptions(),
        &nValue );
    REQUIRE( magnitudeOverflowResult.status == string_parse_status_t::NUMERIC_UNDERFLOW );
    REQUIRE( magnitudeOverflowResult.cchConsumed == 20u );
    REQUIRE( magnitudeOverflowResult.iError == 20u );
    REQUIRE( nValue == 777 );
}

TEST_CASE( "StringParse_I64 preserves original offsets while parsing negative magnitudes",
           "[CypherCommon][Tier1][StringParse]" )
{
    i64 nValue = 0;
    const flags32_t flags =
        STRING_PARSE_FLAG_TRIM_WHITESPACE |
        STRING_PARSE_FLAG_ALLOW_BASE_PREFIX;

    const string_parse_result_t result = StringParse_I64(
        StringView_FromCString( "  -0x2A  " ),
        IntegerOptions( 0u, flags ),
        &nValue );

    REQUIRE( result.status == string_parse_status_t::OK );
    REQUIRE( result.cchConsumed == 9u );
    REQUIRE( result.iError == CY_INVALID_SIZE );
    REQUIRE( nValue == -42 );

    const string_parse_result_t partialResult = StringParse_I64(
        StringView_FromCString( "-123tail" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS ),
        &nValue );
    REQUIRE( partialResult.status == string_parse_status_t::OK );
    REQUIRE( partialResult.cchConsumed == 4u );
    REQUIRE( nValue == -123 );
}

TEST_CASE( "StringParse_I64 rejects missing separated and repeated signs",
           "[CypherCommon][Tier1][StringParse]" )
{
    i64 nValue = 777;

    const string_parse_result_t missingMagnitude = StringParse_I64(
        StringView_FromCString( "-" ), IntegerOptions(), &nValue );
    REQUIRE( missingMagnitude.status == string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( missingMagnitude.iError == 1u );

    const string_parse_result_t separatedMagnitude = StringParse_I64(
        StringView_FromCString( "- 1" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_TRIM_WHITESPACE ),
        &nValue );
    REQUIRE( separatedMagnitude.status == string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( separatedMagnitude.iError == 1u );

    const string_parse_result_t repeatedSign = StringParse_I64(
        StringView_FromCString( "-+1" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_ALLOW_PLUS_SIGN ),
        &nValue );
    REQUIRE( repeatedSign.status == string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( repeatedSign.iError == 1u );
    REQUIRE( nValue == 777 );
}

TEST_CASE( "StringParse_U32 accepts its exact numeric boundaries",
           "[CypherCommon][Tier1][StringParse]" )
{
    u32 nValue = 99u;

    const string_parse_result_t zeroResult = StringParse_U32(
        StringView_FromCString( "0" ), IntegerOptions(), &nValue );
    REQUIRE( zeroResult.status == string_parse_status_t::OK );
    REQUIRE( zeroResult.cchConsumed == 1u );
    REQUIRE( zeroResult.iError == CY_INVALID_SIZE );
    REQUIRE( nValue == 0u );

    const string_parse_result_t maximumResult = StringParse_U32(
        StringView_FromCString( "4294967295" ), IntegerOptions(), &nValue );
    REQUIRE( maximumResult.status == string_parse_status_t::OK );
    REQUIRE( maximumResult.cchConsumed == 10u );
    REQUIRE( maximumResult.iError == CY_INVALID_SIZE );
    REQUIRE( nValue == CY_U32_MAX );
}

TEST_CASE( "StringParse_U32 reports narrowing overflow and preserves output",
           "[CypherCommon][Tier1][StringParse]" )
{
    u32 nValue = 777u;

    const string_parse_result_t overflowResult = StringParse_U32(
        StringView_FromCString( "4294967296" ), IntegerOptions(), &nValue );
    REQUIRE( overflowResult.status == string_parse_status_t::NUMERIC_OVERFLOW );
    REQUIRE( overflowResult.cchConsumed == 10u );
    REQUIRE( overflowResult.iError == 0u );
    REQUIRE( nValue == 777u );

    const string_parse_result_t trimmedOverflowResult = StringParse_U32(
        StringView_FromCString( "  4294967296" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_TRIM_WHITESPACE ),
        &nValue );
    REQUIRE( trimmedOverflowResult.status == string_parse_status_t::NUMERIC_OVERFLOW );
    REQUIRE( trimmedOverflowResult.cchConsumed == 12u );
    REQUIRE( trimmedOverflowResult.iError == 2u );
    REQUIRE( nValue == 777u );
}

TEST_CASE( "StringParse_U32 reuses unsigned grammar without partial writes",
           "[CypherCommon][Tier1][StringParse]" )
{
    u32 nValue = 777u;

    const string_parse_result_t hexadecimalResult = StringParse_U32(
        StringView_FromCString( "0xffffffff" ),
        IntegerOptions( 0u, STRING_PARSE_FLAG_ALLOW_BASE_PREFIX ),
        &nValue );
    REQUIRE( hexadecimalResult.status == string_parse_status_t::OK );
    REQUIRE( hexadecimalResult.cchConsumed == 10u );
    REQUIRE( nValue == CY_U32_MAX );

    const string_parse_result_t separatedResult = StringParse_U32(
        StringView_FromCString( "4_294_967_295" ),
        IntegerOptions( 10u, STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR ),
        &nValue );
    REQUIRE( separatedResult.status == string_parse_status_t::OK );
    REQUIRE( separatedResult.cchConsumed == 13u );
    REQUIRE( nValue == CY_U32_MAX );

    nValue = 777u;
    const string_parse_result_t malformedResult = StringParse_U32(
        StringView_FromCString( "12px" ), IntegerOptions(), &nValue );
    REQUIRE( malformedResult.status == string_parse_status_t::TRAILING_CHARACTERS );
    REQUIRE( malformedResult.cchConsumed == 2u );
    REQUIRE( malformedResult.iError == 2u );
    REQUIRE( nValue == 777u );
}

TEST_CASE( "StringParse_U32 rejects null output storage",
           "[CypherCommon][Tier1][StringParse]" )
{
    g_stringParseAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringParseAssert );

    const string_parse_result_t result = StringParse_U32(
        StringView_FromCString( "1" ), IntegerOptions(), nullptr );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( result.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( result.cchConsumed == 0u );
    REQUIRE( result.iError == 0u );
    REQUIRE( g_stringParseAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "StringParse_I32 accepts both signed boundaries",
           "[CypherCommon][Tier1][StringParse]" )
{
    i32 nValue = 0;

    const string_parse_result_t maximumResult = StringParse_I32(
        StringView_FromCString( "2147483647" ), IntegerOptions(), &nValue );
    REQUIRE( maximumResult.status == string_parse_status_t::OK );
    REQUIRE( maximumResult.cchConsumed == 10u );
    REQUIRE( maximumResult.iError == CY_INVALID_SIZE );
    REQUIRE( nValue == CY_I32_MAX );

    const string_parse_result_t minimumResult = StringParse_I32(
        StringView_FromCString( "-2147483648" ), IntegerOptions(), &nValue );
    REQUIRE( minimumResult.status == string_parse_status_t::OK );
    REQUIRE( minimumResult.cchConsumed == 11u );
    REQUIRE( minimumResult.iError == CY_INVALID_SIZE );
    REQUIRE( nValue == CY_I32_MIN );
}

TEST_CASE( "StringParse_I32 reports narrowing overflow and underflow",
           "[CypherCommon][Tier1][StringParse]" )
{
    i32 nValue = 777;

    const string_parse_result_t overflowResult = StringParse_I32(
        StringView_FromCString( "2147483648" ), IntegerOptions(), &nValue );
    REQUIRE( overflowResult.status == string_parse_status_t::NUMERIC_OVERFLOW );
    REQUIRE( overflowResult.cchConsumed == 10u );
    REQUIRE( overflowResult.iError == 0u );
    REQUIRE( nValue == 777 );

    const string_parse_result_t underflowResult = StringParse_I32(
        StringView_FromCString( "-2147483649" ), IntegerOptions(), &nValue );
    REQUIRE( underflowResult.status == string_parse_status_t::NUMERIC_UNDERFLOW );
    REQUIRE( underflowResult.cchConsumed == 11u );
    REQUIRE( underflowResult.iError == 0u );
    REQUIRE( nValue == 777 );
}

TEST_CASE( "StringParse_I32 reuses signed prefix and whitespace policy",
           "[CypherCommon][Tier1][StringParse]" )
{
    i32 nValue = 0;
    const flags32_t flags =
        STRING_PARSE_FLAG_TRIM_WHITESPACE |
        STRING_PARSE_FLAG_ALLOW_BASE_PREFIX;

    const string_parse_result_t minimumResult = StringParse_I32(
        StringView_FromCString( "  -0x80000000  " ),
        IntegerOptions( 0u, flags ),
        &nValue );
    REQUIRE( minimumResult.status == string_parse_status_t::OK );
    REQUIRE( minimumResult.cchConsumed == 15u );
    REQUIRE( nValue == CY_I32_MIN );

    const string_parse_result_t maximumResult = StringParse_I32(
        StringView_FromCString( "0x7fffffff" ),
        IntegerOptions( 0u, STRING_PARSE_FLAG_ALLOW_BASE_PREFIX ),
        &nValue );
    REQUIRE( maximumResult.status == string_parse_status_t::OK );
    REQUIRE( nValue == CY_I32_MAX );
}

TEST_CASE( "StringParse_F64 parses decimal exponent and explicit-plus forms",
           "[CypherCommon][Tier1][StringParse]" )
{
    f64 nValue = 0.0;

    const string_parse_result_t decimalResult = StringParse_F64(
        StringView_FromCString( "123.5" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( decimalResult.status == string_parse_status_t::OK );
    REQUIRE( decimalResult.cchConsumed == 5u );
    REQUIRE( decimalResult.iError == CY_INVALID_SIZE );
    REQUIRE( nValue == 123.5 );

    const string_parse_result_t negativeResult = StringParse_F64(
        StringView_FromCString( "-0.125" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( negativeResult.status == string_parse_status_t::OK );
    REQUIRE( nValue == -0.125 );

    const string_parse_result_t exponentResult = StringParse_F64(
        StringView_FromCString( "+6.25e2" ),
        STRING_PARSE_FLAG_ALLOW_PLUS_SIGN,
        &nValue );
    REQUIRE( exponentResult.status == string_parse_status_t::OK );
    REQUIRE( exponentResult.cchConsumed == 7u );
    REQUIRE( nValue == 625.0 );
}

TEST_CASE( "StringParse_F64 applies whitespace and trailing-input policy",
           "[CypherCommon][Tier1][StringParse]" )
{
    f64 nValue = 0.0;

    const string_parse_result_t trimmedResult = StringParse_F64(
        StringView_FromCString( " \t1.5e2\r\n" ),
        STRING_PARSE_FLAG_TRIM_WHITESPACE,
        &nValue );
    REQUIRE( trimmedResult.status == string_parse_status_t::OK );
    REQUIRE( trimmedResult.cchConsumed == 9u );
    REQUIRE( nValue == 150.0 );

    nValue = 777.0;
    const string_parse_result_t trailingResult = StringParse_F64(
        StringView_FromCString( "12.5ms" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( trailingResult.status == string_parse_status_t::TRAILING_CHARACTERS );
    REQUIRE( trailingResult.cchConsumed == 4u );
    REQUIRE( trailingResult.iError == 4u );
    REQUIRE( nValue == 777.0 );

    const string_parse_result_t partialResult = StringParse_F64(
        StringView_FromCString( "12.5ms" ),
        STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS,
        &nValue );
    REQUIRE( partialResult.status == string_parse_status_t::OK );
    REQUIRE( partialResult.cchConsumed == 4u );
    REQUIRE( nValue == 12.5 );
}

TEST_CASE( "StringParse_F64 distinguishes overflow underflow and non-finite values",
           "[CypherCommon][Tier1][StringParse]" )
{
    f64 nValue = 777.0;

    const string_parse_result_t overflowResult = StringParse_F64(
        StringView_FromCString( "1e309" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( overflowResult.status == string_parse_status_t::NUMERIC_OVERFLOW );
    REQUIRE( overflowResult.cchConsumed == 5u );
    REQUIRE( nValue == 777.0 );

    const string_parse_result_t negativeOverflowResult = StringParse_F64(
        StringView_FromCString( "-1e309" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( negativeOverflowResult.status == string_parse_status_t::NUMERIC_UNDERFLOW );
    REQUIRE( negativeOverflowResult.cchConsumed == 6u );
    REQUIRE( nValue == 777.0 );

    const string_parse_result_t tinyResult = StringParse_F64(
        StringView_FromCString( "1e-9999" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( tinyResult.status == string_parse_status_t::NUMERIC_UNDERFLOW );
    REQUIRE( tinyResult.cchConsumed == 7u );
    REQUIRE( nValue == 777.0 );

    const string_parse_result_t rejectedInfinity = StringParse_F64(
        StringView_FromCString( "inf" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( rejectedInfinity.status == string_parse_status_t::NON_FINITE_VALUE );
    REQUIRE( nValue == 777.0 );

    const string_parse_result_t acceptedInfinity = StringParse_F64(
        StringView_FromCString( "-infinity" ),
        STRING_PARSE_FLAG_ALLOW_NON_FINITE_FLOAT,
        &nValue );
    REQUIRE( acceptedInfinity.status == string_parse_status_t::OK );
    REQUIRE( std::isinf( nValue ) );
    REQUIRE( nValue < 0.0 );

    const string_parse_result_t acceptedNan = StringParse_F64(
        StringView_FromCString( "nan" ),
        STRING_PARSE_FLAG_ALLOW_NON_FINITE_FLOAT,
        &nValue );
    REQUIRE( acceptedNan.status == string_parse_status_t::OK );
    REQUIRE( std::isnan( nValue ) );
}

TEST_CASE( "StringParse_F64 rejects malformed and invalid contracts",
           "[CypherCommon][Tier1][StringParse]" )
{
    f64 nValue = 777.0;

    REQUIRE( StringParse_F64(
        {}, STRING_PARSE_FLAG_NONE, &nValue ).status ==
        string_parse_status_t::EMPTY_INPUT );
    REQUIRE( StringParse_F64(
        StringView_FromCString( "." ), STRING_PARSE_FLAG_NONE, &nValue ).status ==
        string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( StringParse_F64(
        StringView_FromCString( "+1" ), STRING_PARSE_FLAG_NONE, &nValue ).status ==
        string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( StringParse_F64(
        StringView_FromCString( "+-1" ),
        STRING_PARSE_FLAG_ALLOW_PLUS_SIGN,
        &nValue ).status == string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( nValue == 777.0 );

    const string_view_t invalidView{ nullptr, 1u };
    g_stringParseAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringParseAssert );

    const string_parse_result_t invalidViewResult = StringParse_F64(
        invalidView, STRING_PARSE_FLAG_NONE, &nValue );
    const string_parse_result_t invalidOutputResult = StringParse_F64(
        StringView_FromCString( "1" ), STRING_PARSE_FLAG_NONE, nullptr );
    const string_parse_result_t invalidFlagsResult = StringParse_F64(
        StringView_FromCString( "1" ), CYPHER_BIT32( 31 ), &nValue );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( invalidViewResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( invalidOutputResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( invalidFlagsResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( g_stringParseAssertCount == 3u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "StringParse_F64 respects bounded views with embedded null bytes",
           "[CypherCommon][Tier1][StringParse]" )
{
    const char text[] = { '1', '.', '5', '\0', '9' };
    f64 nValue = 777.0;

    const string_parse_result_t rejectedResult = StringParse_F64(
        StringView_FromRange( text, sizeof( text ) ),
        STRING_PARSE_FLAG_NONE,
        &nValue );
    REQUIRE( rejectedResult.status == string_parse_status_t::TRAILING_CHARACTERS );
    REQUIRE( rejectedResult.iError == 3u );
    REQUIRE( nValue == 777.0 );

    const string_parse_result_t partialResult = StringParse_F64(
        StringView_FromRange( text, sizeof( text ) ),
        STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS,
        &nValue );
    REQUIRE( partialResult.status == string_parse_status_t::OK );
    REQUIRE( partialResult.cchConsumed == 3u );
    REQUIRE( nValue == 1.5 );
}

TEST_CASE( "StringParse_F32 parses directly and enforces f32 range",
           "[CypherCommon][Tier1][StringParse]" )
{
    f32 nValue = 0.0f;

    const string_parse_result_t validResult = StringParse_F32(
        StringView_FromCString( "1.25e2" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( validResult.status == string_parse_status_t::OK );
    REQUIRE( nValue == 125.0f );

    nValue = 777.0f;
    const string_parse_result_t overflowResult = StringParse_F32(
        StringView_FromCString( "3.5e38" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( overflowResult.status == string_parse_status_t::NUMERIC_OVERFLOW );
    REQUIRE( nValue == 777.0f );

    const string_parse_result_t negativeOverflowResult = StringParse_F32(
        StringView_FromCString( "-3.5e38" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( negativeOverflowResult.status == string_parse_status_t::NUMERIC_UNDERFLOW );
    REQUIRE( nValue == 777.0f );

    const string_parse_result_t tinyResult = StringParse_F32(
        StringView_FromCString( "1e-50" ), STRING_PARSE_FLAG_NONE, &nValue );
    REQUIRE( tinyResult.status == string_parse_status_t::NUMERIC_UNDERFLOW );
    REQUIRE( nValue == 777.0f );
}

TEST_CASE( "StringParse_Bool applies literal case and numeric policy",
           "[CypherCommon][Tier1][StringParse]" )
{
    bool_t bValue = CY_FALSE;

    REQUIRE( StringParse_Bool(
        StringView_FromCString( "true" ), STRING_PARSE_FLAG_NONE, &bValue ).status ==
        string_parse_status_t::OK );
    REQUIRE( bValue );

    REQUIRE( StringParse_Bool(
        StringView_FromCString( "false" ), STRING_PARSE_FLAG_NONE, &bValue ).status ==
        string_parse_status_t::OK );
    REQUIRE_FALSE( bValue );

    REQUIRE( StringParse_Bool(
        StringView_FromCString( " TRUE " ),
        STRING_PARSE_FLAG_TRIM_WHITESPACE |
            STRING_PARSE_FLAG_CASE_INSENSITIVE_BOOL,
        &bValue ).status == string_parse_status_t::OK );
    REQUIRE( bValue );

    REQUIRE( StringParse_Bool(
        StringView_FromCString( "0" ),
        STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL,
        &bValue ).status == string_parse_status_t::OK );
    REQUIRE_FALSE( bValue );

    REQUIRE( StringParse_Bool(
        StringView_FromCString( "1" ),
        STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL,
        &bValue ).status == string_parse_status_t::OK );
    REQUIRE( bValue );
}

TEST_CASE( "StringParse_Bool rejects disabled forms and preserves output",
           "[CypherCommon][Tier1][StringParse]" )
{
    bool_t bValue = CY_TRUE;

    REQUIRE( StringParse_Bool(
        StringView_FromCString( "TRUE" ), STRING_PARSE_FLAG_NONE, &bValue ).status ==
        string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( bValue );

    REQUIRE( StringParse_Bool(
        StringView_FromCString( "1" ), STRING_PARSE_FLAG_NONE, &bValue ).status ==
        string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( bValue );

    REQUIRE( StringParse_Bool(
        StringView_FromCString( "2" ),
        STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL,
        &bValue ).status == string_parse_status_t::INVALID_CHARACTER );
    REQUIRE( bValue );

    REQUIRE( StringParse_Bool(
        {}, STRING_PARSE_FLAG_NONE, &bValue ).status ==
        string_parse_status_t::EMPTY_INPUT );
    REQUIRE( bValue );
}

TEST_CASE( "StringParse_Bool reports or permits trailing input",
           "[CypherCommon][Tier1][StringParse]" )
{
    bool_t bValue = CY_FALSE;

    const string_parse_result_t rejectedResult = StringParse_Bool(
        StringView_FromCString( "trueish" ), STRING_PARSE_FLAG_NONE, &bValue );
    REQUIRE( rejectedResult.status == string_parse_status_t::TRAILING_CHARACTERS );
    REQUIRE( rejectedResult.cchConsumed == 4u );
    REQUIRE( rejectedResult.iError == 4u );
    REQUIRE_FALSE( bValue );

    const string_parse_result_t partialResult = StringParse_Bool(
        StringView_FromCString( "trueish" ),
        STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS,
        &bValue );
    REQUIRE( partialResult.status == string_parse_status_t::OK );
    REQUIRE( partialResult.cchConsumed == 4u );
    REQUIRE( bValue );
}

TEST_CASE( "StringParse_Bool rejects invalid contracts",
           "[CypherCommon][Tier1][StringParse]" )
{
    const string_view_t invalidView{ nullptr, 1u };
    bool_t bValue = CY_TRUE;

    g_stringParseAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringParseAssert );

    const string_parse_result_t invalidViewResult = StringParse_Bool(
        invalidView, STRING_PARSE_FLAG_NONE, &bValue );
    const string_parse_result_t invalidOutputResult = StringParse_Bool(
        StringView_FromCString( "true" ), STRING_PARSE_FLAG_NONE, nullptr );
    const string_parse_result_t invalidFlagsResult = StringParse_Bool(
        StringView_FromCString( "true" ), CYPHER_BIT32( 31 ), &bValue );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( invalidViewResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( invalidOutputResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( invalidFlagsResult.status == string_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( bValue );
    REQUIRE( g_stringParseAssertCount == 3u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
