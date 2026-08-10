//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_PathMatch_Tests.cpp
//  Purpose: Tests VFS and editor path filtering policy.
//  Details: Includes, excludes, basename matching, slash normalization, and invalid
//           filter storage are covered independently of filesystem access.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PathMatch.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "PathMatch applies asset path policies",
           "[CypherCommon][Tier1][PathMatch]" )
{
    const flags32_t flags =
        PATH_MATCH_FLAG_CASE_INSENSITIVE_ASCII |
        PATH_MATCH_FLAG_NORMALIZE_SEPARATORS;
    REQUIRE( PathMatch_Wildcard(
        StringView_FromCString( "Materials\\Facility\\wall7.cymat" ),
        StringView_FromCString( "materials/facility/wall[0-9].cymat" ),
        flags ) );
    REQUIRE_FALSE( PathMatch_Wildcard(
        StringView_FromCString( "materials/facility/wall.cymat" ),
        StringView_FromCString( "materials/*.cymat" ),
        flags ) );
    REQUIRE( PathMatch_Wildcard(
        StringView_FromCString( "materials/facility/wall.cymat" ),
        StringView_FromCString( "materials/*.cymat" ),
        flags | PATH_MATCH_FLAG_STAR_CROSSES_SEPARATOR ) );
}

TEST_CASE( "PathMatch filters includes before exclusion overrides",
           "[CypherCommon][Tier1][PathMatch]" )
{
    const string_view_t includes[]{
        StringView_FromCString( "*.cymat" ),
        StringView_FromCString( "*.cytex" )
    };
    const string_view_t excludes[]{
        StringView_FromCString( "debug_*" ),
        StringView_FromCString( "*_temp.*" )
    };
    const path_filter_t filter{
        includes,
        2u,
        excludes,
        2u,
        PATH_MATCH_FLAG_CASE_INSENSITIVE_ASCII |
            PATH_MATCH_FLAG_BASENAME_ONLY
    };

    REQUIRE( PathMatch_Filter(
        StringView_FromCString( "materials/facility/wall.cymat" ),
        filter ) );
    REQUIRE_FALSE( PathMatch_Filter(
        StringView_FromCString( "materials/debug_grid.cymat" ),
        filter ) );
    REQUIRE_FALSE( PathMatch_Filter(
        StringView_FromCString( "models/player.cymesh" ),
        filter ) );
}

TEST_CASE( "PathMatch rejects malformed filter storage",
           "[CypherCommon][Tier1][PathMatch]" )
{
    const path_filter_t invalid{ nullptr, 1u, nullptr, 0u, PATH_MATCH_FLAG_NONE };
    REQUIRE_FALSE( PathMatch_Filter( StringView_FromCString( "asset" ), invalid ) );

    const path_filter_t includeAll{};
    REQUIRE( PathMatch_Filter( StringView_FromCString( "asset" ), includeAll ) );
}
