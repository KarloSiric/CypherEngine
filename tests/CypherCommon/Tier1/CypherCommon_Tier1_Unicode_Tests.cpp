//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Unicode_Tests.cpp
//  Purpose: Tests strict UTF validation, encoding, and transcoding.
//  Details: These tests protect Unicode scalar limits, malformed UTF rejection,
//           replacement policy, surrogate pairs, exact sizing, and terminators.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Unicode.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

string_view_t ByteText( const byte *pData, usize cbData ) noexcept
{
    return { reinterpret_cast<const char *>( pData ), cbData };
}

} // namespace

TEST_CASE( "Unicode identifies scalar values and surrogate code points",
           "[CypherCommon][Tier1][Unicode]" )
{
    REQUIRE( Unicode_IsScalarValue( 0u ) );
    REQUIRE( Unicode_IsScalarValue( CY_UNICODE_MAX ) );
    REQUIRE_FALSE( Unicode_IsScalarValue( CY_UNICODE_MAX + 1u ) );
    REQUIRE( Unicode_IsSurrogate( 0xD800u ) );
    REQUIRE( Unicode_IsSurrogate( 0xDFFFu ) );
    REQUIRE_FALSE( Unicode_IsScalarValue( 0xD800u ) );
}

TEST_CASE( "Unicode decodes and encodes UTF-8 boundary values",
           "[CypherCommon][Tier1][Unicode]" )
{
    const byte encoded[]{
        0x24u,
        0xC2u, 0xA2u,
        0xE2u, 0x82u, 0xACu,
        0xF0u, 0x9Fu, 0x98u, 0x80u
    };
    const unicode_result_t valid = Unicode_ValidateUtf8(
        ByteText( encoded, sizeof( encoded ) ) );
    REQUIRE( valid.status == unicode_status_t::OK );
    REQUIRE( valid.nInputConsumed == sizeof( encoded ) );

    const unicode_result_t count = Unicode_CountUtf8CodePoints(
        ByteText( encoded, sizeof( encoded ) ) );
    REQUIRE( count.status == unicode_status_t::OK );
    REQUIRE( count.nOutputWritten == 4u );

    unicode_code_point_t codePoint = 0u;
    unicode_result_t decoded = Unicode_DecodeUtf8(
        ByteText( encoded + 6u, 4u ),
        &codePoint );
    REQUIRE( decoded.status == unicode_status_t::OK );
    REQUIRE( decoded.nInputConsumed == 4u );
    REQUIRE( codePoint == 0x1F600u );

    char output[4]{};
    const unicode_result_t result = Unicode_EncodeUtf8(
        codePoint,
        output,
        sizeof( output ) );
    REQUIRE( result.status == unicode_status_t::OK );
    REQUIRE( Cy_MemCompare( output, encoded + 6u, 4u ) == 0 );
}

TEST_CASE( "Unicode rejects malformed overlong surrogate and truncated UTF-8",
           "[CypherCommon][Tier1][Unicode]" )
{
    const byte overlong[]{ 0xC0u, 0xAFu };
    REQUIRE( Unicode_ValidateUtf8(
        ByteText( overlong, sizeof( overlong ) ) ).status ==
        unicode_status_t::INVALID_SEQUENCE );

    const byte surrogate[]{ 0xEDu, 0xA0u, 0x80u };
    REQUIRE( Unicode_ValidateUtf8(
        ByteText( surrogate, sizeof( surrogate ) ) ).status ==
        unicode_status_t::INVALID_CODE_POINT );

    const byte aboveMaximum[]{ 0xF4u, 0x90u, 0x80u, 0x80u };
    REQUIRE( Unicode_ValidateUtf8(
        ByteText( aboveMaximum, sizeof( aboveMaximum ) ) ).status ==
        unicode_status_t::INVALID_CODE_POINT );

    const byte truncated[]{ 0xE2u, 0x82u };
    REQUIRE( Unicode_ValidateUtf8(
        ByteText( truncated, sizeof( truncated ) ) ).status ==
        unicode_status_t::TRUNCATED_SEQUENCE );
}

