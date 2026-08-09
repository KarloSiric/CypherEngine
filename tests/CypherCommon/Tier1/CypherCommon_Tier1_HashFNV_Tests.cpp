//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_HashFNV_Tests.cpp
//  Purpose: Tests deterministic FNV-1a hashing.
//  Details: These tests pin standard vectors, chunk-independent updates, binary
//           byte handling, and malformed borrowed-range behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_HashFNV.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_hashFNVAssertCount = 0u;

assert_action_t CaptureHashFNVAssert( const assert_info_t & ) noexcept
{
    ++g_hashFNVAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "FNV-1a matches canonical 32-bit and 64-bit vectors",
           "[CypherCommon][Tier1][HashFNV]" )
{
    struct vector_t {
        const char *pText;
        hash32_t nExpected32;
        hash64_t nExpected64;
    };

    const vector_t vectors[]{
        { "",       0x811C9DC5u, 0xCBF29CE484222325ull },
        { "a",      0xE40C292Cu, 0xAF63DC4C8601EC8Cull },
        { "hello",  0x4F9F2CABu, 0xA430D84680AABD0Bull },
        { "foobar", 0xBF9CF968u, 0x85944171F73967E8ull }
    };

    for ( const vector_t &vector : vectors ) {
        const string_view_t text = StringView_FromCString( vector.pText );
        REQUIRE( HashFNV1a32_String( text ) == vector.nExpected32 );
        REQUIRE( HashFNV1a64_String( text ) == vector.nExpected64 );
        REQUIRE(
            HashFNV1a32_Data(
                BinaryBlock_FromData( text.pData, text.cchLength ) ) ==
            vector.nExpected32 );
        REQUIRE(
            HashFNV1a64_Data(
                BinaryBlock_FromData( text.pData, text.cchLength ) ) ==
            vector.nExpected64 );
    }
}

TEST_CASE( "FNV-1a incremental updates are independent of chunk boundaries",
           "[CypherCommon][Tier1][HashFNV]" )
{
    const byte bytes[]{
        0x00u, 0x01u, 0x7Fu, 0x80u, 0xFEu, 0xFFu,
        'c', 'y', 'p', 'h', 'e', 'r'
    };
    const binary_block_t whole{ bytes, sizeof( bytes ) };

    hash32_t nHash32 = CY_FNV1A32_OFFSET;
    hash64_t nHash64 = CY_FNV1A64_OFFSET;
    for ( usize iByte = 0u; iByte < sizeof( bytes ); ++iByte ) {
        nHash32 = HashFNV1a32_Update( nHash32, { bytes + iByte, 1u } );
        nHash64 = HashFNV1a64_Update( nHash64, { bytes + iByte, 1u } );
    }

    REQUIRE( nHash32 == HashFNV1a32_Data( whole ) );
    REQUIRE( nHash64 == HashFNV1a64_Data( whole ) );

    const hash32_t nBefore32 = nHash32;
    const hash64_t nBefore64 = nHash64;
    REQUIRE( HashFNV1a32_Update( nHash32, {} ) == nBefore32 );
    REQUIRE( HashFNV1a64_Update( nHash64, {} ) == nBefore64 );
}

TEST_CASE( "FNV-1a malformed ranges assert without reading memory",
           "[CypherCommon][Tier1][HashFNV]" )
{
    g_hashFNVAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureHashFNVAssert );

    const binary_block_t invalidBlock{ nullptr, 4u };
    const string_view_t invalidText{ nullptr, 4u };
    const hash32_t nState32 = 0x12345678u;
    const hash64_t nState64 = 0x0123456789ABCDEFull;

    REQUIRE( HashFNV1a32_Update( nState32, invalidBlock ) == nState32 );
    REQUIRE( HashFNV1a64_Update( nState64, invalidBlock ) == nState64 );
    REQUIRE( HashFNV1a32_String( invalidText ) == CY_FNV1A32_OFFSET );
    REQUIRE( HashFNV1a64_String( invalidText ) == CY_FNV1A64_OFFSET );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_hashFNVAssertCount ==
        4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
