//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Module_Tests.cpp
//  Purpose: Tests Tier0 binary-module metadata contracts.
//  Details: These tests cover descriptor validation, semantic and binary API
//           compatibility, state names, and legal module lifecycle transitions.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Module.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Module validates descriptors and API versions", "[CypherCommon][Tier0][Module]" )
{
    module_desc_t descriptor{
        "Renderer",
        "CypherRenderer",
        "Render backend module",
        { 1u, 2u, 3u, 4u },
        7u
    };

    REQUIRE( Cy_ModuleDescriptorIsValid( &descriptor ) );
    REQUIRE_FALSE( Cy_ModuleDescriptorIsValid( nullptr ) );
    REQUIRE( Cy_ModuleApiVersionCompatible( 7u, 7u ) );
    REQUIRE_FALSE( Cy_ModuleApiVersionCompatible( 7u, 8u ) );
    REQUIRE_FALSE( Cy_ModuleApiVersionCompatible( 0u, 0u ) );

    descriptor.pszInternalName = "";
    REQUIRE_FALSE( Cy_ModuleDescriptorIsValid( &descriptor ) );
}

TEST_CASE( "Module version compatibility follows major minor patch policy", "[CypherCommon][Tier0][Module]" )
{
    const module_version_t required{ 2u, 3u, 4u, 10u };

    REQUIRE( Cy_ModuleVersionCompatible(
        required,
        module_version_t{ 2u, 3u, 4u, 1u } ) );
    REQUIRE( Cy_ModuleVersionCompatible(
        required,
        module_version_t{ 2u, 4u, 0u, 0u } ) );
    REQUIRE_FALSE( Cy_ModuleVersionCompatible(
        required,
        module_version_t{ 1u, 9u, 9u, 9u } ) );
    REQUIRE_FALSE( Cy_ModuleVersionCompatible(
        required,
        module_version_t{ 2u, 3u, 3u, 99u } ) );
}

TEST_CASE( "Module lifecycle transitions are explicit", "[CypherCommon][Tier0][Module]" )
{
    REQUIRE( Cy_ModuleCanTransition(
        module_state_t::Unloaded,
        module_state_t::Loaded ) );
    REQUIRE( Cy_ModuleCanTransition(
        module_state_t::Loaded,
        module_state_t::Initialized ) );
    REQUIRE( Cy_ModuleCanTransition(
        module_state_t::Initialized,
        module_state_t::Shutdown ) );
    REQUIRE( Cy_ModuleCanTransition(
        module_state_t::Shutdown,
        module_state_t::Unloaded ) );

    REQUIRE_FALSE( Cy_ModuleCanTransition(
        module_state_t::Unloaded,
        module_state_t::Initialized ) );
    REQUIRE_FALSE( Cy_ModuleCanTransition(
        module_state_t::Initialized,
        module_state_t::Unloaded ) );
    const module_state_t invalidState = static_cast<module_state_t>( 255u );
    REQUIRE_FALSE( Cy_ModuleCanTransition(
        invalidState,
        invalidState ) );
    REQUIRE_FALSE( Cy_ModuleCanTransition(
        invalidState,
        module_state_t::Unloaded ) );
    REQUIRE( Cy_ModuleStateName( module_state_t::Initialized )[0] != '\0' );
    REQUIRE( Cy_ModuleStateName( invalidState )[0] != '\0' );
}
