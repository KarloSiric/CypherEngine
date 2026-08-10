//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_IntrusiveList_Tests.cpp
//  Purpose: Tests intrusive doubly linked list ownership and ordering.
//  Details: Covers endpoint insertion, insertion before a node, bidirectional
//           traversal, removal, clear, and node reuse without allocations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_IntrusiveList.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "IntrusiveList preserves insertion and traversal order",
           "[CypherCommon][Tier1][IntrusiveList]" )
{
    intrusive_list_t list{};
    intrusive_list_node_t first{};
    intrusive_list_node_t middle{};
    intrusive_list_node_t last{};
    IntrusiveList_Init( &list );

    IntrusiveList_PushBack( &list, &first );
    IntrusiveList_PushBack( &list, &last );
    IntrusiveList_InsertBefore( &list, &last, &middle );

    REQUIRE( IntrusiveList_Count( &list ) == 3u );
    REQUIRE( IntrusiveList_Front( &list ) == &first );
    REQUIRE( IntrusiveList_Next( &list, &first ) == &middle );
    REQUIRE( IntrusiveList_Next( &list, &middle ) == &last );
    REQUIRE( IntrusiveList_Next( &list, &last ) == nullptr );
    REQUIRE( IntrusiveList_Back( &list ) == &last );
    REQUIRE( IntrusiveList_Previous( &list, &last ) == &middle );
    REQUIRE( IntrusiveList_Previous( &list, &first ) == nullptr );
}

TEST_CASE( "IntrusiveList removal and clear detach nodes for reuse",
           "[CypherCommon][Tier1][IntrusiveList]" )
{
    intrusive_list_t list{};
    intrusive_list_node_t first{};
    intrusive_list_node_t second{};
    IntrusiveList_Init( &list );
    REQUIRE( IntrusiveList_IsEmpty( &list ) );

    IntrusiveList_PushFront( &list, &first );
    IntrusiveList_PushFront( &list, &second );
    REQUIRE( IntrusiveList_IsLinked( &first ) );
    IntrusiveList_Remove( &list, &first );
    REQUIRE_FALSE( IntrusiveList_IsLinked( &first ) );
    REQUIRE( IntrusiveList_Count( &list ) == 1u );

    IntrusiveList_PushBack( &list, &first );
    IntrusiveList_Clear( &list );
    REQUIRE( IntrusiveList_IsEmpty( &list ) );
    REQUIRE_FALSE( IntrusiveList_IsLinked( &first ) );
    REQUIRE_FALSE( IntrusiveList_IsLinked( &second ) );
    REQUIRE( IntrusiveList_Front( &list ) == nullptr );
    REQUIRE( IntrusiveList_Back( &list ) == nullptr );
}
