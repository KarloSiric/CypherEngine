//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Handle_Tests.cpp
//  Purpose: Tests Tier0 packed-handle contracts.
//  Details: These tests cover invalid sentinels, checked construction, complete
//           32/64-bit round trips, field boundaries, and overflow rejection.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Handle.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Handle32 checked construction round trips boundaries", "[CypherCommon][Tier0][Handle]" )
{
    handle32_t handle{};
    REQUIRE_FALSE( Cy_Handle32IsValid( handle ) );
    REQUIRE_FALSE( Cy_Handle32TryMake( 0u, 0u, &handle ) );
    REQUIRE_FALSE( Cy_Handle32IsValid( handle ) );

    REQUIRE( Cy_Handle32TryMake(
        CY_HANDLE32_INDEX_MAX,
        CY_HANDLE32_GENERATION_MAX,
        &handle ) );
    const handle_parts32_t parts = Cy_Handle32Unpack( handle );
    REQUIRE( parts.nIndex == CY_HANDLE32_INDEX_MAX );
    REQUIRE( parts.nGeneration == CY_HANDLE32_GENERATION_MAX );

    REQUIRE_FALSE( Cy_Handle32TryMake(
        CY_HANDLE32_INDEX_MAX + 1u,
        1u,
        &handle ) );
    REQUIRE_FALSE( Cy_Handle32IsValid( handle ) );
    REQUIRE_FALSE( Cy_Handle32TryMake( 1u, 1u, nullptr ) );
}

TEST_CASE( "Handle64 checked construction preserves every field", "[CypherCommon][Tier0][Handle]" )
{
    handle64_t handle{};
    REQUIRE( Cy_Handle64TryMake(
        CY_U32_MAX,
        CY_HANDLE64_GENERATION_MAX,
        CY_HANDLE64_TYPE_MAX,
        &handle ) );
    REQUIRE( Cy_Handle64IsValid( handle ) );

    const handle_parts64_t parts = Cy_Handle64Unpack( handle );
    REQUIRE( parts.nIndex == CY_U32_MAX );
    REQUIRE( parts.nGeneration == CY_HANDLE64_GENERATION_MAX );
    REQUIRE( parts.nType == CY_HANDLE64_TYPE_MAX );

    REQUIRE_FALSE( Cy_Handle64TryMake(
        1u,
        CY_HANDLE64_GENERATION_MAX + 1u,
        1u,
        &handle ) );
    REQUIRE_FALSE( Cy_Handle64TryMake(
        1u,
        1u,
        CY_HANDLE64_TYPE_MAX + 1u,
        &handle ) );
    REQUIRE_FALSE( Cy_Handle64IsValid( Cy_Handle64Make( 0u, 0u, 0u ) ) );
}
