//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ObjectPool.inl
//  Purpose: Implements typed objects over an owning fixed-block pool.
//  Details: MemoryPool's occupancy bitmap is the single source of liveness.
//           Reset and shutdown destroy every live object before recycling storage.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_OBJECTPOOL_INL
#define CYPHER_COMMON_TIER1_OBJECTPOOL_INL
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <new>
#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
bool_t ObjectPool_IsCanonicalEmpty(
    const object_pool_t<type_t> &pool ) noexcept
{
    return !MemoryPool_IsValid( &pool.memory ) &&
           Allocator_OwnedIsValid( &pool.memory.backing ) &&
           pool.memory.backing.pData == nullptr &&
           !BlockMemory_IsValid( &pool.memory.blocks );
}

template <typename type_t>
bool_t ObjectPool_IsInitialized(
    const object_pool_t<type_t> *pPool ) noexcept
{
    return pPool != nullptr &&
           pPool->memory.backing.pData != nullptr &&
           pPool->memory.blocks.memory.pData != nullptr;
}

template <typename type_t>
void ObjectPool_DestroyLiveObjects(
    object_pool_t<type_t> &pool ) noexcept
{
    static_assert(
        std::is_nothrow_destructible_v<type_t>,
        "ObjectPool element destruction must not throw." );

    block_memory_t &blocks = pool.memory.blocks;
    for ( usize iBlock = 0u; iBlock < blocks.nBlockCount; ++iBlock ) {
        auto *pObject = reinterpret_cast<type_t *>(
            blocks.memory.pData + iBlock * blocks.cbBlockStride );
        if ( MemoryPool_IsAllocated( &pool.memory, pObject ) ) {
            pObject->~type_t();
        }
    }
}

} // namespace detail

template <typename type_t>
object_pool_t<type_t>::~object_pool_t() noexcept
{
    ObjectPool_Shutdown( this );
}

template <typename type_t>
bool_t ObjectPool_Init(
    object_pool_t<type_t> *pPool,
    const allocator_t *pAllocator,
    usize nObjectCount ) noexcept
{
    const bool_t bValidDestination =
        pPool != nullptr && detail::ObjectPool_IsCanonicalEmpty( *pPool );
    CY_ASSERT_MSG(
        bValidDestination,
        "ObjectPool_Init requires a canonical empty destination." );
    if ( !bValidDestination ) {
        return CY_FALSE;
    }

    return MemoryPool_Init(
        &pPool->memory,
        pAllocator,
        sizeof( type_t ),
        alignof( type_t ),
        nObjectCount );
}

template <typename type_t>
void ObjectPool_Shutdown( object_pool_t<type_t> *pPool ) noexcept
{
    const bool_t bValidObject = pPool != nullptr;
    CY_ASSERT_MSG( bValidObject, "ObjectPool_Shutdown requires a pool object." );
    if ( !bValidObject ) {
        return;
    }

    if ( MemoryPool_IsValid( &pPool->memory ) ) {
        detail::ObjectPool_DestroyLiveObjects( *pPool );
    }
    MemoryPool_Shutdown( &pPool->memory );
}

template <typename type_t>
void ObjectPool_Reset( object_pool_t<type_t> *pPool ) noexcept
{
    const bool_t bValidPool = ObjectPool_IsValid( pPool );
    CY_ASSERT_MSG( bValidPool, "ObjectPool_Reset requires an initialized pool." );
    if ( !bValidPool ) {
        return;
    }

    detail::ObjectPool_DestroyLiveObjects( *pPool );
    MemoryPool_Reset( &pPool->memory );
}

template <typename type_t>
bool_t ObjectPool_IsValid(
    const object_pool_t<type_t> *pPool ) noexcept
{
    return pPool != nullptr &&
           MemoryPool_IsValid( &pPool->memory ) &&
           pPool->memory.blocks.cbPayload >= sizeof( type_t ) &&
           pPool->memory.blocks.nAlignment >= alignof( type_t );
}

