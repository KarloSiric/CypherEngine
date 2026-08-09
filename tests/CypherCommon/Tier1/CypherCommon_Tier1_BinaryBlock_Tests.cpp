//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_BinaryBlock_Tests.cpp
//  Purpose: Tests immutable borrowed binary blocks.
//  Details: Protects pointer/size validity, exact byte preservation, clamped
//           subblocks, positioned empty blocks, and Span interoperability.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_BinaryBlock.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_binaryBlockAssertCount = 0u;

assert_action_t CaptureBinaryBlockAssert( const assert_info_t & ) noexcept
{
    ++g_binaryBlockAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "BinaryBlock remains a lightweight borrowed value",
           "[CypherCommon][Tier1][BinaryBlock]" )
{
    STATIC_REQUIRE( is_trivially_copyable_v<binary_block_t> );
    STATIC_REQUIRE( is_standard_layout_v<binary_block_t> );
    STATIC_REQUIRE( sizeof( binary_block_t ) == sizeof( const byte * ) + sizeof( usize ) );
}

TEST_CASE( "BinaryBlock construction preserves exact bytes and empty positions",
           "[CypherCommon][Tier1][BinaryBlock]" )
{
    const byte data[] = { 0x10u, 0x20u, 0x00u, 0xFFu };
    const binary_block_t block = BinaryBlock_FromData( data, sizeof( data ) );
    REQUIRE( BinaryBlock_IsValid( block ) );
    REQUIRE_FALSE( BinaryBlock_IsEmpty( block ) );
    REQUIRE( block.pData == data );
    REQUIRE( block.cbSize == sizeof( data ) );

    const binary_block_t canonicalEmpty = BinaryBlock_FromData( nullptr, 0u );
    REQUIRE( BinaryBlock_IsValid( canonicalEmpty ) );
    REQUIRE( BinaryBlock_IsEmpty( canonicalEmpty ) );
    REQUIRE( canonicalEmpty.pData == nullptr );

    const binary_block_t positionedEmpty = BinaryBlock_FromData( data + 2u, 0u );
    REQUIRE( BinaryBlock_IsValid( positionedEmpty ) );
    REQUIRE( BinaryBlock_IsEmpty( positionedEmpty ) );
    REQUIRE( positionedEmpty.pData == data + 2u );
}

TEST_CASE( "BinaryBlock rejects null non-empty input",
           "[CypherCommon][Tier1][BinaryBlock]" )
{
    g_binaryBlockAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureBinaryBlockAssert );

    const binary_block_t block = BinaryBlock_FromData( nullptr, 5u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( g_binaryBlockAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( BinaryBlock_IsValid( block ) );
    REQUIRE( BinaryBlock_IsEmpty( block ) );
}

TEST_CASE( "BinaryBlock subblocks clamp size and preserve end positions",
           "[CypherCommon][Tier1][BinaryBlock]" )
{
    const byte data[] = { 1u, 2u, 3u, 4u, 5u, 6u };
    const binary_block_t block = BinaryBlock_FromData( data, sizeof( data ) );

    const binary_block_t middle = BinaryBlock_Subblock( block, 2u, 3u );
    REQUIRE( middle.pData == data + 2u );
    REQUIRE( middle.cbSize == 3u );

    const binary_block_t clamped = BinaryBlock_Subblock( block, 4u, 99u );
    REQUIRE( clamped.pData == data + 4u );
    REQUIRE( clamped.cbSize == 2u );

    const binary_block_t atEnd = BinaryBlock_Subblock( block, 6u, 10u );
    REQUIRE( atEnd.pData == data + 6u );
    REQUIRE( atEnd.cbSize == 0u );
}

TEST_CASE( "BinaryBlock out-of-range offsets assert and reduce to the end",
           "[CypherCommon][Tier1][BinaryBlock]" )
{
    const byte data[] = { 1u, 2u, 3u };
    const binary_block_t block = BinaryBlock_FromData( data, sizeof( data ) );

    g_binaryBlockAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureBinaryBlockAssert );

    const binary_block_t result = BinaryBlock_Subblock( block, 50u, 1u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( g_binaryBlockAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( result.pData == data + 3u );
    REQUIRE( result.cbSize == 0u );
}

TEST_CASE( "BinaryBlock and Span conversion preserve one validity contract",
           "[CypherCommon][Tier1][BinaryBlock]" )
{
    const byte data[] = { 4u, 8u, 15u, 16u };
    const const_byte_span_t input = Span_FromArray( data );
    const binary_block_t block = BinaryBlock_FromSpan( input );
    const const_byte_span_t output = BinaryBlock_Span( block );

    REQUIRE( block.pData == data );
    REQUIRE( block.cbSize == sizeof( data ) );
    REQUIRE( output.pData == input.pData );
    REQUIRE( output.nCount == input.nCount );
}

TEST_CASE( "BinaryBlock conversion reports malformed borrowed ranges",
           "[CypherCommon][Tier1][BinaryBlock]" )
{
    const binary_block_t invalidBlock{ nullptr, 4u };
    const const_byte_span_t invalidSpan{ nullptr, 4u };

    g_binaryBlockAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureBinaryBlockAssert );

    const const_byte_span_t spanResult = BinaryBlock_Span( invalidBlock );
    const binary_block_t blockResult = BinaryBlock_FromSpan( invalidSpan );
    const binary_block_t subblockResult = BinaryBlock_Subblock( invalidBlock, 0u, 1u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( spanResult.pData == nullptr );
    REQUIRE( spanResult.nCount == 0u );
    REQUIRE( BinaryBlock_IsValid( blockResult ) );
    REQUIRE( BinaryBlock_IsEmpty( blockResult ) );
    REQUIRE( BinaryBlock_IsValid( subblockResult ) );
    REQUIRE( BinaryBlock_IsEmpty( subblockResult ) );
    REQUIRE(
        g_binaryBlockAssertCount ==
        3u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
