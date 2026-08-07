//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringSplit_Tests.cpp
//  Purpose: Tests Tier1 StringSplit behavior.
//  Details: These tests protect bounded token views, empty-field policy,
//           output truncation reporting, delimiter matching, and visitor cancellation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_StringSplit.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

constexpr usize CY_STRING_SPLIT_TEST_TOKEN_CAPACITY = 8u;

u32 g_stringSplitAssertCount = 0u;

assert_action_t CaptureStringSplitAssert( const assert_info_t & ) noexcept
{
    ++g_stringSplitAssertCount;
    return assert_action_t::Continue;
}

bool_t ViewEquals( string_view_t view, const char *pExpected ) noexcept
{
    return StringView_Equals( view, StringView_FromCString( pExpected ) );
}

struct visit_capture_t {
    string_view_t tokens[CY_STRING_SPLIT_TEST_TOKEN_CAPACITY]{};
    usize tokenIndices[CY_STRING_SPLIT_TEST_TOKEN_CAPACITY]{};
    usize cTokensCaptured{ 0u };
    usize cStopAfter{ CY_INVALID_SIZE };
};

bool_t CaptureVisitedToken(
    string_view_t token,
    usize iToken,
    void *pUserData ) noexcept
{
    visit_capture_t *pCapture = static_cast<visit_capture_t *>( pUserData );
    if ( pCapture == nullptr ) {
        return CY_FALSE;
    }

    if ( pCapture->cTokensCaptured < CY_STRING_SPLIT_TEST_TOKEN_CAPACITY ) {
        pCapture->tokens[pCapture->cTokensCaptured] = token;
        pCapture->tokenIndices[pCapture->cTokensCaptured] = iToken;
    }

    ++pCapture->cTokensCaptured;
    return pCapture->cTokensCaptured < pCapture->cStopAfter;
}

} // namespace

TEST_CASE( "StringSplit_ByChar preserves leading consecutive and trailing empty fields",
           "[CypherCommon][Tier1][StringSplit]" )
{
    const string_view_t source = StringView_FromCString( ",alpha,,beta," );
    string_view_t tokens[CY_STRING_SPLIT_TEST_TOKEN_CAPACITY]{};

    const string_split_result_t result = StringSplit_ByChar(
        source,
        ',',
        STRING_SPLIT_FLAG_NONE,
        tokens,
        CY_STRING_SPLIT_TEST_TOKEN_CAPACITY );

    REQUIRE( result.cTokensWritten == 5u );
    REQUIRE( result.cTokensRequired == 5u );
    REQUIRE( StringView_IsEmpty( tokens[0] ) );
    REQUIRE( ViewEquals( tokens[1], "alpha" ) );
    REQUIRE( StringView_IsEmpty( tokens[2] ) );
    REQUIRE( ViewEquals( tokens[3], "beta" ) );
    REQUIRE( StringView_IsEmpty( tokens[4] ) );
}

TEST_CASE( "StringSplit trims fields before applying the skip-empty policy",
           "[CypherCommon][Tier1][StringSplit]" )
{
    const string_view_t source = StringView_FromCString( "  alpha , \t , beta  " );
    string_view_t tokens[CY_STRING_SPLIT_TEST_TOKEN_CAPACITY]{};

    const flags32_t flags =
        STRING_SPLIT_FLAG_TRIM_WHITESPACE |
        STRING_SPLIT_FLAG_SKIP_EMPTY;

    const string_split_result_t result = StringSplit_ByChar(
        source,
        ',',
        flags,
        tokens,
        CY_STRING_SPLIT_TEST_TOKEN_CAPACITY );

    REQUIRE( result.cTokensWritten == 2u );
    REQUIRE( result.cTokensRequired == 2u );
    REQUIRE( ViewEquals( tokens[0], "alpha" ) );
    REQUIRE( ViewEquals( tokens[1], "beta" ) );
}

TEST_CASE( "StringSplit reports the full required count when output is truncated",
           "[CypherCommon][Tier1][StringSplit]" )
{
    const string_view_t source = StringView_FromCString( "a,b,c,d" );
    string_view_t tokens[2]{};

    const string_split_result_t truncated = StringSplit_ByChar(
        source,
        ',',
        STRING_SPLIT_FLAG_NONE,
        tokens,
        2u );

    REQUIRE( truncated.cTokensWritten == 2u );
    REQUIRE( truncated.cTokensRequired == 4u );
    REQUIRE( ViewEquals( tokens[0], "a" ) );
    REQUIRE( ViewEquals( tokens[1], "b" ) );

    const string_split_result_t countOnly = StringSplit_ByChar(
        source,
        ',',
        STRING_SPLIT_FLAG_NONE,
        nullptr,
        0u );

    REQUIRE( countOnly.cTokensWritten == 0u );
    REQUIRE( countOnly.cTokensRequired == 4u );
}

