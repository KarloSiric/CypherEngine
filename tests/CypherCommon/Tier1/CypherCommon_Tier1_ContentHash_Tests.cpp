//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ContentHash_Tests.cpp
//  Purpose: Tests stable content fingerprints.
//  Details: Verifies XXH3 identity, portable composition, canonical hexadecimal
//           conversion, transactional parse failures, and invalid propagation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_ContentHash.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace cypher::common;

namespace
{

u32 g_contentHashAssertCount = 0u;

assert_action_t CaptureContentHashAssert( const assert_info_t & ) noexcept
{
    ++g_contentHashAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "ContentHash uses the stable XXH3-128 word contract",
           "[CypherCommon][Tier1][ContentHash]" )
{
    const content_hash_t empty = ContentHash_Data( {} );
    REQUIRE( empty.low == 0x6001C324468D497Full );
    REQUIRE( empty.high == 0x99AA06D3014798D8ull );
    REQUIRE( ContentHash_IsValid( empty ) );
    REQUIRE_FALSE( ContentHash_IsValid( CY_CONTENT_HASH_INVALID ) );

    const string_view_t text = StringView_FromCString( "cypher/material" );
    REQUIRE( ContentHash_Equals(
        ContentHash_String( text ),
        ContentHash_Data( BinaryBlock_FromData( text.pData, text.cchLength ) ) ) );
}

TEST_CASE( "ContentHash hex encoding is canonical and round trips",
           "[CypherCommon][Tier1][ContentHash]" )
{
    const content_hash_t empty = ContentHash_Data( {} );
    char text[CY_CONTENT_HASH_HEX_CAPACITY]{};
    REQUIRE(
        ContentHash_ToHex( empty, text, sizeof( text ) ) ==
        CY_CONTENT_HASH_HEX_LENGTH );
    REQUIRE( std::strcmp( text, "99aa06d3014798d86001c324468d497f" ) == 0 );

    content_hash_t parsed{};
    REQUIRE( ContentHash_FromHex( StringView_FromCString( text ), &parsed ) );
    REQUIRE( ContentHash_Equals( parsed, empty ) );

    REQUIRE( ContentHash_FromHex(
        StringView_FromCString( "99AA06D3014798D86001C324468D497F" ),
        &parsed ) );
    REQUIRE( ContentHash_Equals( parsed, empty ) );
}

TEST_CASE( "ContentHash malformed text leaves output unchanged",
           "[CypherCommon][Tier1][ContentHash]" )
{
    const content_hash_t sentinel{ 1u, 2u };
    content_hash_t output = sentinel;
    REQUIRE_FALSE( ContentHash_FromHex( StringView_FromCString( "1234" ), &output ) );
    REQUIRE( ContentHash_Equals( output, sentinel ) );

    output = sentinel;
    REQUIRE_FALSE( ContentHash_FromHex(
        StringView_FromCString( "99aa06d3014798d86001c324468d497x" ),
        &output ) );
    REQUIRE( ContentHash_Equals( output, sentinel ) );
}

TEST_CASE( "ContentHash composition is ordered and propagates invalid inputs",
           "[CypherCommon][Tier1][ContentHash]" )
{
    const content_hash_t left = ContentHash_String( StringView_FromCString( "left" ) );
    const content_hash_t right = ContentHash_String( StringView_FromCString( "right" ) );
    const content_hash_t combined = ContentHash_Combine( left, right );

    REQUIRE( ContentHash_IsValid( combined ) );
    REQUIRE( ContentHash_Equals( combined, ContentHash_Combine( left, right ) ) );
    REQUIRE_FALSE( ContentHash_Equals( combined, ContentHash_Combine( right, left ) ) );
    REQUIRE( ContentHash_Equals(
        ContentHash_Combine( CY_CONTENT_HASH_INVALID, right ),
        CY_CONTENT_HASH_INVALID ) );
}

TEST_CASE( "ContentHash text output rejects insufficient storage safely",
           "[CypherCommon][Tier1][ContentHash]" )
{
    g_contentHashAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureContentHashAssert );

    char text[8]{ 'x', '\0' };
    REQUIRE( ContentHash_ToHex( { 1u, 2u }, text, sizeof( text ) ) == 0u );
    REQUIRE( text[0] == '\0' );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_contentHashAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
