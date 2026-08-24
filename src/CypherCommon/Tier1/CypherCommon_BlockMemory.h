//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BlockMemory.h
//  Purpose: Declares fixed-size free-list allocation over caller memory.
//  Details: BlockMemory owns no backing storage. Every returned block has one fixed
//           size/alignment and must be returned to its originating instance. Payload
//           size and alignment must permit an internal free-list pointer per block.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_BLOCKMEMORY_H
#define CYPHER_COMMON_TIER1_BLOCKMEMORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_FixedMemory.h"

namespace cypher::common
{

/*
================
Fixed Block Layout

The occupancy bitmap and aligned payload slots share one caller-owned region. Free blocks store
their next pointer in the payload itself, so cbPayload must be large enough for that link. Reset
rebuilds the list and invalidates every allocation without inspecting user payload bytes.
================
*/

struct block_memory_t {
    fixed_memory_t memory{};        // Entire borrowed metadata-and-payload region.
    void *pFreeHead{ nullptr };     // First available payload; free payloads form a singly linked list.
    byte *pOccupancyBits{ nullptr };// One allocation bit per block, stored at the front of memory.
    usize cbOccupancyBits{ 0u };    // Bytes reserved for pOccupancyBits.
    usize cbBlockStride{ 0u };      // Aligned distance between consecutive payload addresses.
    usize cbPayload{ 0u };          // Usable bytes returned for each allocation.
    usize nAlignment{ 0u };         // Required alignment of every payload address.
    usize nBlockCount{ 0u };        // Total number of payload slots in memory.
    usize nFreeCount{ 0u };         // Slots currently reachable through pFreeHead.
    usize nHighWaterCount{ 0u };    // Maximum simultaneously allocated slots since reset.
};

CYPHER_NODISCARD CYPHER_COMMON_API
usize BlockMemory_RequiredBytes(
    usize cbPayload,
    usize alignment,
    usize nBlockCount ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BlockMemory_Init(
    block_memory_t *pMemory,
    byte_span_t storage,
    usize cbPayload,
    usize nAlignment,
    usize nBlockCount ) noexcept;

// Returns every block to the free list without releasing caller-owned storage.
CYPHER_COMMON_API void BlockMemory_Reset( block_memory_t *pMemory ) noexcept;

// Detaches the caller-owned storage and restores the default empty state.
CYPHER_COMMON_API void BlockMemory_Shutdown( block_memory_t *pMemory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BlockMemory_IsValid( const block_memory_t *pMemory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *BlockMemory_Allocate( block_memory_t *pMemory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BlockMemory_Free(
    block_memory_t *pMemory,
    void *pBlock ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BlockMemory_Owns(
    const block_memory_t *pMemory,
    const void *pBlock ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BlockMemory_IsAllocated(
    const block_memory_t *pMemory,
    const void *pBlock ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BlockMemory_Capacity( const block_memory_t *pMemory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BlockMemory_FreeCount( const block_memory_t *pMemory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BlockMemory_AllocatedCount( const block_memory_t *pMemory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BlockMemory_HighWaterCount( const block_memory_t *pMemory ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BLOCKMEMORY_H
