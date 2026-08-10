//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringHtml_Tests.cpp
//  Purpose: Tests basic bounded HTML text helpers.
//  Details: Named and numeric entities, UTF-8, malformed input, quoted tag text,
//           whitespace policy, line preservation, sizing, and truncation are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringHtml.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "StringHtml entity conversion preserves Unicode",
           "[CypherCommon][Tier1][StringHtml]" )
{
    const string_view_t source = StringView_FromCString( "<tag title=\"x\">caf\xC3\xA9 & tea</tag>" );
    char encoded[128]{};
    const html_text_result_t encodeResult = StringHtml_EncodeEntities(
        source,
        HTML_TEXT_FLAG_ENCODE_QUOTES,
        encoded,
        sizeof( encoded ) );
    REQUIRE( encodeResult.status == html_text_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( encoded ),
        StringView_FromCString(
            "&lt;tag title=&quot;x&quot;&gt;caf\xC3\xA9 &amp; tea&lt;/tag&gt;" ) ) );

    char decoded[128]{};
    const html_text_result_t decodeResult = StringHtml_DecodeEntities(
        StringView_FromCString( encoded ),
        HTML_TEXT_FLAG_DECODE_NUMERIC,
        decoded,
        sizeof( decoded ) );
    REQUIRE( decodeResult.status == html_text_status_t::OK );
    REQUIRE( StringView_Equals( StringView_FromCString( decoded ), source ) );
}

TEST_CASE( "StringHtml decodes validated numeric scalar entities",
           "[CypherCommon][Tier1][StringHtml]" )
{
    char output[32]{};
    REQUIRE( StringHtml_DecodeEntities(
        StringView_FromCString( "&#65;&#x1F600;" ),
        HTML_TEXT_FLAG_DECODE_NUMERIC,
        output,
        sizeof( output ) ).status == html_text_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "A\xF0\x9F\x98\x80" ) ) );

    REQUIRE( StringHtml_DecodeEntities(
        StringView_FromCString( "&#xD800;" ),
        HTML_TEXT_FLAG_DECODE_NUMERIC,
        output,
        sizeof( output ) ).status == html_text_status_t::INVALID_CODE_POINT );
    REQUIRE( StringHtml_DecodeEntities(
        StringView_FromCString( "&unknown;" ),
        HTML_TEXT_FLAG_NONE,
        output,
        sizeof( output ) ).status == html_text_status_t::INVALID_ENTITY );
}

TEST_CASE( "StringHtml strips simple tags and preserves layout policy",
           "[CypherCommon][Tier1][StringHtml]" )
{
    char output[128]{};
    const html_text_result_t result = StringHtml_StripTags(
        StringView_FromCString( "<p class=\"a>b\">Hello   world</p><br>Next" ),
        HTML_TEXT_FLAG_PRESERVE_LINE_BREAKS |
            HTML_TEXT_FLAG_COLLAPSE_WHITESPACE,
        output,
        sizeof( output ) );
    REQUIRE( result.status == html_text_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "\nHello world\nNext" ) ) );

    REQUIRE( StringHtml_StripTags(
        StringView_FromCString( "text <broken" ),
        HTML_TEXT_FLAG_NONE,
        output,
        sizeof( output ) ).status == html_text_status_t::UNTERMINATED_TAG );
}

TEST_CASE( "StringHtml reports complete output requirements",
           "[CypherCommon][Tier1][StringHtml]" )
{
    const html_text_result_t measured = StringHtml_EncodeEntities(
        StringView_FromCString( "<&" ),
        HTML_TEXT_FLAG_NONE,
        nullptr,
        0u );
    REQUIRE( measured.status == html_text_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.cchRequired == 9u );

    char output[5]{};
    const html_text_result_t truncated = StringHtml_EncodeEntities(
        StringView_FromCString( "<&" ),
        HTML_TEXT_FLAG_NONE,
        output,
        sizeof( output ) );
    REQUIRE( truncated.status == html_text_status_t::OUTPUT_TRUNCATED );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "&lt;" ) ) );
}
