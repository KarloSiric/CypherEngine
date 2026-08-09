//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringToken_Tests.cpp
//  Purpose: Tests compact deterministic string tokens.
//  Details: Covers determinism, case policy, byte length, empty text, invalid
//           sentinel behavior, and representable-length validation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_StringToken.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_stringTokenAssertCount = 0u;

assert_action_t CaptureStringTokenAssert( const assert_info_t & ) noexcept
{
    ++g_stringTokenAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "StringToken is deterministic and records byte length",
           "[CypherCommon][Tier1][StringToken]" )
{
    const string_view_t text = StringView_FromCString( "renderer.opengl" );
    const string_token_t first = StringToken_FromView( text );
    const string_token_t second = StringToken_FromView( text );
    REQUIRE( StringToken_IsValid( first ) );
    REQUIRE( StringToken_Equals( first, second ) );
    REQUIRE( first.cchLength == text.cchLength );
    REQUIRE_FALSE( StringToken_Equals(
        first,
        StringToken_FromView( StringView_FromCString( "renderer.vulkan" ) ) ) );
}

TEST_CASE( "StringToken ASCII-insensitive policy folds only ASCII case",
           "[CypherCommon][Tier1][StringToken]" )
{
    const string_token_t upper = StringToken_FromViewInsensitiveAscii(
        StringView_FromCString( "Render.Debug" ) );
    const string_token_t lower = StringToken_FromViewInsensitiveAscii(
        StringView_FromCString( "render.debug" ) );
    REQUIRE( StringToken_Equals( upper, lower ) );
    REQUIRE_FALSE( StringToken_Equals(
        StringToken_FromView( StringView_FromCString( "Render.Debug" ) ),
        StringToken_FromView( StringView_FromCString( "render.debug" ) ) ) );
}

TEST_CASE( "StringToken represents empty text without using the invalid token",
           "[CypherCommon][Tier1][StringToken]" )
{
    const string_token_t empty = StringToken_FromView( {} );
    REQUIRE( StringToken_IsValid( empty ) );
    REQUIRE( empty.cchLength == 0u );
    REQUIRE_FALSE( StringToken_IsValid( CY_STRING_TOKEN_INVALID ) );
    REQUIRE( StringToken_Equals(
        CY_STRING_TOKEN_INVALID,
        CY_STRING_TOKEN_INVALID ) );
}

TEST_CASE( "StringToken rejects invalid and unrepresentable views before reading",
           "[CypherCommon][Tier1][StringToken]" )
{
    g_stringTokenAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureStringTokenAssert );

    REQUIRE_FALSE( StringToken_IsValid(
        StringToken_FromView( { nullptr, 1u } ) ) );
#if CYPHER_TARGET_64BIT
    const char byteValue = 'x';
    REQUIRE_FALSE( StringToken_IsValid( StringToken_FromView(
        { &byteValue, static_cast<usize>( CY_U32_MAX ) + 1u } ) ) );
#endif

    Cy_AssertSetHandler( pPreviousHandler );
    const u32 nExpected =
        ( CYPHER_TARGET_64BIT ? 2u : 1u ) *
        static_cast<u32>( CYPHER_ASSERTS_ENABLED );
    REQUIRE( g_stringTokenAssertCount == nExpected );
}
