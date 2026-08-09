//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_BitBuffer_Tests.cpp
//  Purpose: Tests fixed-capacity non-owning bit storage.
//  Details: These tests protect logical sizing, deterministic fill and shrink,
//           LSB bit indexing, output preservation, and byte-block projection.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitBuffer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "BitBuffer resizes initializes and clears logical bits",
           "[CypherCommon][Tier1][BitBuffer]" )
{
    byte storage[]{ 0xFFu, 0xFFu, 0xFFu };
    bit_buffer_t buffer{};
    REQUIRE( BitBuffer_Init( &buffer, Span_FromArray( storage ) ) );
    REQUIRE( BitBuffer_IsValid( &buffer ) );
    REQUIRE( BitBuffer_IsEmpty( &buffer ) );
    REQUIRE( BitBuffer_Size( &buffer ) == 0u );
    REQUIRE( BitBuffer_Capacity( &buffer ) == 24u );

    REQUIRE( BitBuffer_Resize( &buffer, 10u ) );
    REQUIRE( storage[0] == 0x00u );
    REQUIRE( storage[1] == 0x00u );
    REQUIRE( BitBuffer_Set( &buffer, 0u, CY_TRUE ) );
    REQUIRE( BitBuffer_Set( &buffer, 7u, CY_TRUE ) );
    REQUIRE( BitBuffer_Set( &buffer, 8u, CY_TRUE ) );
    REQUIRE( BitBuffer_Set( &buffer, 9u, CY_TRUE ) );

    const binary_block_t block = BitBuffer_Block( &buffer );
    REQUIRE( block.cbSize == 2u );
    REQUIRE( block.pData[0] == 0x81u );
    REQUIRE( block.pData[1] == 0x03u );

    bool_t value = CY_FALSE;
    REQUIRE( BitBuffer_Get( &buffer, 9u, &value ) );
    REQUIRE( value );
    value = CY_TRUE;
    REQUIRE_FALSE( BitBuffer_Get( &buffer, 10u, &value ) );
    REQUIRE( value );

    REQUIRE( BitBuffer_Resize( &buffer, 5u ) );
    REQUIRE( storage[0] == 0x01u );
    REQUIRE( storage[1] == 0x00u );
    REQUIRE( BitBuffer_Resize( &buffer, 10u, CY_TRUE ) );
    REQUIRE( storage[0] == 0xE1u );
    REQUIRE( storage[1] == 0x03u );

    BitBuffer_Clear( &buffer );
    REQUIRE( BitBuffer_IsEmpty( &buffer ) );
    REQUIRE( storage[0] == 0x00u );
    REQUIRE( storage[1] == 0x00u );
    REQUIRE( BinaryBlock_IsEmpty( BitBuffer_Block( &buffer ) ) );
}

TEST_CASE( "BitBuffer rejects capacity overflow without mutation",
           "[CypherCommon][Tier1][BitBuffer]" )
{
    byte storage[1]{};
    bit_buffer_t buffer{};
    REQUIRE( BitBuffer_Init( &buffer, Span_FromArray( storage ) ) );
    REQUIRE( BitBuffer_Resize( &buffer, 4u, CY_TRUE ) );

    REQUIRE_FALSE( BitBuffer_Resize( &buffer, 9u ) );
    REQUIRE( BitBuffer_Size( &buffer ) == 4u );
    REQUIRE( storage[0] == 0x0Fu );
}
