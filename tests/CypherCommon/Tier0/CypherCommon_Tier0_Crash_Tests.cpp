//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Crash_Tests.cpp
//  Purpose: Tests Tier0 synchronous crash reporting.
//  Details: These checks protect record normalization, complete source metadata,
//           handler registration, and reentrant registration changes.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Crash.h"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

crash_info_t g_crashInfo{};
u32 g_crashCount = 0u;

void CaptureCrash( const crash_info_t &info ) noexcept
{
    ++g_crashCount;
    g_crashInfo = info;
}

void ReplaceCrashHandler( const crash_info_t & ) noexcept
{
    Cy_CrashSetHandler( CaptureCrash );
}

} // namespace

TEST_CASE( "Crash reports normalized structured context", "[CypherCommon][Tier0][Crash]" )
{
    g_crashInfo = {};
    g_crashCount = 0u;
    Cy_CrashSetHandler( CaptureCrash );

    const source_location_t location{ "crash_test.cpp", "TestFunction", 77u, 5u };
    Cy_CrashReport( nullptr, location );

    REQUIRE( g_crashCount == 1u );
    REQUIRE( std::strcmp( g_crashInfo.pReason, "Unknown fatal error" ) == 0 );
    REQUIRE( std::strcmp( g_crashInfo.location.pFile, "crash_test.cpp" ) == 0 );
    REQUIRE( g_crashInfo.location.line == 77u );
    REQUIRE( g_crashInfo.location.column == 5u );

    Cy_CrashSetHandler( nullptr );
}

TEST_CASE( "Crash handlers may replace registration", "[CypherCommon][Tier0][Crash]" )
{
    Cy_CrashSetHandler( ReplaceCrashHandler );
    Cy_CrashReport( "replace", CY_SOURCE_LOCATION );
    REQUIRE( Cy_CrashGetHandler() == CaptureCrash );
    Cy_CrashSetHandler( nullptr );
}

