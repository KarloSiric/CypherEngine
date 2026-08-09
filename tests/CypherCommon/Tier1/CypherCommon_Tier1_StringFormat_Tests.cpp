//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringFormat_Tests.cpp
//  Purpose: Tests bounded formatted-text helpers.
//  Details: Protects required-length reporting, termination, append totals,
//           signed limits, display units, and invalid argument handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringView.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdarg>

using namespace cypher::common;

namespace
{

u32 g_stringFormatAssertCount = 0u;

assert_action_t CaptureStringFormatAssert( const assert_info_t & ) noexcept
{
    ++g_stringFormatAssertCount;
    return assert_action_t::Continue;
}

string_format_result_t CallVPrintf(
    char *pDest,
    usize cchDest,
    const char *pFormat,
    ... ) noexcept
{
    std::va_list args;
    va_start( args, pFormat );
    const string_format_result_t result =
        StringFormat_VPrintf( pDest, cchDest, pFormat, args );
    va_end( args );
    return result;
}

void RequireCStringEquals( const char *pActual, const char *pExpected )
{
    REQUIRE( StringView_Equals(
        StringView_FromCString( pActual ),
        StringView_FromCString( pExpected ) ) );
}

} // namespace

TEST_CASE( "StringFormat printf writes and measures complete output",
           "[CypherCommon][Tier1][StringFormat]" )
{
    char output[32]{};
    const string_format_result_t result =
        StringFormat_Printf( output, sizeof( output ), "entity %d", 42 );
    REQUIRE( result.status == string_format_status_t::OK );
    REQUIRE( result.cchWritten == 9u );
    REQUIRE( result.cchRequired == 9u );
    RequireCStringEquals( output, "entity 42" );

    char viaList[32]{};
    const string_format_result_t listResult =
        CallVPrintf( viaList, sizeof( viaList ), "%s:%u", "port", 27015u );
    REQUIRE( listResult.status == string_format_status_t::OK );
    RequireCStringEquals( viaList, "port:27015" );
}

TEST_CASE( "StringFormat truncates safely and retains required length",
           "[CypherCommon][Tier1][StringFormat]" )
{
    char output[6] = { 'x', 'x', 'x', 'x', 'x', 'x' };
    const string_format_result_t result =
        StringFormat_Printf( output, sizeof( output ), "entity %d", 42 );
    REQUIRE( result.status == string_format_status_t::OUTPUT_TRUNCATED );
    REQUIRE( result.cchWritten == 5u );
    REQUIRE( result.cchRequired == 9u );
    REQUIRE( output[5] == '\0' );
    RequireCStringEquals( output, "entit" );

    const string_format_result_t measured =
        StringFormat_Printf( nullptr, 0u, "entity %d", 42 );
    REQUIRE( measured.status == string_format_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.cchWritten == 0u );
    REQUIRE( measured.cchRequired == 9u );
}

TEST_CASE( "StringFormat append reports complete destination totals",
           "[CypherCommon][Tier1][StringFormat]" )
{
    char output[16] = "path";
    usize cchLength = 4u;
    const string_format_result_t result = StringFormat_Append(
        output,
        sizeof( output ),
        &cchLength,
        "/%s",
        "asset" );
    REQUIRE( result.status == string_format_status_t::OK );
    REQUIRE( result.cchWritten == 10u );
    REQUIRE( result.cchRequired == 10u );
    REQUIRE( cchLength == 10u );
    RequireCStringEquals( output, "path/asset" );

    char truncated[8] = "path";
    cchLength = 4u;
    const string_format_result_t truncatedResult = StringFormat_Append(
        truncated,
        sizeof( truncated ),
        &cchLength,
        "/%s",
        "asset" );
    REQUIRE(
        truncatedResult.status ==
        string_format_status_t::OUTPUT_TRUNCATED );
    REQUIRE( truncatedResult.cchWritten == 7u );
    REQUIRE( truncatedResult.cchRequired == 10u );
    REQUIRE( cchLength == 7u );
    RequireCStringEquals( truncated, "path/as" );
}

TEST_CASE( "StringFormat grouped integers handle signs and full i64 range",
           "[CypherCommon][Tier1][StringFormat]" )
{
    char output[64]{};
    REQUIRE(
        StringFormat_GroupedInteger(
            1234567890,
            ',',
            output,
            sizeof( output ) ).status == string_format_status_t::OK );
    RequireCStringEquals( output, "1,234,567,890" );

    REQUIRE(
        StringFormat_GroupedInteger(
            CY_I64_MIN,
            ',',
            output,
            sizeof( output ) ).status == string_format_status_t::OK );
    RequireCStringEquals( output, "-9,223,372,036,854,775,808" );

    char small[5]{};
    const string_format_result_t truncated =
        StringFormat_GroupedInteger( 12345, ',', small, sizeof( small ) );
    REQUIRE( truncated.status == string_format_status_t::OUTPUT_TRUNCATED );
    REQUIRE( truncated.cchRequired == 6u );
    RequireCStringEquals( small, "12,3" );
}

TEST_CASE( "StringFormat byte counts and durations select readable units",
           "[CypherCommon][Tier1][StringFormat]" )
{
    char output[64]{};
    REQUIRE(
        StringFormat_ByteCount(
            1536u,
            2u,
            output,
            sizeof( output ) ).status == string_format_status_t::OK );
    RequireCStringEquals( output, "1.50 KiB" );

    REQUIRE(
        StringFormat_Duration(
            0.0015,
            2u,
            output,
            sizeof( output ) ).status == string_format_status_t::OK );
    RequireCStringEquals( output, "1.50 ms" );

    REQUIRE(
        StringFormat_Duration(
            90.0,
            2u,
            output,
            sizeof( output ) ).status == string_format_status_t::OK );
    RequireCStringEquals( output, "1.50 min" );
}

TEST_CASE( "StringFormat invalid arguments assert and return status",
           "[CypherCommon][Tier1][StringFormat]" )
{
    g_stringFormatAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureStringFormatAssert );

    REQUIRE(
        StringFormat_Printf( nullptr, 4u, "%s", "x" ).status ==
        string_format_status_t::INVALID_ARGUMENT );

    char output[16]{};
    REQUIRE(
        StringFormat_Printf( output, sizeof( output ), nullptr ).status ==
        string_format_status_t::INVALID_ARGUMENT );
    REQUIRE( output[0] == '\0' );

    REQUIRE(
        StringFormat_GroupedInteger(
            1,
            '\0',
            output,
            sizeof( output ) ).status ==
        string_format_status_t::INVALID_ARGUMENT );
    REQUIRE(
        StringFormat_ByteCount(
            1u,
            10u,
            output,
            sizeof( output ) ).status ==
        string_format_status_t::INVALID_ARGUMENT );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_stringFormatAssertCount ==
        4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
