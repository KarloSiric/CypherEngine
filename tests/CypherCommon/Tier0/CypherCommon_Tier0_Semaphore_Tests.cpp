//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Semaphore_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 semaphore synchronization behavior.
//  Details: These tests validate counting waits used by future work queues,
//           async request queues, and producer/consumer systems.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Semaphore.h"
#include "CypherCommon_Thread.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace cypher::common;

TEST_CASE( "Semaphore init validates counts and exposes current count", "[CypherCommon][Tier0][Semaphore]" )
{
    cy_semaphore_t semaphore{};

    REQUIRE_FALSE( Cy_SemaphoreInit( &semaphore, 2u, 1u ) );
    REQUIRE( Cy_SemaphoreInit( &semaphore, 2u, 4u ) );
    REQUIRE_FALSE( Cy_SemaphoreInit( &semaphore, 0u, 4u ) );
    REQUIRE( Cy_SemaphoreIsInitialized( &semaphore ) );
    REQUIRE( Cy_SemaphoreGetCount( &semaphore ) == 2u );

    REQUIRE( Cy_SemaphoreShutdown( &semaphore ) );
    REQUIRE_FALSE( Cy_SemaphoreIsInitialized( &semaphore ) );
    REQUIRE( Cy_SemaphoreWaitResult( &semaphore ) ==
             cy_wait_result_t::Shutdown );
}

TEST_CASE( "Semaphore try wait consumes available counts", "[CypherCommon][Tier0][Semaphore]" )
{
    cy_semaphore_t semaphore{};

    REQUIRE( Cy_SemaphoreInit( &semaphore, 2u, 4u ) );
    REQUIRE( Cy_SemaphoreTryWait( &semaphore ) );
    REQUIRE( Cy_SemaphoreGetCount( &semaphore ) == 1u );
    REQUIRE( Cy_SemaphoreTryWait( &semaphore ) );
    REQUIRE( Cy_SemaphoreGetCount( &semaphore ) == 0u );
    REQUIRE_FALSE( Cy_SemaphoreTryWait( &semaphore ) );
    REQUIRE( Cy_SemaphoreShutdown( &semaphore ) );
}

TEST_CASE( "Semaphore post respects maximum count", "[CypherCommon][Tier0][Semaphore]" )
{
    cy_semaphore_t semaphore{};

    REQUIRE( Cy_SemaphoreInit( &semaphore, 1u, 3u ) );
    REQUIRE( Cy_SemaphorePost( &semaphore, 2u ) );
    REQUIRE( Cy_SemaphoreGetCount( &semaphore ) == 3u );
    REQUIRE_FALSE( Cy_SemaphorePost( &semaphore, 1u ) );
    REQUIRE( Cy_SemaphoreShutdown( &semaphore ) );
}

TEST_CASE( "Semaphore wait timeout and worker wake behavior", "[CypherCommon][Tier0][Semaphore]" )
{
    cy_semaphore_t semaphore{};
    bool_t bWorkerWoke = CY_FALSE;

    REQUIRE( Cy_SemaphoreInit( &semaphore, 0u, 2u ) );
    REQUIRE( Cy_SemaphoreWaitTimeoutMsResult( &semaphore, 1u ) ==
             cy_wait_result_t::Timeout );

    std::thread worker( [&]() {
        bWorkerWoke = Cy_SemaphoreWait( &semaphore );
    } );

    Cy_ThreadSleepMs( 1u );
    REQUIRE( Cy_SemaphorePost( &semaphore ) );
    worker.join();

    REQUIRE( bWorkerWoke );
    REQUIRE( Cy_SemaphoreGetCount( &semaphore ) == 0u );
    REQUIRE( Cy_SemaphoreShutdown( &semaphore ) );
}

TEST_CASE( "Semaphore shutdown waits for blocked workers to leave", "[CypherCommon][Tier0][Semaphore]" )
{
    cy_semaphore_t semaphore{};
    cy_wait_result_t workerResult = cy_wait_result_t::Invalid;
    REQUIRE( Cy_SemaphoreInit( &semaphore, 0u, 2u ) );

    std::thread worker( [&]() {
        workerResult = Cy_SemaphoreWaitResult( &semaphore );
    } );
    while ( Cy_SemaphoreGetWaiterCount( &semaphore ) == 0u ) {
        Cy_ThreadYield();
    }

    REQUIRE( Cy_SemaphoreShutdown( &semaphore ) );
    worker.join();

    REQUIRE( workerResult == cy_wait_result_t::Shutdown );
    REQUIRE( Cy_SemaphoreGetWaiterCount( &semaphore ) == 0u );
}
