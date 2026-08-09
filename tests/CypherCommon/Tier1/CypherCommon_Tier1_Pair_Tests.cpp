//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Pair_Tests.cpp
//  Purpose: Tests lightweight heterogeneous pair values.
//  Details: Protects aggregate layout, heterogeneous storage, constexpr creation,
//           and component-wise equality without adding ownership behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Pair.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Pair remains a transparent aggregate value",
           "[CypherCommon][Tier1][Pair]" )
{
    STATIC_REQUIRE( is_trivially_copyable_v<pair_t<u32, u64>> );
    STATIC_REQUIRE( is_standard_layout_v<pair_t<u32, u64>> );

    constexpr pair_t<u32, i32> value = Pair_Make<u32, i32>( 7u, -3 );
    STATIC_REQUIRE( value.first == 7u );
    STATIC_REQUIRE( value.second == -3 );
}

TEST_CASE( "Pair equality compares both components",
           "[CypherCommon][Tier1][Pair]" )
{
    constexpr pair_t<u32, char> pairA{ 42u, 'x' };
    constexpr pair_t<u32, char> pairB{ 42u, 'x' };
    constexpr pair_t<u32, char> differentFirst{ 7u, 'x' };
    constexpr pair_t<u32, char> differentSecond{ 42u, 'y' };

    STATIC_REQUIRE( Pair_Equals( pairA, pairB ) );
    STATIC_REQUIRE_FALSE( Pair_Equals( pairA, differentFirst ) );
    STATIC_REQUIRE_FALSE( Pair_Equals( pairA, differentSecond ) );
}
