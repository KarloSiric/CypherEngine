//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_StackTrace_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 stack trace helpers.
//  Details: These tests verify raw frame capture, bounds handling, and empty
//           trace behavior without depending on symbol names or platform
//           unwinder formatting.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon/Tier0/CypherCommon_Platform.h"
#include "CypherCommon/Tier0/CypherCommon_StackTrace.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Tier0 stack trace clear resets all frames", "[CypherCommon][Tier0][StackTrace]" )
{
    stack_trace_t trace{};
    trace.frame_count = 2u;
    trace.frames[0].address = reinterpret_cast<void *>( static_cast<uintptr>( 0x1000u ) );
    trace.frames[1].address = reinterpret_cast<void *>( static_cast<uintptr>( 0x2000u ) );

    Cy_StackTraceClear( &trace );

    REQUIRE( Cy_StackTraceIsEmpty( &trace ) );
    REQUIRE( Cy_StackTraceGetFrameCount( &trace ) == 0u );
    REQUIRE( Cy_StackTraceGetFrameAddress( &trace, 0u ) == nullptr );
}

TEST_CASE( "Tier0 stack trace capture respects caller frame limit", "[CypherCommon][Tier0][StackTrace]" )
{
    stack_trace_t trace{};
    const u32 cFrames = Cy_StackTraceCapture( &trace, 8u, 0u );

    REQUIRE( cFrames <= 8u );
    REQUIRE( Cy_StackTraceGetFrameCount( &trace ) == cFrames );

#if CYPHER_PLATFORM_WINDOWS || CYPHER_PLATFORM_POSIX
    REQUIRE( cFrames > 0u );
    REQUIRE( Cy_StackTraceGetFrameAddress( &trace, 0u ) != nullptr );
#endif
}

TEST_CASE( "Tier0 stack trace handles invalid arguments", "[CypherCommon][Tier0][StackTrace]" )
{
    stack_trace_t trace{};

    REQUIRE( Cy_StackTraceCapture( nullptr, 8u, 0u ) == 0u );
    REQUIRE( Cy_StackTraceCapture( &trace, 0u, 0u ) == 0u );
    REQUIRE( Cy_StackTraceGetFrameCount( nullptr ) == 0u );
    REQUIRE( Cy_StackTraceGetFrameAddress( nullptr, 0u ) == nullptr );
    REQUIRE( Cy_StackTraceIsEmpty( nullptr ) );
}
