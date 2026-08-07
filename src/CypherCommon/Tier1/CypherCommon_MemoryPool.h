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

    block_memory_t blocks{};
    const allocator_t *pAllocator{ nullptr };
    void *pAllocation{ nullptr };
    usize cbAllocation{ 0u };
    usize allocationAlignment{ 0u };
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
void *MemoryPool_Allocate( memory_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryPool_Free( memory_pool_t *pPool, void *pBlock ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MEMORYPOOL_H
