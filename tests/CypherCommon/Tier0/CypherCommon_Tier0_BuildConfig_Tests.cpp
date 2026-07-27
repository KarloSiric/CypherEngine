//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_BuildConfig_Tests.cpp
//  Purpose: Tests Tier0 build configuration contracts.
//  Details: These checks verify that CMake configuration definitions map to one
//           typed runtime identity and retain stable diagnostic names.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BuildConfig.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "BuildConfig maps the active CMake configuration", "[CypherCommon][Tier0][BuildConfig]" )
{
#if CYPHER_CONFIG_DEBUG
    STATIC_REQUIRE( Cy_BuildConfigGetCurrent() == build_config_t::Debug );
#elif CYPHER_CONFIG_DEVELOPMENT
    STATIC_REQUIRE( Cy_BuildConfigGetCurrent() == build_config_t::Development );
#elif CYPHER_CONFIG_RELEASE
    STATIC_REQUIRE( Cy_BuildConfigGetCurrent() == build_config_t::Release );
#elif CYPHER_CONFIG_SHIPPING
    STATIC_REQUIRE( Cy_BuildConfigGetCurrent() == build_config_t::Shipping );
#endif

    STATIC_REQUIRE( Cy_BuildConfigIsKnown( Cy_BuildConfigGetCurrent() ) );
}

TEST_CASE( "BuildConfig predicates remain mutually exclusive", "[CypherCommon][Tier0][BuildConfig]" )
{
    constexpr u32 nActivePredicates =
        static_cast<u32>( Cy_BuildConfigIsDebug() ) +
        static_cast<u32>( Cy_BuildConfigIsDevelopment() ) +
        static_cast<u32>( Cy_BuildConfigIsRelease() ) +
        static_cast<u32>( Cy_BuildConfigIsShipping() );

    STATIC_REQUIRE( nActivePredicates == 1u );
    STATIC_REQUIRE( Cy_BuildConfigIsOptimized() == !Cy_BuildConfigIsDebug() );
}

TEST_CASE( "BuildConfig names are stable", "[CypherCommon][Tier0][BuildConfig]" )
{
    STATIC_REQUIRE( Cy_BuildConfigGetName( build_config_t::Unknown )[0] == 'U' );
    STATIC_REQUIRE( Cy_BuildConfigGetName( build_config_t::Debug )[0] == 'D' );
    STATIC_REQUIRE( Cy_BuildConfigGetName( build_config_t::Development )[0] == 'D' );
    STATIC_REQUIRE( Cy_BuildConfigGetName( build_config_t::Release )[0] == 'R' );
    STATIC_REQUIRE( Cy_BuildConfigGetName( build_config_t::Shipping )[0] == 'S' );

    STATIC_REQUIRE_FALSE( Cy_BuildConfigIsKnown( build_config_t::Unknown ) );
    STATIC_REQUIRE( Cy_BuildConfigIsKnown( build_config_t::Debug ) );
    STATIC_REQUIRE( Cy_BuildConfigIsKnown( build_config_t::Development ) );
    STATIC_REQUIRE( Cy_BuildConfigIsKnown( build_config_t::Release ) );
    STATIC_REQUIRE( Cy_BuildConfigIsKnown( build_config_t::Shipping ) );
    STATIC_REQUIRE_FALSE( Cy_BuildConfigIsKnown(
        static_cast<build_config_t>( 255u ) ) );
}
