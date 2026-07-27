//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_MemoryOps_Tests.cpp
//  Purpose: Tests Tier0 MemoryOps Tests behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryOps.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct memoryops_test_struct_t {
    u32 nA;
    u32 nB;
};

} // namespace

TEST_CASE( "MemoryOps copy compare and equality operate on byte ranges", "[CypherCommon][Tier0][MemoryOps]" )
{
    u8 nSrc[] = { 1u, 2u, 3u, 4u, 5u, 6u };
    u8 nDst[] = { 0u, 0u, 0u, 0u, 0u, 0u };

    REQUIRE( Cy_MemCopy( nDst, nSrc, sizeof( nSrc ) ) == nDst );
    REQUIRE( Cy_MemEqual( nDst, nSrc, sizeof( nSrc ) ) );
    REQUIRE( Cy_MemCompare( nDst, nSrc, sizeof( nSrc ) ) == 0 );

    nDst[3] = 9u;
    REQUIRE_FALSE( Cy_MemEqual( nDst, nSrc, sizeof( nSrc ) ) );
    REQUIRE( Cy_MemCompare( nDst, nSrc, sizeof( nSrc ) ) != 0 );

    REQUIRE( Cy_MemCopy( nullptr, nullptr, 0u ) == nullptr );
    REQUIRE( Cy_MemCompare( nullptr, nullptr, 0u ) == 0 );
}

TEST_CASE( "MemoryOps move handles overlapping byte ranges", "[CypherCommon][Tier0][MemoryOps]" )
{
    u8 nForward[] = { 0u, 1u, 2u, 3u, 4u, 5u };
    Cy_MemMove( nForward + 2u, nForward, 4u );
    REQUIRE( nForward[0] == 0u );
    REQUIRE( nForward[1] == 1u );
    REQUIRE( nForward[2] == 0u );
    REQUIRE( nForward[3] == 1u );
    REQUIRE( nForward[4] == 2u );
    REQUIRE( nForward[5] == 3u );

    u8 nBackward[] = { 0u, 1u, 2u, 3u, 4u, 5u };
    Cy_MemMove( nBackward, nBackward + 2u, 4u );
    REQUIRE( nBackward[0] == 2u );
    REQUIRE( nBackward[1] == 3u );
    REQUIRE( nBackward[2] == 4u );
    REQUIRE( nBackward[3] == 5u );
    REQUIRE( nBackward[4] == 4u );
    REQUIRE( nBackward[5] == 5u );
}

TEST_CASE( "MemoryOps set zero and zero inspection handle byte ranges", "[CypherCommon][Tier0][MemoryOps]" )
{
    u8 nBytes[8] = {};
    REQUIRE( Cy_MemIsZero( nBytes, sizeof( nBytes ) ) );

    REQUIRE( Cy_MemSet( nBytes, 0x7F, sizeof( nBytes ) ) == nBytes );
    REQUIRE_FALSE( Cy_MemIsZero( nBytes, sizeof( nBytes ) ) );
    for ( u8 nByte : nBytes ) {
        REQUIRE( nByte == 0x7Fu );
    }

    REQUIRE( Cy_MemZero( nBytes, sizeof( nBytes ) ) == nBytes );
    REQUIRE( Cy_MemIsZero( nBytes, sizeof( nBytes ) ) );

    REQUIRE( Cy_MemIsZero( nullptr, 0u ) );
    REQUIRE_FALSE( Cy_MemIsZero( nullptr, 1u ) );
}

TEST_CASE( "MemoryOps pointer range checks use half-open ranges", "[CypherCommon][Tier0][MemoryOps]" )
{
    u8 nBytes[16] = {};

    REQUIRE( Cy_MemPointerInRange( nBytes, nBytes, sizeof( nBytes ) ) );
    REQUIRE( Cy_MemPointerInRange( nBytes + 7u, nBytes, sizeof( nBytes ) ) );
    REQUIRE( Cy_MemPointerInRange( nBytes + 15u, nBytes, sizeof( nBytes ) ) );
    REQUIRE_FALSE( Cy_MemPointerInRange( nBytes + 16u, nBytes, sizeof( nBytes ) ) );
    REQUIRE_FALSE( Cy_MemPointerInRange( nullptr, nBytes, sizeof( nBytes ) ) );
    REQUIRE_FALSE( Cy_MemPointerInRange( nBytes, nullptr, sizeof( nBytes ) ) );
    REQUIRE_FALSE( Cy_MemPointerInRange( nBytes, nBytes, 0u ) );
}

