//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Process_Tests.cpp
//  Purpose: Tests Tier0 process identity and executable path behavior.
//  Details: These tests verify stable process identity, bounded path storage, and
//           immutable concurrent publication without invoking process termination.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Process.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <thread>

using namespace cypher::common;

TEST_CASE( "Process exposes stable identity and executable path", "[CypherCommon][Tier0][Process]" )
{
    REQUIRE( Cy_ProcessGetCurrentId() != 0u );

    const char *pszPath = Cy_ProcessGetExecutablePath();
    REQUIRE( pszPath != nullptr );
    REQUIRE( pszPath[0] != '\0' );
    REQUIRE( std::strlen( pszPath ) < CY_PROCESS_PATH_MAX );
    REQUIRE( Cy_ProcessGetExecutablePath() == pszPath );
}

TEST_CASE( "Process publishes one executable path across threads", "[CypherCommon][Tier0][Process]" )
{
    constexpr usize THREAD_COUNT = 16u;
    std::array<const char *, THREAD_COUNT> results = {};
    std::array<std::thread, THREAD_COUNT> threads;

    for ( usize i = 0u; i < THREAD_COUNT; ++i ) {
        threads[i] = std::thread( [&results, i]() {
            results[i] = Cy_ProcessGetExecutablePath();
        } );
    }

    for ( std::thread &thread : threads ) {
        thread.join();
    }

    const char *pszExpected = Cy_ProcessGetExecutablePath();
    for ( const char *pszResult : results ) {
        REQUIRE( pszResult == pszExpected );
    }
}
