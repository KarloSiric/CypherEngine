//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Search_Tests.cpp
//  Purpose: Tests generic linear and ordered range searches.
//  Details: Covers duplicates, missing values, insertion boundaries, empty ranges,
//           and custom equality policies without allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Search.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct absolute_equal_t {
    bool_t operator()( i32 left, i32 right ) const noexcept
    {
        const i32 nLeft = left < 0 ? -left : left;
        const i32 nRight = right < 0 ? -right : right;
        return nLeft == nRight;
    }
};

} // namespace

TEST_CASE( "Search linear lookup honors caller equality",
           "[CypherCommon][Tier1][Search]" )
{
    const i32 values[]{ -3, 7, -11, 15 };
    REQUIRE( Search_Linear<i32>( { values, 4u }, 11, absolute_equal_t{} ) == 2u );
    REQUIRE( Search_Linear<i32>( { values, 4u }, 8, absolute_equal_t{} ) ==
             CY_INVALID_SIZE );
    REQUIRE( Search_Linear<i32>( {}, 8 ) == CY_INVALID_SIZE );
}

TEST_CASE( "Search bounds identify duplicate and insertion ranges",
           "[CypherCommon][Tier1][Search]" )
{
    const i32 values[]{ 1, 2, 2, 2, 4, 7 };
    const span_t<const i32> range{ values, 6u };

    REQUIRE( Search_LowerBound( range, 2 ) == 1u );
    REQUIRE( Search_UpperBound( range, 2 ) == 4u );
    REQUIRE( Search_LowerBound( range, 3 ) == 4u );
    REQUIRE( Search_UpperBound( range, 3 ) == 4u );
    REQUIRE( Search_LowerBound( range, 0 ) == 0u );
    REQUIRE( Search_UpperBound( range, 9 ) == 6u );
    REQUIRE( Search_LowerBound<i32>( {}, 3 ) == 0u );
    REQUIRE( Search_UpperBound<i32>( {}, 3 ) == 0u );
}

TEST_CASE( "Search binary lookup returns the first equivalent value",
           "[CypherCommon][Tier1][Search]" )
{
    const i32 values[]{ 1, 2, 2, 2, 4, 7 };
    const span_t<const i32> range{ values, 6u };

    REQUIRE( Search_Binary( range, 2 ) == 1u );
    REQUIRE( Search_Binary( range, 7 ) == 5u );
    REQUIRE( Search_Binary( range, 3 ) == CY_INVALID_SIZE );
    REQUIRE( Search_Binary<i32>( {}, 3 ) == CY_INVALID_SIZE );
}
