//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ObjectPool.h
//  Purpose: Declares typed object construction over a fixed-block pool.
//  Details: ObjectPool owns storage and runs constructors/destructors explicitly.
//           Destroying the pool destroys every still-live object.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_OBJECTPOOL_H
#define CYPHER_COMMON_TIER1_OBJECTPOOL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_MemoryPool.h"

namespace cypher::common
{

template <typename type_t>
struct object_pool_t {
    static_assert( is_object_v<type_t>, "object_pool_t requires an object type." );
    static_assert( !is_array_v<type_t>, "object_pool_t does not store array elements." );

    object_pool_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( object_pool_t );
    ~object_pool_t() noexcept;

    memory_pool_t memory{};
};

template <typename type_t>
CYPHER_NODISCARD bool_t ObjectPool_Init(
    object_pool_t<type_t> *pPool,
    const allocator_t *pAllocator,
    usize nObjectCount ) noexcept;

template <typename type_t>
void ObjectPool_Shutdown( object_pool_t<type_t> *pPool ) noexcept;

template <typename type_t>
void ObjectPool_Reset( object_pool_t<type_t> *pPool ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t ObjectPool_IsValid(
    const object_pool_t<type_t> *pPool ) noexcept;

template <typename type_t, typename... args_t>
CYPHER_NODISCARD type_t *ObjectPool_Create(
    object_pool_t<type_t> *pPool,
    args_t &&... args ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t ObjectPool_Destroy(
    object_pool_t<type_t> *pPool,
    type_t *pObject ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t ObjectPool_Owns(
    const object_pool_t<type_t> *pPool,
    const type_t *pObject ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t ObjectPool_IsLive(
    const object_pool_t<type_t> *pPool,
    const type_t *pObject ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize ObjectPool_Capacity(
    const object_pool_t<type_t> *pPool ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize ObjectPool_LiveCount(
    const object_pool_t<type_t> *pPool ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize ObjectPool_FreeCount(
    const object_pool_t<type_t> *pPool ) noexcept;

} // namespace cypher::common

#include "CypherCommon_ObjectPool.inl"

#endif // CYPHER_COMMON_TIER1_OBJECTPOOL_H