TEST_CASE( "Unicode transcodes UTF-8 and UTF-16 including surrogate pairs",
           "[CypherCommon][Tier1][Unicode]" )
{
    const byte utf8[]{ 'A', 0xF0u, 0x9Fu, 0x98u, 0x80u };
    utf16_unit_t utf16[4]{};
    const unicode_result_t toUtf16 = Unicode_Utf8ToUtf16(
        ByteText( utf8, sizeof( utf8 ) ),
        UNICODE_FLAG_WRITE_TERMINATOR,
        Span_FromArray( utf16 ) );
    REQUIRE( toUtf16.status == unicode_status_t::OK );
    REQUIRE( toUtf16.nOutputWritten == 3u );
    REQUIRE( utf16[0] == static_cast<utf16_unit_t>( 'A' ) );
    REQUIRE( utf16[1] == 0xD83Du );
    REQUIRE( utf16[2] == 0xDE00u );
    REQUIRE( utf16[3] == 0u );

    char roundTrip[sizeof( utf8 ) + 1u]{};
    const unicode_result_t toUtf8 = Unicode_Utf16ToUtf8(
        { utf16, 3u },
        UNICODE_FLAG_WRITE_TERMINATOR,
        roundTrip,
        sizeof( roundTrip ) );
    REQUIRE( toUtf8.status == unicode_status_t::OK );
    REQUIRE( toUtf8.nOutputWritten == sizeof( utf8 ) );
    REQUIRE( Cy_MemCompare( roundTrip, utf8, sizeof( utf8 ) ) == 0 );
    REQUIRE( roundTrip[sizeof( utf8 )] == '\0' );
}

TEST_CASE( "Unicode reports count-only output and never writes partial code points",
           "[CypherCommon][Tier1][Unicode]" )
{
    const byte utf8[]{ 0xF0u, 0x9Fu, 0x98u, 0x80u };
    const unicode_result_t measured = Unicode_Utf8ToUtf16(
        ByteText( utf8, sizeof( utf8 ) ),
        UNICODE_FLAG_WRITE_TERMINATOR,
        {} );
    REQUIRE( measured.status == unicode_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.nOutputWritten == 0u );
    REQUIRE( measured.nOutputRequired == 2u );

    utf16_unit_t tooSmall[2]{ 0xAAAAu, 0xBBBBu };
    const unicode_result_t truncated = Unicode_Utf8ToUtf16(
        ByteText( utf8, sizeof( utf8 ) ),
        UNICODE_FLAG_WRITE_TERMINATOR,
        Span_FromArray( tooSmall ) );
    REQUIRE( truncated.status == unicode_status_t::OUTPUT_TRUNCATED );
    REQUIRE( truncated.nOutputWritten == 0u );
    REQUIRE( tooSmall[0] == 0u );
    REQUIRE( tooSmall[1] == 0xBBBBu );
}

TEST_CASE( "Unicode replacement and reject-NUL policies are explicit",
           "[CypherCommon][Tier1][Unicode]" )
{
    const byte malformed[]{ 'A', 0xFFu, 'B' };
    unicode_code_point_t output[4]{};
    const unicode_result_t replaced = Unicode_Utf8ToUtf32(
        ByteText( malformed, sizeof( malformed ) ),
        UNICODE_FLAG_REPLACE_INVALID | UNICODE_FLAG_WRITE_TERMINATOR,
        Span_FromArray( output ) );
    REQUIRE( replaced.status == unicode_status_t::OK );
    REQUIRE( replaced.nOutputWritten == 3u );
    REQUIRE( output[0] == static_cast<unicode_code_point_t>( 'A' ) );
    REQUIRE( output[1] == CY_UNICODE_REPLACEMENT );
    REQUIRE( output[2] == static_cast<unicode_code_point_t>( 'B' ) );
    REQUIRE( output[3] == 0u );

    const char withNul[]{ 'A', '\0', 'B' };
    REQUIRE( Unicode_Utf8ToUtf32(
        { withNul, sizeof( withNul ) },
        UNICODE_FLAG_REJECT_NUL,
        Span_FromArray( output ) ).status ==
        unicode_status_t::INVALID_CODE_POINT );
}

TEST_CASE( "Unicode UTF-32 conversion rejects or replaces invalid scalars",
           "[CypherCommon][Tier1][Unicode]" )
{
    const unicode_code_point_t source[]{ 'A', 0xD800u, 'B' };
    char output[16]{};
    REQUIRE( Unicode_Utf32ToUtf8(
        Span_FromArray( source ),
        UNICODE_FLAG_NONE,
        output,
        sizeof( output ) ).status ==
        unicode_status_t::INVALID_CODE_POINT );

    const unicode_result_t replaced = Unicode_Utf32ToUtf8(
        Span_FromArray( source ),
        UNICODE_FLAG_REPLACE_INVALID | UNICODE_FLAG_WRITE_TERMINATOR,
        output,
        sizeof( output ) );
    REQUIRE( replaced.status == unicode_status_t::OK );
    REQUIRE( replaced.nOutputWritten == 5u );
    REQUIRE( static_cast<byte>( output[1] ) == 0xEFu );
    REQUIRE( static_cast<byte>( output[2] ) == 0xBFu );
    REQUIRE( static_cast<byte>( output[3] ) == 0xBDu );
}
