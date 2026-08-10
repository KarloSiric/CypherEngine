//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringUrl_Tests.cpp
//  Purpose: Tests bounded URI parsing and percent conversion.
//  Details: Borrowed components, IPv6 authority, invalid syntax, binary round trips,
//           count-only output, and truncation are covered without network access.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringUrl.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "StringUrl parses hierarchical URI components",
           "[CypherCommon][Tier1][StringUrl]" )
{
    url_parts_t parts{};
    const url_result_t result = StringUrl_Parse(
        StringView_FromCString(
            "https://user@example.com:8443/assets/file?revision=7#mesh" ),
        &parts );
    REQUIRE( result.status == url_status_t::OK );
    REQUIRE( StringView_Equals( parts.scheme, StringView_FromCString( "https" ) ) );
    REQUIRE( StringView_Equals( parts.userInfo, StringView_FromCString( "user" ) ) );
    REQUIRE( StringUrl_HostEquals( parts, StringView_FromCString( "EXAMPLE.COM" ) ) );
    REQUIRE( StringView_Equals( parts.port, StringView_FromCString( "8443" ) ) );
    REQUIRE( StringView_Equals( parts.path, StringView_FromCString( "/assets/file" ) ) );
    REQUIRE( StringView_Equals( parts.query, StringView_FromCString( "revision=7" ) ) );
    REQUIRE( StringView_Equals( parts.fragment, StringView_FromCString( "mesh" ) ) );
}

TEST_CASE( "StringUrl parses bracketed IPv6 hosts",
           "[CypherCommon][Tier1][StringUrl]" )
{
    url_parts_t parts{};
    REQUIRE( StringUrl_Parse(
        StringView_FromCString( "cypher://[2001:db8::1]:27015/session" ),
        &parts ).status == url_status_t::OK );
    REQUIRE( StringView_Equals( parts.host, StringView_FromCString( "2001:db8::1" ) ) );
    REQUIRE( StringView_Equals( parts.port, StringView_FromCString( "27015" ) ) );
}

TEST_CASE( "StringUrl rejects malformed syntax",
           "[CypherCommon][Tier1][StringUrl]" )
{
    url_parts_t parts{};
    REQUIRE( StringUrl_Parse(
        StringView_FromCString( "1http://host" ),
        &parts ).status == url_status_t::INVALID_URL );
    REQUIRE( StringUrl_Parse(
        StringView_FromCString( "https://host:abc/path" ),
        &parts ).status == url_status_t::INVALID_URL );
    REQUIRE( StringUrl_Parse(
        StringView_FromCString( "https://host/%zz" ),
        &parts ).status == url_status_t::INVALID_URL );
    REQUIRE( StringUrl_Parse(
        StringView_FromCString( "https://[2001:db8::1/path" ),
        &parts ).status == url_status_t::INVALID_URL );
}

TEST_CASE( "StringUrl percent conversion round trips binary bytes",
           "[CypherCommon][Tier1][StringUrl]" )
{
    const byte source[]{ 'a', ' ', '/', 0x00u, 0xFFu };
    char encoded[64]{};
    const url_result_t encodeResult = StringUrl_PercentEncode(
        Span_FromArray( source ),
        URL_ENCODE_FLAG_SPACE_AS_PLUS |
            URL_ENCODE_FLAG_PRESERVE_SLASH |
            URL_ENCODE_FLAG_UPPERCASE_HEX,
        encoded,
        sizeof( encoded ) );
    REQUIRE( encodeResult.status == url_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( encoded ),
        StringView_FromCString( "a+/%00%FF" ) ) );

    byte decoded[sizeof( source )]{};
    const url_result_t decodeResult = StringUrl_PercentDecode(
        StringView_FromCString( encoded ),
        URL_DECODE_FLAG_PLUS_AS_SPACE,
        Span_FromArray( decoded ) );
    REQUIRE( decodeResult.status == url_status_t::OK );
    REQUIRE( Cy_MemCompare( source, decoded, sizeof( source ) ) == 0 );

    REQUIRE( StringUrl_PercentDecode(
        StringView_FromCString( "%00" ),
        URL_DECODE_FLAG_REJECT_NUL,
        Span_FromArray( decoded ) ).status == url_status_t::INVALID_URL );
}

TEST_CASE( "StringUrl conversion reports complete required sizes",
           "[CypherCommon][Tier1][StringUrl]" )
{
    const byte source[]{ 0xFFu, 'a' };
    const url_result_t measured = StringUrl_PercentEncode(
        Span_FromArray( source ),
        URL_ENCODE_FLAG_NONE,
        nullptr,
        0u );
    REQUIRE( measured.status == url_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.cbRequired == 4u );

    char output[3]{};
    const url_result_t truncated = StringUrl_PercentEncode(
        Span_FromArray( source ),
        URL_ENCODE_FLAG_NONE,
        output,
        sizeof( output ) );
    REQUIRE( truncated.status == url_status_t::OUTPUT_TRUNCATED );
    REQUIRE( output[0] == 'a' );
    REQUIRE( output[1] == '\0' );
    REQUIRE( truncated.cbRequired == 4u );
}
