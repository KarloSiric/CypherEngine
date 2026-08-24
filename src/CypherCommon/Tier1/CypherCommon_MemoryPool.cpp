//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryPool.cpp
//  Purpose: Implements an owning fixed-block memory pool.
//  Details: One explicit allocator-backed ownership record supplies storage to
//           BlockMemory and guarantees transactional initialization and shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Memory Pool Implementation Notes

Storage ownership remains with the configured backing allocator or buffer. Reset and release
operations invalidate outstanding allocations according to the lifetime documented by the public
API.
================
*/

#include "CypherCommon_MemoryPool.h"

namespace cypher::common
{

namespace
{

bool_t IsEmptyPool( const memory_pool_t &pool ) noexcept
{
    return !BlockMemory_IsValid( &pool.blocks ) &&
           Allocator_OwnedIsValid( &pool.backing ) &&
           pool.backing.pData == nullptr;
}

bool_t IsInitializedPool( const memory_pool_t *pPool ) noexcept
{
    return pPool != nullptr &&
           pPool->backing.pData != nullptr &&
           pPool->blocks.memory.pData != nullptr;
}

} // namespace

bool_t MemoryPool_Init(
    memory_pool_t *pPool,
    const allocator_t *pAllocator,
    usize cbPayload,
    usize nAlignment,
    usize nBlockCount ) noexcept
{
    const bool_t bValidDestination =
        pPool != nullptr && IsEmptyPool( *pPool );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "MemoryPool_Init requires an empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "MemoryPool_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    const usize cbRequired = BlockMemory_RequiredBytes(
        cbPayload,
        nAlignment,
        nBlockCount );
    const bool_t bValidLayout = cbRequired > 0u;
    CY_ASSERT_MSG(
        bValidLayout,
        "MemoryPool_Init received an invalid or overflowing block layout." );
    if ( !bValidLayout ) {
        return CY_FALSE;
    }

    // Free-list links live in each block, so backing alignment must satisfy both
    // the requested payload and pointer-sized internal metadata.
    const usize nBackingAlignment = nAlignment < alignof( void * )
        ? alignof( void * )
        : nAlignment;
    if ( !Allocator_AllocateOwned(
             &pPool->backing,
             pAllocator,
             cbRequired,
             nBackingAlignment ) ) {
        return CY_FALSE;
    }

    // BlockMemory borrows this span; backing retains sole ownership for shutdown.
    const byte_span_t storage{
        static_cast<byte *>( pPool->backing.pData ),
        pPool->backing.cbSize
    };
    if ( !BlockMemory_Init(
             &pPool->blocks,
             storage,
             cbPayload,
             nAlignment,
             nBlockCount ) ) {
        Allocator_FreeOwned( &pPool->backing );
        return CY_FALSE;
    }

    return CY_TRUE;
}

void MemoryPool_Shutdown( memory_pool_t *pPool ) noexcept
{
    const bool_t bValidPoolObject = pPool != nullptr;
    CY_ASSERT_MSG(
        bValidPoolObject,
        "MemoryPool_Shutdown requires a pool object." );
    if ( !bValidPoolObject ) {
        return;
    }

    if ( BlockMemory_IsValid( &pPool->blocks ) ) {
        BlockMemory_Shutdown( &pPool->blocks );
    } else {
        pPool->blocks = {};
    }
    Allocator_FreeOwned( &pPool->backing );
}

void MemoryPool_Reset( memory_pool_t *pPool ) noexcept
{
    const bool_t bValidPool = MemoryPool_IsValid( pPool );
    CY_ASSERT_MSG( bValidPool, "MemoryPool_Reset requires an initialized pool." );
    if ( bValidPool ) {
        BlockMemory_Reset( &pPool->blocks );
    }
}

bool_t MemoryPool_IsValid( const memory_pool_t *pPool ) noexcept
{
    if ( pPool == nullptr ||
         !Allocator_OwnedIsValid( &pPool->backing ) ||
         pPool->backing.pData == nullptr ||
         !BlockMemory_IsValid( &pPool->blocks ) ) {
        return CY_FALSE;
    }

    const fixed_memory_t backingMemory{
        static_cast<byte *>( pPool->backing.pData ),
        pPool->backing.cbSize
    };
    // Both bookkeeping and block payload must remain inside the owned allocation.
    return FixedMemory_ContainsRange(
               backingMemory,
               pPool->blocks.pOccupancyBits,
               pPool->blocks.cbOccupancyBits ) &&
           FixedMemory_ContainsRange(
               backingMemory,
               pPool->blocks.memory.pData,
               pPool->blocks.memory.cbSize );
}

void *MemoryPool_Allocate( memory_pool_t *pPool ) noexcept
{
    const bool_t bInitialized = IsInitializedPool( pPool );
    CY_ASSERT_MSG(
        bInitialized && MemoryPool_IsValid( pPool ),
        "MemoryPool_Allocate requires an initialized pool." );
    return bInitialized ? BlockMemory_Allocate( &pPool->blocks ) : nullptr;
}

bool_t MemoryPool_Free( memory_pool_t *pPool, void *pBlock ) noexcept
{
    const bool_t bInitialized = IsInitializedPool( pPool );
    CY_ASSERT_MSG(
        bInitialized && MemoryPool_IsValid( pPool ),
        "MemoryPool_Free requires an initialized pool." );
    return bInitialized && BlockMemory_Free( &pPool->blocks, pBlock );
}

bool_t MemoryPool_Owns(
    const memory_pool_t *pPool,
    const void *pBlock ) noexcept
{
    const bool_t bInitialized = IsInitializedPool( pPool );
    CY_ASSERT_MSG(
        !bInitialized || MemoryPool_IsValid( pPool ),
        "MemoryPool_Owns received a corrupted pool." );
    return bInitialized &&
           BlockMemory_Owns( &pPool->blocks, pBlock );
}

bool_t MemoryPool_IsAllocated(
    const memory_pool_t *pPool,
    const void *pBlock ) noexcept
{
    const bool_t bInitialized = IsInitializedPool( pPool );
    CY_ASSERT_MSG(
        !bInitialized || MemoryPool_IsValid( pPool ),
        "MemoryPool_IsAllocated received a corrupted pool." );
    return bInitialized &&
           BlockMemory_IsAllocated( &pPool->blocks, pBlock );
}

usize MemoryPool_Capacity( const memory_pool_t *pPool ) noexcept
{
    const bool_t bInitialized = IsInitializedPool( pPool );
    CY_ASSERT_MSG(
        !bInitialized || MemoryPool_IsValid( pPool ),
        "MemoryPool_Capacity received a corrupted pool." );
    return bInitialized
        ? BlockMemory_Capacity( &pPool->blocks )
        : 0u;
}

usize MemoryPool_FreeCount( const memory_pool_t *pPool ) noexcept
{
    const bool_t bInitialized = IsInitializedPool( pPool );
    CY_ASSERT_MSG(
        !bInitialized || MemoryPool_IsValid( pPool ),
        "MemoryPool_FreeCount received a corrupted pool." );
    return bInitialized
        ? BlockMemory_FreeCount( &pPool->blocks )
        : 0u;
}

usize MemoryPool_AllocatedCount( const memory_pool_t *pPool ) noexcept
{
    const bool_t bInitialized = IsInitializedPool( pPool );
    CY_ASSERT_MSG(
        !bInitialized || MemoryPool_IsValid( pPool ),
        "MemoryPool_AllocatedCount received a corrupted pool." );
    return bInitialized
        ? BlockMemory_AllocatedCount( &pPool->blocks )
        : 0u;
}

usize MemoryPool_HighWaterCount( const memory_pool_t *pPool ) noexcept
{
    const bool_t bInitialized = IsInitializedPool( pPool );
    CY_ASSERT_MSG(
        !bInitialized || MemoryPool_IsValid( pPool ),
        "MemoryPool_HighWaterCount received a corrupted pool." );
    return bInitialized
        ? BlockMemory_HighWaterCount( &pPool->blocks )
        : 0u;
}

} // namespace cypher::common
