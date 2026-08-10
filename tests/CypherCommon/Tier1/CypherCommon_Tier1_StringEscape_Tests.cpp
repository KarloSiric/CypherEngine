//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringEscape_Tests.cpp
//  Purpose: Tests bounded Unicode-aware string escaping.
//  Details: These tests protect JSON/Cypher grammar differences, non-ASCII escapes,
//           strict surrogate handling, round trips, sizing, and atomic sequence writes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringEscape.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

constexpr flags32_t TEST_ESCAPE_FLAGS =
    STRING_ESCAPE_FLAG_QUOTES |
    STRING_ESCAPE_FLAG_BACKSLASH |
    STRING_ESCAPE_FLAG_CONTROL_CHARS |
    STRING_ESCAPE_FLAG_NON_ASCII |
    STRING_ESCAPE_FLAG_PATH_SLASHES;

} // namespace

TEST_CASE( "StringEscape emits deterministic JSON escapes",
           "[CypherCommon][Tier1][StringEscape]" )
{
    const char source[]{
        '"', '\n',
        static_cast<char>( 0xC3u ), static_cast<char>( 0xA9u ),
        static_cast<char>( 0xF0u ), static_cast<char>( 0x9Fu ),
        static_cast<char>( 0x98u ), static_cast<char>( 0x80u )
    };
    char encoded[64]{};
    const string_escape_result_t result = StringEscape_Encode(
        { source, sizeof( source ) },
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS,
        encoded,
        sizeof( encoded ) );
    REQUIRE( result.status == string_escape_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( encoded ),
        StringView_FromCString( "\\\"\\n\\u00e9\\ud83d\\ude00" ) ) );
}

TEST_CASE( "StringEscape Cypher style supports byte and full Unicode escapes",
           "[CypherCommon][Tier1][StringEscape]" )
{
    const char source[]{
        0x01,
        static_cast<char>( 0xC3u ), static_cast<char>( 0xA9u ),
        static_cast<char>( 0xF0u ), static_cast<char>( 0x9Fu ),
        static_cast<char>( 0x98u ), static_cast<char>( 0x80u )
    };
    char encoded[64]{};
    const string_escape_result_t result = StringEscape_Encode(
        { source, sizeof( source ) },
        string_escape_style_t::CYPHER,
        TEST_ESCAPE_FLAGS,
        encoded,
        sizeof( encoded ) );
    REQUIRE( result.status == string_escape_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( encoded ),
        StringView_FromCString( "\\x01\\u00e9\\U0001f600" ) ) );
}

TEST_CASE( "StringEscape JSON round trip preserves UTF-8 text",
           "[CypherCommon][Tier1][StringEscape]" )
{
    const char source[]{
        'a', '"', '\\', '/', '\t',
        static_cast<char>( 0xC3u ), static_cast<char>( 0xA9u )
    };
    char encoded[128]{};
    const string_escape_result_t encodeResult = StringEscape_Encode(
        { source, sizeof( source ) },
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS,
        encoded,
        sizeof( encoded ) );
    REQUIRE( encodeResult.status == string_escape_status_t::OK );
    REQUIRE( StringEscape_NeedsEscaping(
        { source, sizeof( source ) },
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS ) );

    char decoded[sizeof( source ) + 1u]{};
    const string_escape_result_t decodeResult = StringEscape_Decode(
        StringView_FromCString( encoded ),
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS,
        decoded,
        sizeof( decoded ) );
    REQUIRE( decodeResult.status == string_escape_status_t::OK );
    REQUIRE( decodeResult.cchWritten == sizeof( source ) );
    REQUIRE( Cy_MemCompare( decoded, source, sizeof( source ) ) == 0 );
}

TEST_CASE( "StringEscape rejects grammar and Unicode violations",
           "[CypherCommon][Tier1][StringEscape]" )
{
    char output[32]{};
    REQUIRE( StringEscape_Decode(
        StringView_FromCString( "\\x41" ),
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS,
        output,
        sizeof( output ) ).status == string_escape_status_t::INVALID_ESCAPE );
    REQUIRE( StringEscape_Decode(
        StringView_FromCString( "\\ud800" ),
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS,
        output,
        sizeof( output ) ).status == string_escape_status_t::INVALID_CODE_POINT );
    REQUIRE( StringEscape_Decode(
        StringView_FromCString( "\\ud800\\u0041" ),
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS,
        output,
        sizeof( output ) ).status == string_escape_status_t::INVALID_CODE_POINT );

    const char malformed[]{ static_cast<char>( 0xFFu ) };
    REQUIRE( StringEscape_Encode(
        { malformed, sizeof( malformed ) },
        string_escape_style_t::CYPHER,
        TEST_ESCAPE_FLAGS,
        output,
        sizeof( output ) ).status == string_escape_status_t::INVALID_CODE_POINT );
}

TEST_CASE( "StringEscape measures output and does not split escape sequences",
           "[CypherCommon][Tier1][StringEscape]" )
{
    const string_view_t source = StringView_FromCString( "A\nB" );
    const string_escape_result_t measured = StringEscape_Encode(
        source,
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS,
        nullptr,
        0u );
    REQUIRE( measured.status == string_escape_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.cchWritten == 0u );
    REQUIRE( measured.cchRequired == 4u );

    char output[3]{};
    const string_escape_result_t truncated = StringEscape_Encode(
        source,
        string_escape_style_t::JSON,
        TEST_ESCAPE_FLAGS,
        output,
        sizeof( output ) );
    REQUIRE( truncated.status == string_escape_status_t::OUTPUT_TRUNCATED );
    REQUIRE( truncated.cchWritten == 2u );
    REQUIRE( output[0] == 'A' );
    REQUIRE( output[1] == 'B' );
    REQUIRE( output[2] == '\0' );
}
