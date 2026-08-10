//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringConvert_Tests.cpp
//  Purpose: Tests deterministic primitive and binary text conversion.
//  Details: These tests protect integer limits, formatting policy, required-length
//           reporting, float styles, strict hexadecimal validation, and truncation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringConvert.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "StringConvert formats integer limits and policies",
           "[CypherCommon][Tier1][StringConvert]" )
{
    char output[96]{};
    const string_integer_format_t decimal{};
    string_convert_result_t result = StringConvert_I64(
        CY_I64_MIN,
        decimal,
        output,
        sizeof( output ) );
    REQUIRE( result.status == string_convert_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "-9223372036854775808" ) ) );

    const string_integer_format_t hexadecimal{
        16u,
        8u,
        STRING_INTEGER_FORMAT_FLAG_UPPERCASE |
        STRING_INTEGER_FORMAT_FLAG_PREFIX |
        STRING_INTEGER_FORMAT_FLAG_PLUS_SIGN
    };
    result = StringConvert_U32( 0x12ABu, hexadecimal, output, sizeof( output ) );
    REQUIRE( result.status == string_convert_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "+0X000012AB" ) ) );

    const string_integer_format_t binary{
        2u,
        1u,
        STRING_INTEGER_FORMAT_FLAG_PREFIX
    };
    result = StringConvert_U64( 10u, binary, output, sizeof( output ) );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "0b1010" ) ) );
}

TEST_CASE( "StringConvert reports count-only and truncation exactly",
           "[CypherCommon][Tier1][StringConvert]" )
{
    const string_convert_result_t measured = StringConvert_U64(
        CY_U64_MAX,
        {},
        nullptr,
        0u );
    REQUIRE( measured.status == string_convert_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.cchWritten == 0u );
    REQUIRE( measured.cchRequired == 20u );

    char output[5]{};
    const string_convert_result_t truncated = StringConvert_U64(
        123456u,
        {},
        output,
        sizeof( output ) );
    REQUIRE( truncated.status == string_convert_status_t::OUTPUT_TRUNCATED );
    REQUIRE( truncated.cchWritten == 4u );
    REQUIRE( truncated.cchRequired == 6u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "1234" ) ) );
}

TEST_CASE( "StringConvert formats floats without locale dependence",
           "[CypherCommon][Tier1][StringConvert]" )
{
    char output[128]{};
    const string_float_format_t fixed{
        string_float_style_t::FIXED,
        4u,
        STRING_FLOAT_FORMAT_FLAG_PLUS_SIGN |
        STRING_FLOAT_FORMAT_FLAG_TRIM_TRAILING_ZERO
    };
    string_convert_result_t result = StringConvert_F64(
        12.5,
        fixed,
        output,
        sizeof( output ) );
    REQUIRE( result.status == string_convert_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "+12.5" ) ) );

    const string_float_format_t scientific{
        string_float_style_t::SCIENTIFIC,
        2u,
        STRING_FLOAT_FORMAT_FLAG_UPPERCASE
    };
    result = StringConvert_F32( 125.0f, scientific, output, sizeof( output ) );
    REQUIRE( result.status == string_convert_status_t::OK );
    REQUIRE( StringView_Contains(
        StringView_FromCString( output ),
        StringView_FromCString( "E" ) ) );
}

TEST_CASE( "StringConvert binary hexadecimal conversion is strict",
           "[CypherCommon][Tier1][StringConvert]" )
{
    const byte source[]{ 0x00u, 0x12u, 0xABu, 0xFFu };
    char text[9]{};
    string_convert_result_t encoded = StringConvert_BinaryToHex(
        source,
        sizeof( source ),
        CY_FALSE,
        text,
        sizeof( text ) );
    REQUIRE( encoded.status == string_convert_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( text ),
        StringView_FromCString( "0012abff" ) ) );

    byte decoded[sizeof( source )]{};
    const string_convert_result_t result = StringConvert_HexToBinary(
        StringView_FromCString( "0012ABff" ),
        decoded,
        sizeof( decoded ) );
    REQUIRE( result.status == string_convert_status_t::OK );
    REQUIRE( result.cchWritten == sizeof( source ) );
    REQUIRE( result.cchConsumed == 8u );
    REQUIRE( Cy_MemCompare( decoded, source, sizeof( source ) ) == 0 );

    byte unchanged[]{ 1u, 2u };
    const string_convert_result_t invalid = StringConvert_HexToBinary(
        StringView_FromCString( "00xz" ),
        unchanged,
        sizeof( unchanged ) );
    REQUIRE( invalid.status == string_convert_status_t::INVALID_TEXT );
    REQUIRE( unchanged[0] == 1u );
    REQUIRE( unchanged[1] == 2u );
}

TEST_CASE( "StringConvert formats booleans and rejects invalid integer formats",
           "[CypherCommon][Tier1][StringConvert]" )
{
    char output[16]{};
    REQUIRE( StringConvert_Bool(
        CY_TRUE,
        output,
        sizeof( output ) ).status == string_convert_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "true" ) ) );

    const string_integer_format_t invalidBase{ 1u, 1u, 0u };
    REQUIRE( StringConvert_U64(
        10u,
        invalidBase,
        output,
        sizeof( output ) ).status == string_convert_status_t::INVALID_BASE );
    REQUIRE( output[0] == '\0' );
}
