//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_LinkedList_Tests.cpp
//  Purpose: Tests allocator-backed owning doubly linked lists.
//  Details: Covers endpoint insertion, middle insertion, stable nodes, erasure,
//           bidirectional traversal, bulk splice, and value lifetime cleanup.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_LinkedList.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct tracked_list_value_t {
    static inline u32 cConstructed = 0u;
    static inline u32 cDestroyed = 0u;

    i32 nValue{};

    explicit tracked_list_value_t( i32 nInitial = 0 ) noexcept
        : nValue( nInitial )
    {
        ++cConstructed;
    }

    tracked_list_value_t( const tracked_list_value_t &other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
    }

    ~tracked_list_value_t() noexcept
    {
        ++cDestroyed;
    }
};

} // namespace

TEST_CASE( "LinkedList preserves stable nodes and bidirectional order",
           "[CypherCommon][Tier1][LinkedList]" )
{
    linked_list_t<i32> list{};
    REQUIRE( LinkedList_Init( &list, Allocator_GetSystem() ) );

    linked_list_node_t<i32> *pThree = LinkedList_PushBack( &list, 3 );
    linked_list_node_t<i32> *pOne = LinkedList_PushFront( &list, 1 );
    linked_list_node_t<i32> *pTwo = LinkedList_InsertBefore( &list, pThree, 2 );
    REQUIRE( pOne != nullptr );
    REQUIRE( pTwo != nullptr );
    REQUIRE( pThree != nullptr );
    REQUIRE( LinkedList_Count( &list ) == 3u );
    REQUIRE( LinkedList_Front( &list ) == pOne );
    REQUIRE( LinkedList_Next( pOne ) == pTwo );
    REQUIRE( LinkedList_Next( pTwo ) == pThree );
    REQUIRE( LinkedList_Previous( pThree ) == pTwo );
    REQUIRE( LinkedList_Back( &list ) == pThree );

    LinkedList_Erase( &list, pTwo );
    REQUIRE( LinkedList_Count( &list ) == 2u );
    REQUIRE( LinkedList_Next( pOne ) == pThree );
    REQUIRE( LinkedList_Previous( pThree ) == pOne );
    REQUIRE( pOne->value == 1 );
    REQUIRE( pThree->value == 3 );
}

TEST_CASE( "LinkedList splice transfers nodes and owner metadata",
           "[CypherCommon][Tier1][LinkedList]" )
{
    linked_list_t<i32> destination{};
    linked_list_t<i32> source{};
    REQUIRE( LinkedList_Init( &destination, Allocator_GetSystem() ) );
    REQUIRE( LinkedList_Init( &source, Allocator_GetSystem() ) );
    REQUIRE( LinkedList_PushBack( &destination, 1 ) != nullptr );
    linked_list_node_t<i32> *pTwo = LinkedList_PushBack( &source, 2 );
    linked_list_node_t<i32> *pThree = LinkedList_PushBack( &source, 3 );

    LinkedList_SpliceBack( &destination, &source );
    REQUIRE( LinkedList_Count( &destination ) == 3u );
    REQUIRE( LinkedList_IsEmpty( &source ) );
    REQUIRE( pTwo->pOwner == &destination );
    REQUIRE( pThree->pOwner == &destination );
    REQUIRE( LinkedList_Back( &destination ) == pThree );

    LinkedList_Erase( &destination, pTwo );
    REQUIRE( LinkedList_Count( &destination ) == 2u );
}

TEST_CASE( "LinkedList destroys every owned value",
           "[CypherCommon][Tier1][LinkedList]" )
{
    tracked_list_value_t::cConstructed = 0u;
    tracked_list_value_t::cDestroyed = 0u;
    {
        linked_list_t<tracked_list_value_t> list{};
        REQUIRE( LinkedList_Init( &list, Allocator_GetSystem() ) );
        const tracked_list_value_t first( 1 );
        const tracked_list_value_t second( 2 );
        REQUIRE( LinkedList_PushBack( &list, first ) != nullptr );
        REQUIRE( LinkedList_PushBack( &list, second ) != nullptr );
        LinkedList_Clear( &list );
        REQUIRE( LinkedList_IsEmpty( &list ) );
    }
    REQUIRE( tracked_list_value_t::cConstructed ==
             tracked_list_value_t::cDestroyed );
}

TEST_CASE( "LinkedList exposes explicit validity and shutdown contracts",
           "[CypherCommon][Tier1][LinkedList]" )
{
    linked_list_t<u32> list{};
    REQUIRE( LinkedList_IsValid( &list ) );
    REQUIRE( LinkedList_Init( &list, Allocator_GetSystem() ) );
    REQUIRE( LinkedList_PushBack( &list, 17u ) != nullptr );

    LinkedList_Shutdown( &list );
    REQUIRE( LinkedList_IsValid( &list ) );
    REQUIRE( LinkedList_IsEmpty( &list ) );
}
