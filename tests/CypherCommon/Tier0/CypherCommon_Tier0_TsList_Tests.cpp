//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_TsList_Tests.cpp
//  Purpose: Tests Tier0 thread-safe intrusive list behavior.
//  Details: Covers lifecycle, LIFO order, duplicate ownership protection, counts,
//           and concurrent producers without allocating inside the list.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TsList.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <thread>

using namespace cypher::common;

TEST_CASE( "TsList enforces lifecycle LIFO order and unique node ownership", "[CypherCommon][Tier0][TsList]" )
{
    tslist_t list{};
    tslist_t other{};
    tslist_node_t first{};
    tslist_node_t second{};

    REQUIRE_FALSE( Cy_TsListPush( &list, &first ) );
    REQUIRE( Cy_TsListInit( &list ) );
    REQUIRE( Cy_TsListInit( &other ) );
    REQUIRE_FALSE( Cy_TsListInit( &list ) );
    REQUIRE( Cy_TsListIsEmpty( &list ) );

    REQUIRE( Cy_TsListPush( &list, &first ) );
    REQUIRE_FALSE( Cy_TsListPush( &list, &first ) );
    REQUIRE_FALSE( Cy_TsListPush( &other, &first ) );
    REQUIRE( Cy_TsListPush( &list, &second ) );
    REQUIRE( Cy_TsListGetCount( &list ) == 2u );
    REQUIRE_FALSE( Cy_TsListShutdown( &list ) );

    REQUIRE( Cy_TsListPop( &list ) == &second );
    REQUIRE( Cy_TsListPop( &list ) == &first );
    REQUIRE( Cy_TsListPop( &list ) == nullptr );
    REQUIRE( Cy_TsListIsEmpty( &list ) );
    REQUIRE( Cy_TsListShutdown( &list ) );
    REQUIRE( Cy_TsListShutdown( &other ) );
}

TEST_CASE( "TsList accepts concurrent producers without losing nodes", "[CypherCommon][Tier0][TsList]" )
{
    constexpr usize nProducerCount = 4u;
    constexpr usize nNodesPerProducer = 128u;
    constexpr usize nNodeCount = nProducerCount * nNodesPerProducer;

    tslist_t list{};
    std::array<tslist_node_t, nNodeCount> nodes{};
    std::array<std::thread, nProducerCount> producers;
    std::array<bool_t, nProducerCount> producerSucceeded{};
    REQUIRE( Cy_TsListInit( &list ) );

    for ( usize nProducer = 0u; nProducer < nProducerCount; ++nProducer ) {
        producers[nProducer] = std::thread( [&, nProducer]() {
            bool_t didSucceed = CY_TRUE;
            const usize nStart = nProducer * nNodesPerProducer;
            for ( usize nOffset = 0u; nOffset < nNodesPerProducer; ++nOffset ) {
                didSucceed =
                    Cy_TsListPush( &list, &nodes[nStart + nOffset] ) &&
                    didSucceed;
            }
            producerSucceeded[nProducer] = didSucceed;
        } );
    }
    for ( std::thread &producer : producers ) {
        producer.join();
    }

    for ( bool_t didSucceed : producerSucceeded ) {
        REQUIRE( didSucceed );
    }
    REQUIRE( Cy_TsListGetCount( &list ) == nNodeCount );

    usize nPoppedCount = 0u;
    while ( Cy_TsListPop( &list ) != nullptr ) {
        ++nPoppedCount;
    }
    REQUIRE( nPoppedCount == nNodeCount );
    REQUIRE( Cy_TsListShutdown( &list ) );
}
