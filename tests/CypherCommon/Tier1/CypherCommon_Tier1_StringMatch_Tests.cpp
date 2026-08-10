//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringMatch_Tests.cpp
//  Purpose: Tests bounded literal and wildcard string matching.
//  Details: The suite covers ASCII case policy, path separators, wildcard
//           backtracking, character classes, malformed patterns, and invalid input.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringMatch.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "StringMatch applies explicit byte comparison policies",
           "[CypherCommon][Tier1][StringMatch]" )
{
    const string_view_t text = StringView_FromCString( "Materials\\Wall.CYMAT" );
    const flags32_t normalizedFlags =
        STRING_MATCH_FLAG_CASE_INSENSITIVE_ASCII |
        STRING_MATCH_FLAG_PATH_SEPARATORS_EQUAL;

    REQUIRE( StringMatch_Equals(
        text,
        StringView_FromCString( "materials/wall.cymat" ),
        normalizedFlags ) );
    REQUIRE( StringMatch_StartsWith(
        text,
        StringView_FromCString( "materials/" ),
        normalizedFlags ) );
    REQUIRE( StringMatch_EndsWith(
        text,
        StringView_FromCString( ".cymat" ),
        normalizedFlags ) );
    REQUIRE( StringMatch_Contains(
        text,
        StringView_FromCString( "wall" ),
        normalizedFlags ) );
    REQUIRE_FALSE( StringMatch_Equals(
        text,
        StringView_FromCString( "materials/wall.cymat" ),
        STRING_MATCH_FLAG_NONE ) );
}

TEST_CASE( "StringMatch wildcard does not cross directories by default",
           "[CypherCommon][Tier1][StringMatch]" )
{
    const string_view_t path = StringView_FromCString( "materials/facility/wall.cymat" );
    const flags32_t pathFlags = STRING_MATCH_FLAG_PATH_SEPARATORS_EQUAL;

    REQUIRE( StringMatch_Wildcard(
        path,
        StringView_FromCString( "materials/*/wall.?ymat" ),
        pathFlags ) );
    REQUIRE_FALSE( StringMatch_Wildcard(
        path,
        StringView_FromCString( "materials/*.cymat" ),
        pathFlags ) );
    REQUIRE( StringMatch_Wildcard(
        path,
        StringView_FromCString( "materials/*.cymat" ),
        pathFlags | STRING_MATCH_FLAG_STAR_MATCHES_SEPARATOR ) );
}

TEST_CASE( "StringMatch wildcard backtracks without recursion",
           "[CypherCommon][Tier1][StringMatch]" )
{
    REQUIRE( StringMatch_Wildcard(
        StringView_FromCString( "abefcdgiescdfimde" ),
        StringView_FromCString( "ab*cd?i*de" ),
        STRING_MATCH_FLAG_STAR_MATCHES_SEPARATOR ) );
    REQUIRE_FALSE( StringMatch_Wildcard(
        StringView_FromCString( "asset/material" ),
        StringView_FromCString( "asset/*mesh" ),
        STRING_MATCH_FLAG_NONE ) );
}

TEST_CASE( "StringMatch supports optional ASCII character classes",
           "[CypherCommon][Tier1][StringMatch]" )
{
    const flags32_t flags =
        STRING_MATCH_FLAG_ALLOW_CHARACTER_CLASS |
        STRING_MATCH_FLAG_CASE_INSENSITIVE_ASCII;

    REQUIRE( StringMatch_Wildcard(
        StringView_FromCString( "texture7" ),
        StringView_FromCString( "[tT]exture[0-9]" ),
        flags ) );
    REQUIRE( StringMatch_Wildcard(
        StringView_FromCString( "modelA" ),
        StringView_FromCString( "model[!0-9]" ),
        flags ) );
    REQUIRE_FALSE( StringMatch_Wildcard(
        StringView_FromCString( "model4" ),
        StringView_FromCString( "model[^0-9]" ),
        flags ) );
    REQUIRE_FALSE( StringMatch_Wildcard(
        StringView_FromCString( "a" ),
        StringView_FromCString( "[z-a]" ),
        flags ) );
    REQUIRE_FALSE( StringMatch_Wildcard(
        StringView_FromCString( "a" ),
        StringView_FromCString( "[abc" ),
        flags ) );
}

TEST_CASE( "StringMatch rejects invalid views and flag bits",
           "[CypherCommon][Tier1][StringMatch]" )
{
    const string_view_t invalid{ nullptr, 1u };
    const string_view_t empty{};
    REQUIRE_FALSE( StringMatch_Equals( invalid, empty, STRING_MATCH_FLAG_NONE ) );
    REQUIRE_FALSE( StringMatch_Wildcard( empty, invalid, STRING_MATCH_FLAG_NONE ) );
    REQUIRE_FALSE( StringMatch_Equals( empty, empty, CYPHER_BIT32( 31 ) ) );
    REQUIRE( StringMatch_Wildcard( empty, empty, STRING_MATCH_FLAG_NONE ) );
}