TEST_CASE( "MemoryOps range overlap detects overlap but not touching ranges", "[CypherCommon][Tier0][MemoryOps]" )
{
    u8 nBytes[16] = {};

    REQUIRE( Cy_MemRangesOverlap( nBytes, 8u, nBytes, 8u ) );
    REQUIRE( Cy_MemRangesOverlap( nBytes, 8u, nBytes + 4u, 8u ) );
    REQUIRE( Cy_MemRangesOverlap( nBytes + 4u, 8u, nBytes, 8u ) );

    REQUIRE_FALSE( Cy_MemRangesOverlap( nBytes, 4u, nBytes + 4u, 4u ) );
    REQUIRE_FALSE( Cy_MemRangesOverlap( nBytes, 4u, nBytes + 8u, 4u ) );
    REQUIRE_FALSE( Cy_MemRangesOverlap( nBytes, 0u, nBytes, 8u ) );
    REQUIRE_FALSE( Cy_MemRangesOverlap( nullptr, 8u, nBytes, 8u ) );
}

TEST_CASE( "MemoryOps typed helpers clear copy move and inspect trivially copyable data", "[CypherCommon][Tier0][MemoryOps]" )
{
    memoryops_test_struct_t value{ 10u, 20u };
    Cy_ZeroStruct( value );
    REQUIRE( value.nA == 0u );
    REQUIRE( value.nB == 0u );
    REQUIRE( Cy_StructIsZero( value ) );

    u32 nFixedValues[] = { 1u, 2u, 3u, 4u };
    Cy_ZeroArray( nFixedValues );
    REQUIRE( Cy_ArrayIsZero( nFixedValues, 4u ) );

    u32 nPointerValues[] = { 1u, 2u, 3u, 4u };
    Cy_ZeroArray( nPointerValues, 4u );
    REQUIRE( Cy_ArrayIsZero( nPointerValues, 4u ) );

    u32 nSrc[] = { 7u, 8u, 9u, 10u };
    u32 nDst[] = { 0u, 0u, 0u, 0u };
    Cy_CopyArray( nDst, nSrc, 4u );
    REQUIRE( nDst[0] == 7u );
    REQUIRE( nDst[1] == 8u );
    REQUIRE( nDst[2] == 9u );
    REQUIRE( nDst[3] == 10u );

    u32 nMoveValues[] = { 1u, 2u, 3u, 4u, 5u };
    Cy_MoveArray( nMoveValues + 1u, nMoveValues, 4u );
    REQUIRE( nMoveValues[0] == 1u );
    REQUIRE( nMoveValues[1] == 1u );
    REQUIRE( nMoveValues[2] == 2u );
    REQUIRE( nMoveValues[3] == 3u );
    REQUIRE( nMoveValues[4] == 4u );

    const memoryops_test_struct_t zeroValue{};
    REQUIRE( Cy_StructIsZero( zeroValue ) );
    REQUIRE_FALSE( Cy_ArrayIsZero( nSrc, 4u ) );
    REQUIRE( Cy_ArrayIsZero( static_cast<const u32 *>( nullptr ), 0u ) );
}

TEST_CASE( "MemoryOps typed byte counts reject arithmetic overflow", "[CypherCommon][Tier0][MemoryOps]" )
{
    usize nByteCount = CY_INVALID_SIZE;

    REQUIRE( Cy_TryArrayByteCount<u32>( 0u, nByteCount ) );
    REQUIRE( nByteCount == 0u );
    REQUIRE( Cy_TryArrayByteCount<u32>( 4u, nByteCount ) );
    REQUIRE( nByteCount == 4u * sizeof( u32 ) );

    const usize nOverflowCount = ( CY_USIZE_MAX / sizeof( u64 ) ) + 1u;
    REQUIRE_FALSE( Cy_TryArrayByteCount<u64>( nOverflowCount, nByteCount ) );
    REQUIRE( nByteCount == 0u );
    REQUIRE_FALSE( Cy_ArrayIsZero( static_cast<const u64 *>( nullptr ), nOverflowCount ) );
}

TEST_CASE( "MemoryOps zero-byte operations do not access their pointers", "[CypherCommon][Tier0][MemoryOps]" )
{
    REQUIRE( Cy_MemCopy( nullptr, nullptr, 0u ) == nullptr );
    REQUIRE( Cy_MemMove( nullptr, nullptr, 0u ) == nullptr );
    REQUIRE( Cy_MemSet( nullptr, 0xFFu, 0u ) == nullptr );
    REQUIRE( Cy_MemZero( nullptr, 0u ) == nullptr );
    REQUIRE( Cy_MemCompare( nullptr, nullptr, 0u ) == 0 );
    REQUIRE( Cy_MemEqual( nullptr, nullptr, 0u ) );
}
