//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringBuilder_Tests.cpp
//  Purpose: Tests non-owning bounded text construction.
//  Details: Protects termination, required-length accounting, count-only use,
//           aliases, formatting, sticky status, and invalid-call behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_StringBuilder.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_stringBuilderAssertCount = 0u;

assert_action_t CaptureStringBuilderAssert( const assert_info_t & ) noexcept
{
    ++g_stringBuilderAssertCount;
    return assert_action_t::Continue;
}

void RequireBuilderEquals(
    const string_builder_t &builder,
    const char *pExpected )
{
    REQUIRE( StringBuilder_IsValid( &builder ) );
    REQUIRE( StringView_Equals(
        StringBuilder_View( &builder ),
        StringView_FromCString( pExpected ) ) );
    REQUIRE( builder.pData[builder.cchLength] == '\0' );
}

} // namespace

TEST_CASE( "StringBuilder initializes and appends bounded text",
           "[CypherCommon][Tier1][StringBuilder]" )
{
    char storage[32]{};
    string_builder_t builder{};
    REQUIRE( StringBuilder_Init( &builder, storage, sizeof( storage ) ) );
    REQUIRE( StringBuilder_IsValid( &builder ) );
    REQUIRE( StringBuilder_Length( &builder ) == 0u );
    REQUIRE( StringBuilder_Required( &builder ) == 0u );
    REQUIRE( StringBuilder_Remaining( &builder ) == 31u );

    REQUIRE(
        StringBuilder_Append(
            &builder,
            StringView_FromCString( "cypher" ) ) ==
        string_builder_status_t::OK );
    REQUIRE( StringBuilder_AppendChar( &builder, '/' ) ==
             string_builder_status_t::OK );
    REQUIRE( StringBuilder_AppendRepeat( &builder, 'x', 3u ) ==
             string_builder_status_t::OK );
    RequireBuilderEquals( builder, "cypher/xxx" );
    REQUIRE( StringBuilder_Length( &builder ) == 10u );
    REQUIRE( StringBuilder_Required( &builder ) == 10u );
}

TEST_CASE( "StringBuilder truncation remains terminated and keeps counting",
           "[CypherCommon][Tier1][StringBuilder]" )
{
    char storage[5]{};
    string_builder_t builder{};
    REQUIRE( StringBuilder_Init( &builder, storage, sizeof( storage ) ) );
    REQUIRE(
        StringBuilder_Append(
            &builder,
            StringView_FromCString( "abcdef" ) ) ==
        string_builder_status_t::OUTPUT_TRUNCATED );
    RequireBuilderEquals( builder, "abcd" );
    REQUIRE( StringBuilder_Length( &builder ) == 4u );
    REQUIRE( StringBuilder_Required( &builder ) == 6u );
    REQUIRE( StringBuilder_WasTruncated( &builder ) );

    REQUIRE(
        StringBuilder_Append(
            &builder,
            StringView_FromCString( "XY" ) ) ==
        string_builder_status_t::OUTPUT_TRUNCATED );
    RequireBuilderEquals( builder, "abcd" );
    REQUIRE( StringBuilder_Required( &builder ) == 8u );
}

TEST_CASE( "StringBuilder supports count-only output sizing",
           "[CypherCommon][Tier1][StringBuilder]" )
{
    string_builder_t builder{};
    REQUIRE( StringBuilder_Init( &builder, nullptr, 0u ) );
    REQUIRE( StringBuilder_CStr( &builder )[0] == '\0' );
    REQUIRE(
        StringBuilder_Append(
            &builder,
            StringView_FromCString( "material" ) ) ==
        string_builder_status_t::OUTPUT_TRUNCATED );
    REQUIRE( StringBuilder_Length( &builder ) == 0u );
    REQUIRE( StringBuilder_Required( &builder ) == 8u );
    REQUIRE( StringBuilder_View( &builder ).pData == nullptr );
}

TEST_CASE( "StringBuilder append tolerates a source alias",
           "[CypherCommon][Tier1][StringBuilder]" )
{
    char storage[16]{};
    string_builder_t builder{};
    REQUIRE( StringBuilder_Init( &builder, storage, sizeof( storage ) ) );
    REQUIRE( StringBuilder_Append(
                 &builder,
                 StringView_FromCString( "abc" ) ) ==
             string_builder_status_t::OK );
    const string_view_t self = StringBuilder_View( &builder );
    REQUIRE( StringBuilder_Append( &builder, self ) ==
             string_builder_status_t::OK );
    RequireBuilderEquals( builder, "abcabc" );
}

TEST_CASE( "StringBuilder formatting shares truncation accounting",
           "[CypherCommon][Tier1][StringBuilder]" )
{
    char storage[16]{};
    string_builder_t builder{};
    REQUIRE( StringBuilder_Init( &builder, storage, sizeof( storage ) ) );
    REQUIRE( StringBuilder_AppendFormat(
                 &builder,
                 "%s:%u",
                 "port",
                 27015u ) == string_builder_status_t::OK );
    RequireBuilderEquals( builder, "port:27015" );

    REQUIRE( StringBuilder_AppendFormat(
                 &builder,
                 "/%s",
                 "network" ) ==
             string_builder_status_t::OUTPUT_TRUNCATED );
    REQUIRE( StringBuilder_Length( &builder ) == 15u );
    REQUIRE( StringBuilder_Required( &builder ) == 18u );
    REQUIRE( storage[15] == '\0' );
}

TEST_CASE( "StringBuilder clear resets text required length and status",
           "[CypherCommon][Tier1][StringBuilder]" )
{
    char storage[4]{};
    string_builder_t builder{};
    REQUIRE( StringBuilder_Init( &builder, storage, sizeof( storage ) ) );
    REQUIRE( StringBuilder_AppendRepeat( &builder, 'a', 8u ) ==
             string_builder_status_t::OUTPUT_TRUNCATED );

    StringBuilder_Clear( &builder );
    REQUIRE( StringBuilder_Status( &builder ) == string_builder_status_t::OK );
    REQUIRE( StringBuilder_Length( &builder ) == 0u );
    REQUIRE( StringBuilder_Required( &builder ) == 0u );
    REQUIRE( storage[0] == '\0' );
}

TEST_CASE( "StringBuilder invalid calls assert and retain queryable state",
           "[CypherCommon][Tier1][StringBuilder]" )
{
    g_stringBuilderAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureStringBuilderAssert );

    char storage[8]{};
    REQUIRE_FALSE( StringBuilder_Init( nullptr, storage, sizeof( storage ) ) );
    REQUIRE(
        StringBuilder_Append(
            nullptr,
            StringView_FromCString( "x" ) ) ==
        string_builder_status_t::INVALID_ARGUMENT );

    string_builder_t builder{};
    REQUIRE( StringBuilder_Init( &builder, storage, sizeof( storage ) ) );
    REQUIRE(
        StringBuilder_Append(
            &builder,
            string_view_t{ nullptr, 1u } ) ==
        string_builder_status_t::INVALID_ARGUMENT );
    REQUIRE( StringBuilder_IsValid( &builder ) );

    StringBuilder_Clear( &builder );
    REQUIRE( StringBuilder_AppendFormat( &builder, nullptr ) ==
             string_builder_status_t::INVALID_ARGUMENT );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_stringBuilderAssertCount ==
        4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
