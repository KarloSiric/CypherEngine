//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Assert_Tests.cpp
//  Purpose: Tests Tier0 programmer-invariant diagnostics.
//  Details: These checks protect handler registration, normalized records,
//           source metadata, expression evaluation, and build-mode semantics.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct assert_capture_t {
    u32 callCount{ 0u };
    assert_info_t info{};
};

assert_capture_t g_capture{};

assert_action_t CaptureAssert( const assert_info_t &info ) noexcept
{
    ++g_capture.callCount;
    g_capture.info = info;
    return assert_action_t::Continue;
}

assert_action_t ReplaceAssertHandler( const assert_info_t & ) noexcept
{
    Cy_AssertSetHandler( CaptureAssert );
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Assert forwards normalized records to the installed handler", "[CypherCommon][Tier0][Assert]" )
{
    g_capture = {};
    Cy_AssertSetHandler( CaptureAssert );

    const source_location_t location{ "assert_test.cpp", "TestFunction", 42u, 9u };
    Cy_AssertHandleFailure( nullptr, nullptr, location );

    REQUIRE( g_capture.callCount == 1u );
    REQUIRE( std::strcmp( g_capture.info.pExpression, "<unknown expression>" ) == 0 );
    REQUIRE( std::strcmp( g_capture.info.pMessage, "" ) == 0 );
    REQUIRE( std::strcmp( g_capture.info.location.pFile, "assert_test.cpp" ) == 0 );
    REQUIRE( g_capture.info.location.line == 42u );
    REQUIRE( g_capture.info.location.column == 9u );

    Cy_AssertSetHandler( nullptr );
}

TEST_CASE( "Assert handlers may replace registration", "[CypherCommon][Tier0][Assert]" )
{
    Cy_AssertSetHandler( ReplaceAssertHandler );
    Cy_AssertHandleFailure( "false", "replace", CY_SOURCE_LOCATION );
    REQUIRE( Cy_AssertGetHandler() == CaptureAssert );
    Cy_AssertSetHandler( nullptr );
}

TEST_CASE( "Assert and Verify obey evaluation contracts", "[CypherCommon][Tier0][Assert]" )
{
    g_capture = {};
    Cy_AssertSetHandler( CaptureAssert );

    u32 nAssertEvaluations = 0u;
    u32 nVerifyEvaluations = 0u;
    CY_ASSERT( ++nAssertEvaluations == 0u );
    CY_VERIFY( ++nVerifyEvaluations == 0u );

    REQUIRE( nAssertEvaluations == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( nVerifyEvaluations == 1u );
    REQUIRE( g_capture.callCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) +
                                      static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );

    Cy_AssertSetHandler( nullptr );
}

