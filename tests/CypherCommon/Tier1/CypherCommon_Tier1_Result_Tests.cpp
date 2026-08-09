//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Result_Tests.cpp
//  Purpose: Tests packed operation and value result records.
//  Details: Protects named success/failure construction, error preservation,
//           invalid failure-code fallback, and success-gated value access.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Result.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_resultAssertCount = 0u;

assert_action_t CaptureResultAssert( const assert_info_t & ) noexcept
{
    ++g_resultAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Result remains a compact packed error value",
           "[CypherCommon][Tier1][Result]" )
{
    STATIC_REQUIRE( is_trivially_copyable_v<result_t> );
    STATIC_REQUIRE( is_standard_layout_v<result_t> );
    STATIC_REQUIRE( sizeof( result_t ) == sizeof( error_code_t ) );
    STATIC_REQUIRE( is_trivially_copyable_v<value_result_t<u64>> );
}

TEST_CASE( "Result named constructors preserve success and subsystem failure",
           "[CypherCommon][Tier1][Result]" )
{
    constexpr result_t success = Result_Success();
    constexpr error_code_t error = Cy_ErrorMake(
        error_domain_t::FILESYSTEM,
        7u );
    constexpr result_t failure = Result_Failure( error );

    STATIC_REQUIRE( Result_Succeeded( success ) );
    STATIC_REQUIRE_FALSE( Result_Failed( success ) );
    STATIC_REQUIRE( Result_Failed( failure ) );
    STATIC_REQUIRE( Result_ErrorCode( failure ) == error );
    STATIC_REQUIRE( Cy_ErrorDomain( failure.error ) == error_domain_t::FILESYSTEM );
}

TEST_CASE( "ValueResult exposes values only after success",
           "[CypherCommon][Tier1][Result]" )
{
    value_result_t<u32> success = ValueResult_Success<u32>( 42u );
    value_result_t<u32> failure = ValueResult_Failure<u32>(
        Cy_ErrorMake( common_error_t::ERR_NOT_FOUND ) );

    REQUIRE( Result_Succeeded( success ) );
    REQUIRE( ValueResult_Get( &success ) == &success.value );
    REQUIRE( *ValueResult_Get( &success ) == 42u );
    REQUIRE( Result_Failed( failure ) );
    REQUIRE( ValueResult_Get( &failure ) == nullptr );
    REQUIRE( ValueResult_Get(
                 static_cast<value_result_t<u32> *>( nullptr ) ) == nullptr );

    const value_result_t<u32> constSuccess = success;
    STATIC_REQUIRE( is_same_v<
        decltype( ValueResult_Get( &constSuccess ) ),
        const u32 *> );
    REQUIRE( ValueResult_Get( &constSuccess ) == &constSuccess.value );
}

TEST_CASE( "Result failure constructors reject success codes deterministically",
           "[CypherCommon][Tier1][Result]" )
{
    g_resultAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureResultAssert );

    const result_t result = Result_Failure( CY_ERROR_OK );
    const value_result_t<u32> valueResult =
        ValueResult_Failure<u32>( CY_ERROR_OK );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( Result_Failed( result ) );
    REQUIRE( Result_Failed( valueResult ) );
    REQUIRE( Cy_ErrorLocalCode( result.error ) ==
             static_cast<u16>( common_error_t::ERR_FAILED ) );
    REQUIRE(
        g_resultAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