TEST_CASE( "StringSplit handles empty bounded input according to its field policy",
           "[CypherCommon][Tier1][StringSplit]" )
{
    string_view_t token{};

    const string_split_result_t preserved = StringSplit_ByChar(
        {},
        ',',
        STRING_SPLIT_FLAG_NONE,
        &token,
        1u );

    REQUIRE( preserved.cTokensWritten == 1u );
    REQUIRE( preserved.cTokensRequired == 1u );
    REQUIRE( StringView_IsEmpty( token ) );
    REQUIRE( token.pData == nullptr );

    const string_split_result_t skipped = StringSplit_ByChar(
        {},
        ',',
        STRING_SPLIT_FLAG_SKIP_EMPTY,
        &token,
        1u );

    REQUIRE( skipped.cTokensWritten == 0u );
    REQUIRE( skipped.cTokensRequired == 0u );
}

TEST_CASE( "StringSplit_ByChar supports embedded null bytes",
           "[CypherCommon][Tier1][StringSplit]" )
{
    const char data[] = { 'a', '\0', 'b', '\0', 'c' };
    const string_view_t source = StringView_FromRange( data, sizeof( data ) );
    string_view_t tokens[3]{};

    const string_split_result_t result = StringSplit_ByChar(
        source,
        '\0',
        STRING_SPLIT_FLAG_NONE,
        tokens,
        3u );

    REQUIRE( result.cTokensWritten == 3u );
    REQUIRE( result.cTokensRequired == 3u );
    REQUIRE( tokens[0].pData == data );
    REQUIRE( tokens[0].cchLength == 1u );
    REQUIRE( tokens[1].pData == data + 2u );
    REQUIRE( tokens[1].cchLength == 1u );
    REQUIRE( tokens[2].pData == data + 4u );
    REQUIRE( tokens[2].cchLength == 1u );
}

TEST_CASE( "StringSplit_BySet recognizes any member of the delimiter set",
           "[CypherCommon][Tier1][StringSplit]" )
{
    character_set_t delimiters{};
    CharacterSet_Add( &delimiters, ',' );
    CharacterSet_Add( &delimiters, ';' );
    CharacterSet_Add( &delimiters, '|' );

    const string_view_t source = StringView_FromCString( "one,two;three|four" );
    string_view_t tokens[4]{};

    const string_split_result_t result = StringSplit_BySet(
        source,
        &delimiters,
        STRING_SPLIT_FLAG_NONE,
        tokens,
        4u );

    REQUIRE( result.cTokensWritten == 4u );
    REQUIRE( result.cTokensRequired == 4u );
    REQUIRE( ViewEquals( tokens[0], "one" ) );
    REQUIRE( ViewEquals( tokens[1], "two" ) );
    REQUIRE( ViewEquals( tokens[2], "three" ) );
    REQUIRE( ViewEquals( tokens[3], "four" ) );

    character_set_t emptyDelimiters{};
    const string_split_result_t unsplit = StringSplit_BySet(
        source,
        &emptyDelimiters,
        STRING_SPLIT_FLAG_NONE,
        tokens,
        4u );

    REQUIRE( unsplit.cTokensWritten == 1u );
    REQUIRE( unsplit.cTokensRequired == 1u );
    REQUIRE( StringView_Equals( tokens[0], source ) );
}

TEST_CASE( "StringSplit_ByString uses non-overlapping left-to-right matches",
           "[CypherCommon][Tier1][StringSplit]" )
{
    const string_view_t source = StringView_FromCString( "::a::::b::" );
    const string_view_t delimiter = StringView_FromCString( "::" );
    string_view_t tokens[CY_STRING_SPLIT_TEST_TOKEN_CAPACITY]{};

    const string_split_result_t result = StringSplit_ByString(
        source,
        delimiter,
        STRING_SPLIT_FLAG_NONE,
        tokens,
        CY_STRING_SPLIT_TEST_TOKEN_CAPACITY );

    REQUIRE( result.cTokensWritten == 5u );
    REQUIRE( result.cTokensRequired == 5u );
    REQUIRE( StringView_IsEmpty( tokens[0] ) );
    REQUIRE( ViewEquals( tokens[1], "a" ) );
    REQUIRE( StringView_IsEmpty( tokens[2] ) );
    REQUIRE( ViewEquals( tokens[3], "b" ) );
    REQUIRE( StringView_IsEmpty( tokens[4] ) );
}

