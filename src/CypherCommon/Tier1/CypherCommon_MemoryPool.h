//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryPool.h
//  Purpose: Declares an owning fixed-block pool wrapper.
//  Details: MemoryPool allocates one backing region through allocator_t and delegates
//           block management to block_memory_t. It does not construct typed objects.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Memory Pool Contract

The pool owns one backing allocation and divides it into equal-sized blocks. Individual blocks do
not run constructors or destructors; ObjectPool adds that typed lifetime layer. Reset invalidates
every outstanding block at once and must be used only when no borrower can retain a pointer.
================
*/

#ifndef CYPHER_COMMON_TIER1_MEMORYPOOL_H
#define CYPHER_COMMON_TIER1_MEMORYPOOL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_BlockMemory.h"

namespace cypher::common
{

struct memory_pool_t {
    memory_pool_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( memory_pool_t );

    block_memory_t blocks{};          // Free-list and occupancy state over backing.pData.
    owned_allocation_t backing{};     // One allocation containing metadata and all block payloads.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryPool_Init(
    memory_pool_t *pPool,
    const allocator_t *pAllocator,
    usize cbPayload,
    usize alignment,
    usize nBlockCount ) noexcept;

CYPHER_COMMON_API void MemoryPool_Shutdown( memory_pool_t *pPool ) noexcept;
CYPHER_COMMON_API void MemoryPool_Reset( memory_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryPool_IsValid( const memory_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *MemoryPool_Allocate( memory_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryPool_Free( memory_pool_t *pPool, void *pBlock ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryPool_Owns(
    const memory_pool_t *pPool,
    const void *pBlock ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryPool_IsAllocated(
    const memory_pool_t *pPool,
    const void *pBlock ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryPool_Capacity( const memory_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryPool_FreeCount( const memory_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryPool_AllocatedCount( const memory_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryPool_HighWaterCount( const memory_pool_t *pPool ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MEMORYPOOL_H
