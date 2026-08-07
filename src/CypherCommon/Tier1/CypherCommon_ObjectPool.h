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
    object_pool_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( object_pool_t );

    memory_pool_t memory{};
    byte *pOccupied{ nullptr };
    usize cbOccupied{ 0u };
    usize nLiveCount{ 0u };
};

template <typename type_t>
CYPHER_NODISCARD bool_t ObjectPool_Init(
    object_pool_t<type_t> *pPool,
    const allocator_t *pAllocator,
    usize nObjectCount ) noexcept;

template <typename type_t>
void ObjectPool_Shutdown( object_pool_t<type_t> *pPool ) noexcept;

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

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_OBJECTPOOL_H
