//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_PriorityQueue_Tests.cpp
//  Purpose: Tests allocator-backed binary-heap priority queues.
//  Details: Covers max/min policies, duplicate values, pop ordering, empty behavior,
//           capacity reuse, and explicit shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PriorityQueue.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct greater_i32_t {
    bool_t operator()( i32 left, i32 right ) const noexcept
    {
        return left > right;
    }
};

} // namespace

TEST_CASE( "PriorityQueue default policy produces a max heap",
           "[CypherCommon][Tier1][PriorityQueue]" )
{
    priority_queue_t<i32> queue{};
    REQUIRE( PriorityQueue_Init( &queue, Allocator_GetSystem(), 2u ) );
    for ( i32 nValue : { 4, 1, 7, 7, 3, 9, 2 } ) {
        REQUIRE( PriorityQueue_Push( &queue, nValue ) );
    }
    REQUIRE( PriorityQueue_Count( &queue ) == 7u );
    REQUIRE( *PriorityQueue_Top( &queue ) == 9 );

    const i32 expected[]{ 9, 7, 7, 4, 3, 2, 1 };
    for ( i32 nExpected : expected ) {
        i32 nValue = 0;
        REQUIRE( PriorityQueue_Pop( &queue, &nValue ) );
        REQUIRE( nValue == nExpected );
    }
    REQUIRE( PriorityQueue_IsEmpty( &queue ) );
    REQUIRE( PriorityQueue_Top( &queue ) == nullptr );
    REQUIRE_FALSE( PriorityQueue_Pop( &queue ) );
}

TEST_CASE( "PriorityQueue comparison policy can produce a min heap",
           "[CypherCommon][Tier1][PriorityQueue]" )
{
    priority_queue_t<i32, greater_i32_t> queue{};
    REQUIRE( PriorityQueue_Init(
        &queue,
        Allocator_GetSystem(),
        0u,
        greater_i32_t{} ) );
    for ( i32 nValue : { 8, 3, 5, 1, 6 } ) {
        REQUIRE( PriorityQueue_Push( &queue, nValue ) );
    }

    const i32 expected[]{ 1, 3, 5, 6, 8 };
    for ( i32 nExpected : expected ) {
        i32 nValue = 0;
        REQUIRE( PriorityQueue_Pop( &queue, &nValue ) );
        REQUIRE( nValue == nExpected );
    }

    REQUIRE( PriorityQueue_Push( &queue, 12 ) );
    PriorityQueue_Clear( &queue );
    REQUIRE( PriorityQueue_IsEmpty( &queue ) );
    REQUIRE( PriorityQueue_IsValid( &queue ) );
    PriorityQueue_Shutdown( &queue );
    REQUIRE( PriorityQueue_IsValid( &queue ) );
}
