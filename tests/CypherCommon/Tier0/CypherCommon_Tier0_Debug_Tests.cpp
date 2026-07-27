//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Debug_Tests.cpp
//  Purpose: Tests Tier0 debugger queries and build gates.
//  Details: These checks protect Boolean debugger state and exact Debug,
//           Development, Release, Shipping, diagnostic, and non-shipping gates.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Debug.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Debug reports debugger state as a Boolean", "[CypherCommon][Tier0][Debug]" )
{
    const bool_t bAttached = Cy_DebuggerIsAttached();
    REQUIRE( ( bAttached == CY_TRUE || bAttached == CY_FALSE ) );

    if ( !bAttached ) {
        REQUIRE_FALSE( Cy_DebugBreakIfAttached() );
    }
}

TEST_CASE( "Debug build gates match the selected configuration", "[CypherCommon][Tier0][Debug]" )
{
    u32 nDebug = 0u;
    u32 nDevelopment = 0u;
    u32 nRelease = 0u;
    u32 nShipping = 0u;
    u32 nDiagnostic = 0u;
    u32 nNonShipping = 0u;

    CY_DEBUG_ONLY( ++nDebug );
    CY_DEVELOPMENT_ONLY( ++nDevelopment );
    CY_RELEASE_ONLY( ++nRelease );
    CY_SHIPPING_ONLY( ++nShipping );
    CY_DIAGNOSTIC_ONLY( ++nDiagnostic );
    CY_NON_SHIPPING_ONLY( ++nNonShipping );

    REQUIRE( nDebug == static_cast<u32>( CYPHER_CONFIG_DEBUG ) );
    REQUIRE( nDevelopment == static_cast<u32>( CYPHER_CONFIG_DEVELOPMENT ) );
    REQUIRE( nRelease == static_cast<u32>( CYPHER_CONFIG_RELEASE ) );
    REQUIRE( nShipping == static_cast<u32>( CYPHER_CONFIG_SHIPPING ) );
    REQUIRE( nDiagnostic == static_cast<u32>( CYPHER_CONFIG_DEBUG || CYPHER_CONFIG_DEVELOPMENT ) );
    REQUIRE( nNonShipping == static_cast<u32>( !CYPHER_CONFIG_SHIPPING ) );
}

