//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_BlockMemory_Tests.cpp
//  Purpose: Tests fixed-block allocation over caller-owned memory.
//  Details: Protects layout overflow, alignment, exhaustion, reuse, occupancy,
//           duplicate-free detection, high-water tracking, reset, and shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_BlockMemory.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_blockMemoryAssertCount = 0u;

assert_action_t CaptureBlockMemoryAssert( const assert_info_t & ) noexcept
{
    ++g_blockMemoryAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "BlockMemory reports checked storage requirements",
           "[CypherCommon][Tier1][BlockMemory]" )
{
    REQUIRE( BlockMemory_RequiredBytes( 24u, 16u, 8u ) >= 24u * 8u );
    REQUIRE( BlockMemory_RequiredBytes( 1u, 1u, 1u ) >= sizeof( void * ) );
    REQUIRE( BlockMemory_RequiredBytes( 0u, 16u, 8u ) == 0u );
    REQUIRE( BlockMemory_RequiredBytes( 16u, 3u, 8u ) == 0u );
    REQUIRE( BlockMemory_RequiredBytes( 16u, 16u, 0u ) == 0u );
    REQUIRE( BlockMemory_RequiredBytes( CY_USIZE_MAX, 16u, 2u ) == 0u );
}

TEST_CASE( "BlockMemory allocates unique aligned blocks until exhaustion",
           "[CypherCommon][Tier1][BlockMemory]" )
{
    constexpr usize nBlockCount = 32u;
    constexpr usize cbPayload = 24u;
    constexpr usize nAlignment = 32u;
    alignas( 64 ) byte storage[4096]{};
    block_memory_t memory{};

    REQUIRE( BlockMemory_Init(
        &memory,
        { storage + 1u, sizeof( storage ) - 1u },
        cbPayload,
        nAlignment,
        nBlockCount ) );
    REQUIRE( BlockMemory_IsValid( &memory ) );
    REQUIRE( BlockMemory_Capacity( &memory ) == nBlockCount );
    REQUIRE( BlockMemory_FreeCount( &memory ) == nBlockCount );

    void *blocks[nBlockCount]{};
    for ( usize iBlock = 0u; iBlock < nBlockCount; ++iBlock ) {
        blocks[iBlock] = BlockMemory_Allocate( &memory );
        REQUIRE( blocks[iBlock] != nullptr );
        REQUIRE( Cy_AlignIsPointerAligned( blocks[iBlock], nAlignment ) );
        REQUIRE( BlockMemory_Owns( &memory, blocks[iBlock] ) );
        REQUIRE( BlockMemory_IsAllocated( &memory, blocks[iBlock] ) );

        for ( usize iPrevious = 0u; iPrevious < iBlock; ++iPrevious ) {
            REQUIRE( blocks[iPrevious] != blocks[iBlock] );
        }
    }

    REQUIRE( BlockMemory_Allocate( &memory ) == nullptr );
    REQUIRE( BlockMemory_FreeCount( &memory ) == 0u );
    REQUIRE( BlockMemory_AllocatedCount( &memory ) == nBlockCount );
    REQUIRE( BlockMemory_HighWaterCount( &memory ) == nBlockCount );

    for ( void *pBlock : blocks ) {
        REQUIRE( BlockMemory_Free( &memory, pBlock ) );
    }
    REQUIRE( BlockMemory_FreeCount( &memory ) == nBlockCount );
}

TEST_CASE( "BlockMemory reuses freed blocks and rejects invalid frees",
           "[CypherCommon][Tier1][BlockMemory]" )
{
    alignas( 64 ) byte storage[1024]{};
    block_memory_t memory{};
    REQUIRE( BlockMemory_Init(
        &memory,
        { storage, sizeof( storage ) },
        32u,
        16u,
        8u ) );

    void *pFirst = BlockMemory_Allocate( &memory );
    void *pSecond = BlockMemory_Allocate( &memory );
    REQUIRE( pFirst != nullptr );
    REQUIRE( pSecond != nullptr );
    REQUIRE( BlockMemory_Free( &memory, pFirst ) );

    void *pReused = BlockMemory_Allocate( &memory );
    REQUIRE( pReused == pFirst );

    g_blockMemoryAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureBlockMemoryAssert );

    REQUIRE( BlockMemory_Free( &memory, pReused ) );
    REQUIRE_FALSE( BlockMemory_Free( &memory, pReused ) );
    REQUIRE_FALSE( BlockMemory_Free( &memory, static_cast<byte *>( pSecond ) + 1u ) );
    REQUIRE_FALSE( BlockMemory_Free( &memory, storage + sizeof( storage ) - 1u ) );
    REQUIRE_FALSE( BlockMemory_Free( &memory, nullptr ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_blockMemoryAssertCount ==
        3u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( BlockMemory_Free( &memory, pSecond ) );
}

TEST_CASE( "BlockMemory reset invalidates outstanding allocations in bulk",
           "[CypherCommon][Tier1][BlockMemory]" )
{
    alignas( 64 ) byte storage[1024]{};
    block_memory_t memory{};
    REQUIRE( BlockMemory_Init(
        &memory,
        { storage, sizeof( storage ) },
        16u,
        16u,
        16u ) );

    void *pBlock = BlockMemory_Allocate( &memory );
    REQUIRE( pBlock != nullptr );
    REQUIRE( BlockMemory_IsAllocated( &memory, pBlock ) );
    REQUIRE( BlockMemory_HighWaterCount( &memory ) == 1u );

    BlockMemory_Reset( &memory );
    REQUIRE_FALSE( BlockMemory_IsAllocated( &memory, pBlock ) );
    REQUIRE( BlockMemory_FreeCount( &memory ) == 16u );
    REQUIRE( BlockMemory_HighWaterCount( &memory ) == 0u );

    BlockMemory_Shutdown( &memory );
    REQUIRE_FALSE( BlockMemory_IsValid( &memory ) );
    REQUIRE( memory.memory.pData == nullptr );
}

TEST_CASE( "BlockMemory init rejects insufficient and dirty destinations",
           "[CypherCommon][Tier1][BlockMemory]" )
{
    alignas( 64 ) byte storage[512]{};
    const usize cbRequired = BlockMemory_RequiredBytes( 32u, 16u, 8u );
    REQUIRE( cbRequired > 0u );

    g_blockMemoryAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureBlockMemoryAssert );

    block_memory_t memory{};
    REQUIRE_FALSE( BlockMemory_Init(
        &memory,
        { storage, cbRequired - 1u },
        32u,
        16u,
        8u ) );
    REQUIRE( BlockMemory_Init(
        &memory,
        { storage, sizeof( storage ) },
        32u,
        16u,
        8u ) );
    REQUIRE_FALSE( BlockMemory_Init(
        &memory,
        { storage, sizeof( storage ) },
        32u,
        16u,
        8u ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_blockMemoryAssertCount ==
        1u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