template <typename type_t, typename... args_t>
type_t *ObjectPool_Create(
    object_pool_t<type_t> *pPool,
    args_t &&... args ) noexcept
{
    static_assert(
        std::is_nothrow_constructible_v<type_t, args_t...>,
        "ObjectPool_Create construction must not throw." );

    const bool_t bInitialized = detail::ObjectPool_IsInitialized( pPool );
    CY_ASSERT_MSG(
        bInitialized && ObjectPool_IsValid( pPool ),
        "ObjectPool_Create requires an initialized pool." );
    if ( !bInitialized ) {
        return nullptr;
    }

    void *pStorage = MemoryPool_Allocate( &pPool->memory );
    if ( pStorage == nullptr ) {
        return nullptr;
    }

    return ::new ( pStorage ) type_t( static_cast<args_t &&>( args )... );
}

template <typename type_t>
bool_t ObjectPool_Destroy(
    object_pool_t<type_t> *pPool,
    type_t *pObject ) noexcept
{
    static_assert(
        std::is_nothrow_destructible_v<type_t>,
        "ObjectPool element destruction must not throw." );

    const bool_t bInitialized = detail::ObjectPool_IsInitialized( pPool );
    CY_ASSERT_MSG(
        bInitialized && ObjectPool_IsValid( pPool ),
        "ObjectPool_Destroy requires an initialized pool." );
    const bool_t bLiveObject =
        bInitialized &&
        MemoryPool_IsAllocated( &pPool->memory, pObject );
    CY_ASSERT_MSG(
        bLiveObject,
        "ObjectPool_Destroy requires a live object owned by this pool." );
    if ( !bLiveObject ) {
        return CY_FALSE;
    }

    pObject->~type_t();
    return MemoryPool_Free( &pPool->memory, pObject );
}

template <typename type_t>
bool_t ObjectPool_Owns(
    const object_pool_t<type_t> *pPool,
    const type_t *pObject ) noexcept
{
    const bool_t bInitialized = detail::ObjectPool_IsInitialized( pPool );
    CY_ASSERT_MSG(
        !bInitialized || ObjectPool_IsValid( pPool ),
        "ObjectPool_Owns received a corrupted pool." );
    return bInitialized && MemoryPool_Owns( &pPool->memory, pObject );
}

template <typename type_t>
bool_t ObjectPool_IsLive(
    const object_pool_t<type_t> *pPool,
    const type_t *pObject ) noexcept
{
    const bool_t bInitialized = detail::ObjectPool_IsInitialized( pPool );
    CY_ASSERT_MSG(
        !bInitialized || ObjectPool_IsValid( pPool ),
        "ObjectPool_IsLive received a corrupted pool." );
    return bInitialized &&
           MemoryPool_IsAllocated( &pPool->memory, pObject );
}

template <typename type_t>
usize ObjectPool_Capacity(
    const object_pool_t<type_t> *pPool ) noexcept
{
    const bool_t bInitialized = detail::ObjectPool_IsInitialized( pPool );
    CY_ASSERT_MSG(
        !bInitialized || ObjectPool_IsValid( pPool ),
        "ObjectPool_Capacity received a corrupted pool." );
    return bInitialized ? MemoryPool_Capacity( &pPool->memory ) : 0u;
}

template <typename type_t>
usize ObjectPool_LiveCount(
    const object_pool_t<type_t> *pPool ) noexcept
{
    const bool_t bInitialized = detail::ObjectPool_IsInitialized( pPool );
    CY_ASSERT_MSG(
        !bInitialized || ObjectPool_IsValid( pPool ),
        "ObjectPool_LiveCount received a corrupted pool." );
    return bInitialized
        ? MemoryPool_AllocatedCount( &pPool->memory )
        : 0u;
}

template <typename type_t>
usize ObjectPool_FreeCount(
    const object_pool_t<type_t> *pPool ) noexcept
{
    const bool_t bInitialized = detail::ObjectPool_IsInitialized( pPool );
    CY_ASSERT_MSG(
        !bInitialized || ObjectPool_IsValid( pPool ),
        "ObjectPool_FreeCount received a corrupted pool." );
    return bInitialized ? MemoryPool_FreeCount( &pPool->memory ) : 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_OBJECTPOOL_INL
