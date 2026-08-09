//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Hash_Tests.cpp
//  Purpose: Tests engine-default hash behavior.
//  Details: Verifies adapter selection, string/data equivalence, allocation-free
//           ASCII folding across chunk boundaries, and order-sensitive combining.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Char.h"
#include "CypherCommon_Hash.h"
#include "CypherCommon_HashXXH.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Default hashes select the documented xxHash algorithms",
           "[CypherCommon][Tier1][Hash]" )
{
    const byte data[]{ 1u, 2u, 3u, 4u, 5u };
    const binary_block_t block{ data, sizeof( data ) };

    REQUIRE(
        Hash32_Data( block ) ==
        HashXXH32_Data( block, CY_HASH32_DEFAULT_SEED ) );
    REQUIRE(
        Hash64_Data( block ) ==
        HashXXH3_64_Data( block, CY_HASH64_DEFAULT_SEED ) );
}

TEST_CASE( "Default string hashes equal hashes of the represented bytes",
           "[CypherCommon][Tier1][Hash]" )
{
    const string_view_t text = StringView_FromCString( "materials/metal_01" );
    const binary_block_t data = BinaryBlock_FromData( text.pData, text.cchLength );

    REQUIRE( Hash32_String( text ) == Hash32_Data( data ) );
    REQUIRE( Hash64_String( text ) == Hash64_Data( data ) );
}

TEST_CASE( "ASCII-insensitive hashes equal hashes of lowercase bytes",
           "[CypherCommon][Tier1][Hash]" )
{
    char mixed[600]{};
    char lower[600]{};
    for ( usize iChar = 0u; iChar < sizeof( mixed ); ++iChar ) {
        mixed[iChar] = ( iChar & 1u ) == 0u ? 'A' : 'z';
        lower[iChar] = Char_ToLowerAscii( mixed[iChar] );
    }

    const string_view_t mixedView{ mixed, sizeof( mixed ) };
    const string_view_t lowerView{ lower, sizeof( lower ) };
    REQUIRE(
        Hash32_StringInsensitiveAscii( mixedView ) ==
        Hash32_String( lowerView ) );
    REQUIRE(
        Hash64_StringInsensitiveAscii( mixedView ) ==
        Hash64_String( lowerView ) );

    REQUIRE(
        Hash64_StringInsensitiveAscii( StringView_FromCString( "CYDF/Materials" ) ) ==
        Hash64_StringInsensitiveAscii( StringView_FromCString( "cydf/materials" ) ) );
}

TEST_CASE( "Hash combining is deterministic and order-sensitive",
           "[CypherCommon][Tier1][Hash]" )
{
    REQUIRE( Hash32_Combine( 10u, 20u ) == Hash32_Combine( 10u, 20u ) );
    REQUIRE( Hash64_Combine( 10u, 20u ) == Hash64_Combine( 10u, 20u ) );
    REQUIRE( Hash32_Combine( 10u, 20u ) != Hash32_Combine( 20u, 10u ) );
    REQUIRE( Hash64_Combine( 10u, 20u ) != Hash64_Combine( 20u, 10u ) );
    REQUIRE( Hash32_Combine( 10u, 20u ) != Hash32_Combine( 10u, 21u ) );
    REQUIRE( Hash64_Combine( 10u, 20u ) != Hash64_Combine( 10u, 21u ) );
}
