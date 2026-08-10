//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_RangeCheckedVar_Tests.cpp
//  Purpose: Tests reject and clamp policies for range-checked values.
//  Details: Verifies inclusive endpoints, transactional rejection, initialization,
//           and clamping for integral and floating-point value types.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RangeCheckedVar.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "RangeCheckedVar rejects invalid assignments transactionally",
           "[CypherCommon][Tier1][RangeCheckedVar]" )
{
    range_checked_var_t<i32> variable{};
    REQUIRE( RangeCheckedVar_Init( &variable, 5, { 0, 10 } ) );
    REQUIRE( RangeCheckedVar_Get( variable ) == 5 );
    REQUIRE( RangeCheckedVar_TrySet( &variable, 10 ) );
    REQUIRE( RangeCheckedVar_Get( variable ) == 10 );

    REQUIRE_FALSE( RangeCheckedVar_TrySet( &variable, 11 ) );
    REQUIRE( RangeCheckedVar_Get( variable ) == 10 );

    range_checked_var_t<i32> unchanged{};
    REQUIRE_FALSE( RangeCheckedVar_Init( &unchanged, 12, { 0, 10 } ) );
    REQUIRE( unchanged.value == 0 );
}

TEST_CASE( "RangeCheckedVar clamps to inclusive endpoints",
           "[CypherCommon][Tier1][RangeCheckedVar]" )
{
    range_checked_var_t<f32> variable{};
    REQUIRE( RangeCheckedVar_Init( &variable, 0.5f, { 0.0f, 1.0f } ) );

    RangeCheckedVar_SetClamped( &variable, -4.0f );
    REQUIRE( RangeCheckedVar_Get( variable ) == 0.0f );
    RangeCheckedVar_SetClamped( &variable, 5.0f );
    REQUIRE( RangeCheckedVar_Get( variable ) == 1.0f );
    RangeCheckedVar_SetClamped( &variable, 0.25f );
    REQUIRE( RangeCheckedVar_Get( variable ) == 0.25f );
}