TEST_CASE( "StringSplit visitors report completion and emitted token indices",
           "[CypherCommon][Tier1][StringSplit]" )
{
    visit_capture_t capture{};

    const string_split_visit_result_t result = StringSplit_VisitByChar(
        StringView_FromCString( "a,b,c" ),
        ',',
        STRING_SPLIT_FLAG_NONE,
        CaptureVisitedToken,
        &capture );

    REQUIRE( result.bCompleted );
    REQUIRE( result.cTokensVisited == 3u );
    REQUIRE( capture.cTokensCaptured == 3u );
    REQUIRE( capture.tokenIndices[0] == 0u );
    REQUIRE( capture.tokenIndices[1] == 1u );
    REQUIRE( capture.tokenIndices[2] == 2u );
    REQUIRE( ViewEquals( capture.tokens[0], "a" ) );
    REQUIRE( ViewEquals( capture.tokens[1], "b" ) );
    REQUIRE( ViewEquals( capture.tokens[2], "c" ) );
}

TEST_CASE( "StringSplit visitors count the token that requests cancellation",
           "[CypherCommon][Tier1][StringSplit]" )
{
    visit_capture_t capture{};
    capture.cStopAfter = 2u;

    const string_split_visit_result_t result = StringSplit_VisitByString(
        StringView_FromCString( "a::b::c" ),
        StringView_FromCString( "::" ),
        STRING_SPLIT_FLAG_NONE,
        CaptureVisitedToken,
        &capture );

    REQUIRE_FALSE( result.bCompleted );
    REQUIRE( result.cTokensVisited == 2u );
    REQUIRE( capture.cTokensCaptured == 2u );
    REQUIRE( ViewEquals( capture.tokens[0], "a" ) );
    REQUIRE( ViewEquals( capture.tokens[1], "b" ) );
}

TEST_CASE( "StringSplit_VisitBySet uses the same delimiter membership policy",
           "[CypherCommon][Tier1][StringSplit]" )
{
    character_set_t delimiters{};
    CharacterSet_Add( &delimiters, ',' );
    CharacterSet_Add( &delimiters, ';' );

    visit_capture_t capture{};
    const string_split_visit_result_t result = StringSplit_VisitBySet(
        StringView_FromCString( "a;b,c" ),
        &delimiters,
        STRING_SPLIT_FLAG_NONE,
        CaptureVisitedToken,
        &capture );

    REQUIRE( result.bCompleted );
    REQUIRE( result.cTokensVisited == 3u );
    REQUIRE( ViewEquals( capture.tokens[0], "a" ) );
    REQUIRE( ViewEquals( capture.tokens[1], "b" ) );
    REQUIRE( ViewEquals( capture.tokens[2], "c" ) );
}

TEST_CASE( "StringSplit reports invalid contracts and returns safe empty results",
           "[CypherCommon][Tier1][StringSplit]" )
{
    const string_view_t validSource = StringView_FromCString( "a,b" );
    const string_view_t invalidSource{ nullptr, 1u };
    string_view_t token{};

    g_stringSplitAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringSplitAssert );

    const string_split_result_t invalidSourceResult = StringSplit_ByChar(
        invalidSource,
        ',',
        STRING_SPLIT_FLAG_NONE,
        &token,
        1u );
    const string_split_result_t invalidOutputResult = StringSplit_ByChar(
        validSource,
        ',',
        STRING_SPLIT_FLAG_NONE,
        nullptr,
        1u );
    const string_split_result_t invalidSetResult = StringSplit_BySet(
        validSource,
        nullptr,
        STRING_SPLIT_FLAG_NONE,
        &token,
        1u );
    const string_split_result_t invalidDelimiterResult = StringSplit_ByString(
        validSource,
        {},
        STRING_SPLIT_FLAG_NONE,
        &token,
        1u );
    const string_split_visit_result_t invalidCallbackResult = StringSplit_VisitByChar(
        validSource,
        ',',
        STRING_SPLIT_FLAG_NONE,
        nullptr,
        nullptr );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( g_stringSplitAssertCount == 5u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( invalidSourceResult.cTokensRequired == 0u );
    REQUIRE( invalidOutputResult.cTokensRequired == 0u );
    REQUIRE( invalidSetResult.cTokensRequired == 0u );
    REQUIRE( invalidDelimiterResult.cTokensRequired == 0u );
    REQUIRE( invalidCallbackResult.cTokensVisited == 0u );
    REQUIRE_FALSE( invalidCallbackResult.bCompleted );
}
