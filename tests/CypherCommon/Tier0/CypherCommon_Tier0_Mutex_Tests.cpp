//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Mutex_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 mutex primitive behavior.
//  Details: These tests validate low-level mutual exclusion used by runtime
//           tables, diagnostics, async systems, and future thread-safe queues.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Mutex.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace cypher::common;

TEST_CASE( "Mutex init and shutdown update initialized state", "[CypherCommon][Tier0][Mutex]" )
{
    cy_mutex_t mutex{};

    REQUIRE_FALSE( Cy_MutexIsInitialized( &mutex ) );
    REQUIRE( Cy_MutexInit( &mutex ) );
    REQUIRE( Cy_MutexIsInitialized( &mutex ) );

    Cy_MutexShutdown( &mutex );
    REQUIRE_FALSE( Cy_MutexIsInitialized( &mutex ) );
}

TEST_CASE( "Mutex try lock reports ownership availability", "[CypherCommon][Tier0][Mutex]" )
{
    cy_mutex_t mutex{};
    REQUIRE( Cy_MutexInit( &mutex ) );

    REQUIRE( Cy_MutexTryLock( &mutex ) );
    REQUIRE_FALSE( Cy_MutexTryLock( &mutex ) );

    Cy_MutexUnlock( &mutex );
    REQUIRE( Cy_MutexTryLock( &mutex ) );
    Cy_MutexUnlock( &mutex );
}

TEST_CASE( "Mutex protects a shared counter across worker threads", "[CypherCommon][Tier0][Mutex]" )
{
    constexpr u32 kThreadCount = 4u;
    constexpr u32 kIterationsPerThread = 1000u;

    cy_mutex_t mutex{};
    i32 nCounter = 0;

    REQUIRE( Cy_MutexInit( &mutex ) );

    std::vector<std::thread> workers;
    workers.reserve( kThreadCount );

    for ( u32 nThreadIndex = 0u; nThreadIndex < kThreadCount; ++nThreadIndex ) {
        workers.emplace_back( [&]() {
            for ( u32 i = 0u; i < kIterationsPerThread; ++i ) {
                Cy_MutexLock( &mutex );
                ++nCounter;
                Cy_MutexUnlock( &mutex );
            }
        } );
    }

    for ( std::thread &worker : workers ) {
        worker.join();
    }

    REQUIRE( nCounter == static_cast<i32>( kThreadCount * kIterationsPerThread ) );
}

TEST_CASE( "Recursive mutex allows same thread to lock multiple times", "[CypherCommon][Tier0][Mutex]" )
{
    cy_recursive_mutex_t mutex{};

    REQUIRE( Cy_RecursiveMutexInit( &mutex ) );
    REQUIRE( Cy_RecursiveMutexIsInitialized( &mutex ) );

    Cy_RecursiveMutexLock( &mutex );
    REQUIRE( Cy_RecursiveMutexTryLock( &mutex ) );

    Cy_RecursiveMutexUnlock( &mutex );
    Cy_RecursiveMutexUnlock( &mutex );
}
