//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_MapSet_Tests.cpp
//  Purpose: Tests ordered map and set facades over the red-black tree.
//  Details: Verifies duplicate policy, typed value lookup, const access, ordering,
//           erasure, counts, clear reuse, and explicit shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Map.h"
#include "CypherCommon_Set.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Map exposes ordered unique key-value storage",
           "[CypherCommon][Tier1][Map]" )
{
    map_t<u32, i32> map{};
    REQUIRE( Map_Init( &map, Allocator_GetSystem() ) );
    REQUIRE( Map_Insert( &map, 7u, 70 ).bInserted );
    REQUIRE( Map_Insert( &map, 2u, 20 ).bInserted );
    REQUIRE( Map_Insert( &map, 9u, 90 ).bInserted );
    REQUIRE_FALSE( Map_Insert( &map, 7u, 700 ).bInserted );
    REQUIRE( Map_Count( &map ) == 3u );
    REQUIRE( *Map_Find( &map, 7u ) == 70 );
    REQUIRE( Map_Find( &map, 8u ) == nullptr );

    const map_t<u32, i32> &constMap = map;
    STATIC_REQUIRE( is_same_v<decltype( Map_Find( &constMap, 7u ) ), const i32 *> );
    REQUIRE( *Map_Find( &constMap, 9u ) == 90 );
    REQUIRE( Map_Erase( &map, 2u ) );
    REQUIRE_FALSE( Map_Contains( &map, 2u ) );
    Map_Clear( &map );
    REQUIRE( Map_Count( &map ) == 0u );
}

TEST_CASE( "Set keeps unique keys in deterministic order",
           "[CypherCommon][Tier1][Set]" )
{
    set_t<u32> set{};
    REQUIRE( Set_Init( &set, Allocator_GetSystem() ) );
    for ( u32 nKey : { 5u, 1u, 8u, 3u } ) {
        REQUIRE( Set_Insert( &set, nKey ) );
    }
    REQUIRE_FALSE( Set_Insert( &set, 5u ) );
    REQUIRE( Set_Count( &set ) == 4u );
    REQUIRE( Set_Contains( &set, 3u ) );

    const u32 expected[]{ 1u, 3u, 5u, 8u };
    usize iExpected = 0u;
    for ( const rb_tree_node_t<u32, set_unit_t> *pNode = RBTree_First( set.pRoot );
          pNode != nullptr;
          pNode = RBTree_Next( pNode ) ) {
        REQUIRE( pNode->key == expected[iExpected] );
        ++iExpected;
    }
    REQUIRE( iExpected == 4u );
    REQUIRE( Set_Erase( &set, 5u ) );
    REQUIRE_FALSE( Set_Contains( &set, 5u ) );
    Set_Shutdown( &set );
    REQUIRE( RBTree_IsValid( &set ) );
}
