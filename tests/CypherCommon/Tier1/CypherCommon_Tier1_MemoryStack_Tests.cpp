//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_MemoryStack_Tests.cpp
//  Purpose: Tests linear allocation over caller-owned memory.
//  Details: Protects absolute alignment, transactional exhaustion, marker rewind,
//           zero fill, overflow checks, ownership, and persistent high-water stats.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_MemoryStack.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_memoryStackAssertCount = 0u;

assert_action_t CaptureMemoryStackAssert( const assert_info_t & ) noexcept
{
    ++g_memoryStackAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "MemoryStack aligns absolute addresses from misaligned storage",
           "[CypherCommon][Tier1][MemoryStack]" )
{
    alignas( 64 ) byte storage[512]{};
    memory_stack_t stack{};
    REQUIRE( MemoryStack_Init(
        &stack,
        { storage + 1u, sizeof( storage ) - 1u } ) );

    void *pFirst = MemoryStack_Allocate( &stack, 3u, 8u );
    void *pSecond = MemoryStack_Allocate( &stack, 17u, 32u );
    REQUIRE( pFirst != nullptr );
    REQUIRE( pSecond != nullptr );
    REQUIRE( Cy_AlignIsPointerAligned( pFirst, 8u ) );
    REQUIRE( Cy_AlignIsPointerAligned( pSecond, 32u ) );
    REQUIRE( reinterpret_cast<uintptr>( pSecond ) >=
             reinterpret_cast<uintptr>( pFirst ) + 3u );
    REQUIRE( MemoryStack_Owns( &stack, pFirst ) );
    REQUIRE( MemoryStack_Used( &stack ) > 20u );
    REQUIRE( MemoryStack_HighWater( &stack ) == MemoryStack_Used( &stack ) );
}

TEST_CASE( "MemoryStack failed allocation leaves the marker unchanged",
           "[CypherCommon][Tier1][MemoryStack]" )
{
    byte storage[32]{};
    memory_stack_t stack{};
    REQUIRE( MemoryStack_Init( &stack, { storage, sizeof( storage ) } ) );
    REQUIRE( MemoryStack_Allocate( &stack, 16u, 8u ) != nullptr );
    const memory_stack_marker_t marker = MemoryStack_Mark( &stack );

    REQUIRE( MemoryStack_Allocate( &stack, 64u, 8u ) == nullptr );
    REQUIRE( MemoryStack_Mark( &stack ) == marker );
    REQUIRE( MemoryStack_Remaining( &stack ) == sizeof( storage ) - marker );
}

TEST_CASE( "MemoryStack markers restore and reuse the same address",
           "[CypherCommon][Tier1][MemoryStack]" )
{
    alignas( 32 ) byte storage[128]{};
    memory_stack_t stack{};
    REQUIRE( MemoryStack_Init( &stack, { storage, sizeof( storage ) } ) );
    REQUIRE( MemoryStack_Allocate( &stack, 8u, 8u ) != nullptr );
    const memory_stack_marker_t marker = MemoryStack_Mark( &stack );
    void *pTemporary = MemoryStack_Allocate( &stack, 24u, 16u );
    REQUIRE( pTemporary != nullptr );

    REQUIRE( MemoryStack_Restore( &stack, marker ) );
    void *pReused = MemoryStack_Allocate( &stack, 24u, 16u );
    REQUIRE( pReused == pTemporary );

    const usize cbPeak = MemoryStack_HighWater( &stack );
    MemoryStack_Reset( &stack );
    REQUIRE( MemoryStack_Used( &stack ) == 0u );
    REQUIRE( MemoryStack_HighWater( &stack ) == cbPeak );
    MemoryStack_ClearHighWater( &stack );
    REQUIRE( MemoryStack_HighWater( &stack ) == 0u );
}

TEST_CASE( "MemoryStack zeroed and typed allocation helpers preserve contracts",
           "[CypherCommon][Tier1][MemoryStack]" )
{
    alignas( 64 ) byte storage[256];
    Cy_MemSet( storage, 0xCCu, sizeof( storage ) );
    memory_stack_t stack{};
    REQUIRE( MemoryStack_Init( &stack, { storage, sizeof( storage ) } ) );

    void *pZeroed = MemoryStack_AllocateZeroed( &stack, 32u, 16u );
    REQUIRE( pZeroed != nullptr );
    REQUIRE( Cy_MemIsZero( pZeroed, 32u ) );

    u64 *pValues = MemoryStack_AllocateArrayStorage<u64>( &stack, 8u );
    REQUIRE( pValues != nullptr );
    REQUIRE( Cy_AlignIsPointerAligned( pValues, alignof( u64 ) ) );

    const byte_span_t allocated = MemoryStack_AllocatedSpan( &stack );
    const byte_span_t remaining = MemoryStack_RemainingSpan( &stack );
    REQUIRE( allocated.nCount == MemoryStack_Used( &stack ) );
    REQUIRE( remaining.nCount == MemoryStack_Remaining( &stack ) );
    REQUIRE( allocated.nCount + remaining.nCount == sizeof( storage ) );
}

TEST_CASE( "MemoryStack rejects invalid alignment, markers, and array overflow",
           "[CypherCommon][Tier1][MemoryStack]" )
{
    byte storage[64]{};
    memory_stack_t stack{};
    REQUIRE( MemoryStack_Init( &stack, { storage, sizeof( storage ) } ) );

    g_memoryStackAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureMemoryStackAssert );

    REQUIRE( MemoryStack_Allocate( &stack, 8u, 3u ) == nullptr );
    REQUIRE_FALSE( MemoryStack_Restore( &stack, 1u ) );
    REQUIRE( MemoryStack_AllocateArrayStorage<u64>(
                 &stack,
                 CY_USIZE_MAX ) == nullptr );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_memoryStackAssertCount ==
        3u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
