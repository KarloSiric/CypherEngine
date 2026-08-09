//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_MemoryPool_Tests.cpp
//  Purpose: Tests owning fixed-block memory pools.
//  Details: Protects one-allocation ownership, OOM rollback, alignment, capacity,
//           exhaustion, duplicate-free handling, reset, and exact shutdown release.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_MemoryPool.h"

#include <catch2/catch_test_macros.hpp>

#include <new>

using namespace cypher::common;

namespace
{

struct pool_allocator_state_t {
    u32 cAllocateCalls{};
    u32 cFreeCalls{};
    bool_t bFailAllocation{ CY_FALSE };
};

usize EffectiveAlignment( usize nAlignment ) noexcept
{
    return nAlignment < CY_ALLOCATOR_DEFAULT_ALIGNMENT
        ? CY_ALLOCATOR_DEFAULT_ALIGNMENT
        : nAlignment;
}

void *PoolAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<pool_allocator_state_t *>( pUserData );
    ++pState->cAllocateCalls;
    if ( pState->bFailAllocation ) {
        return nullptr;
    }

    return ::operator new(
        cbSize,
        static_cast<std::align_val_t>( EffectiveAlignment( nAlignment ) ),
        std::nothrow );
}

void PoolFree(
    void *pUserData,
    void *pMemory,
    usize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<pool_allocator_state_t *>( pUserData );
    ++pState->cFreeCalls;
    ::operator delete(
        pMemory,
        static_cast<std::align_val_t>( EffectiveAlignment( nAlignment ) ) );
}

allocator_t MakePoolAllocator( pool_allocator_state_t *pState ) noexcept
{
    return { PoolAllocate, nullptr, PoolFree, pState };
}

u32 g_memoryPoolAssertCount = 0u;

assert_action_t CaptureMemoryPoolAssert( const assert_info_t & ) noexcept
{
    ++g_memoryPoolAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "MemoryPool owns one allocation and serves aligned blocks",
           "[CypherCommon][Tier1][MemoryPool]" )
{
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<memory_pool_t> );
    STATIC_REQUIRE_FALSE( is_move_constructible_v<memory_pool_t> );

    pool_allocator_state_t state{};
    const allocator_t allocator = MakePoolAllocator( &state );
    memory_pool_t pool{};
    REQUIRE( MemoryPool_Init( &pool, &allocator, 24u, 32u, 32u ) );
    REQUIRE( MemoryPool_IsValid( &pool ) );
    REQUIRE( state.cAllocateCalls == 1u );
    REQUIRE( MemoryPool_Capacity( &pool ) == 32u );

    void *blocks[32]{};
    for ( void *&pBlock : blocks ) {
        pBlock = MemoryPool_Allocate( &pool );
        REQUIRE( pBlock != nullptr );
        REQUIRE( Cy_AlignIsPointerAligned( pBlock, 32u ) );
        REQUIRE( MemoryPool_Owns( &pool, pBlock ) );
        REQUIRE( MemoryPool_IsAllocated( &pool, pBlock ) );
    }
    REQUIRE( MemoryPool_Allocate( &pool ) == nullptr );
    REQUIRE( MemoryPool_AllocatedCount( &pool ) == 32u );
    REQUIRE( MemoryPool_HighWaterCount( &pool ) == 32u );

    for ( void *pBlock : blocks ) {
        REQUIRE( MemoryPool_Free( &pool, pBlock ) );
    }
    REQUIRE( MemoryPool_FreeCount( &pool ) == 32u );

    MemoryPool_Shutdown( &pool );
    REQUIRE_FALSE( MemoryPool_IsValid( &pool ) );
    REQUIRE( state.cFreeCalls == 1u );
    REQUIRE( pool.backing.pData == nullptr );
}

TEST_CASE( "MemoryPool reset returns all raw blocks to the pool",
           "[CypherCommon][Tier1][MemoryPool]" )
{
    memory_pool_t pool{};
    REQUIRE( MemoryPool_Init(
        &pool,
        Allocator_GetSystem(),
        64u,
        16u,
        8u ) );
    REQUIRE( MemoryPool_Allocate( &pool ) != nullptr );
    REQUIRE( MemoryPool_Allocate( &pool ) != nullptr );
    REQUIRE( MemoryPool_AllocatedCount( &pool ) == 2u );

    MemoryPool_Reset( &pool );
    REQUIRE( MemoryPool_AllocatedCount( &pool ) == 0u );
    REQUIRE( MemoryPool_FreeCount( &pool ) == 8u );
    MemoryPool_Shutdown( &pool );
}

TEST_CASE( "MemoryPool allocation failure rolls back to canonical empty",
           "[CypherCommon][Tier1][MemoryPool]" )
{
    pool_allocator_state_t state{};
    state.bFailAllocation = CY_TRUE;
    const allocator_t allocator = MakePoolAllocator( &state );
    memory_pool_t pool{};

    REQUIRE_FALSE( MemoryPool_Init( &pool, &allocator, 32u, 16u, 8u ) );
    REQUIRE( state.cAllocateCalls == 1u );
    REQUIRE( state.cFreeCalls == 0u );
    REQUIRE_FALSE( MemoryPool_IsValid( &pool ) );
    REQUIRE( pool.backing.pData == nullptr );
    REQUIRE_FALSE( BlockMemory_IsValid( &pool.blocks ) );
}

TEST_CASE( "MemoryPool rejects invalid layouts and duplicate initialization",
           "[CypherCommon][Tier1][MemoryPool]" )
{
    memory_pool_t pool{};
    g_memoryPoolAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureMemoryPoolAssert );

    REQUIRE_FALSE( MemoryPool_Init(
        &pool,
        Allocator_GetSystem(),
        0u,
        16u,
        8u ) );
    REQUIRE( MemoryPool_Init(
        &pool,
        Allocator_GetSystem(),
        32u,
        16u,
        8u ) );
    REQUIRE_FALSE( MemoryPool_Init(
        &pool,
        Allocator_GetSystem(),
        32u,
        16u,
        8u ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_memoryPoolAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    MemoryPool_Shutdown( &pool );
}
