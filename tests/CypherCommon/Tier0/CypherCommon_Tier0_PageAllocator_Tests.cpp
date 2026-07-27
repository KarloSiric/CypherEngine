//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_PageAllocator_Tests.cpp
//  Purpose: Tests Tier0 linear page allocator behavior.
//  Details: Verifies zero-state requirements, checked reservation sizing, linear
//           commits, capacity limits, reset reuse, and idempotent shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PageAllocator.h"
#include "CypherCommon_PlatformMemory.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "PageAllocator validates initialization state and request size", "[CypherCommon][Tier0][PageAllocator]" )
{
    REQUIRE_FALSE( Cy_PageAllocatorInit( nullptr, 4096u ) );

    page_allocator_t allocator{};
    REQUIRE_FALSE( Cy_PageAllocatorInit( &allocator, 0u ) );
    REQUIRE_FALSE( Cy_PageAllocatorInit( &allocator, CY_USIZE_MAX ) );

    const usize nPageSize = Cy_PlatformMemoryGetInfo().nPageSize;
    REQUIRE( Cy_PageAllocatorInit( &allocator, nPageSize ) );
    void *pReservedBase = allocator.pReservedBase;
    REQUIRE_FALSE( Cy_PageAllocatorInit( &allocator, nPageSize ) );
    REQUIRE( allocator.pReservedBase == pReservedBase );
    REQUIRE( Cy_PageAllocatorShutdown( &allocator ) );
}

TEST_CASE( "PageAllocator commits linearly resets and reuses its reservation", "[CypherCommon][Tier0][PageAllocator]" )
{
    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    page_allocator_t allocator{};
    REQUIRE( Cy_PageAllocatorInit(
        &allocator,
        info.nAllocationGranularity * 2u ) );

    void *pFirst = Cy_PageAllocatorCommit( &allocator, 1u );
    REQUIRE( pFirst == allocator.pReservedBase );
    REQUIRE( allocator.nCommittedByteCount == info.nPageSize );

    void *pSecond = Cy_PageAllocatorCommit( &allocator, info.nPageSize );
    REQUIRE( pSecond == static_cast<u8 *>( pFirst ) + info.nPageSize );
    REQUIRE( allocator.nCommittedByteCount == info.nPageSize * 2u );

    REQUIRE( Cy_PageAllocatorCommit(
        &allocator,
        allocator.nReservedByteCount ) == nullptr );

    REQUIRE( Cy_PageAllocatorReset( &allocator ) );
    REQUIRE( allocator.nCommittedByteCount == 0u );
    REQUIRE( Cy_PageAllocatorReset( &allocator ) );

    void *pReused = Cy_PageAllocatorCommit( &allocator, info.nPageSize );
    REQUIRE( pReused == pFirst );
    REQUIRE( Cy_PageAllocatorShutdown( &allocator ) );
    REQUIRE( allocator.pReservedBase == nullptr );
    REQUIRE( allocator.nReservedByteCount == 0u );
    REQUIRE( Cy_PageAllocatorShutdown( &allocator ) );
}

TEST_CASE( "PageAllocator rejects corrupted reservation bookkeeping", "[CypherCommon][Tier0][PageAllocator]" )
{
    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    page_allocator_t allocator{};
    REQUIRE( Cy_PageAllocatorInit( &allocator, info.nAllocationGranularity ) );

    const usize nOriginalCommittedByteCount = allocator.nCommittedByteCount;
    allocator.nCommittedByteCount = 1u;
    REQUIRE( Cy_PageAllocatorCommit( &allocator, 1u ) == nullptr );
    REQUIRE_FALSE( Cy_PageAllocatorReset( &allocator ) );

    allocator.nCommittedByteCount = nOriginalCommittedByteCount;
    REQUIRE( Cy_PageAllocatorShutdown( &allocator ) );
}
