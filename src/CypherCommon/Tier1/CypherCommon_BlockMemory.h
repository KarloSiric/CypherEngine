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

struct block_memory_t {
    fixed_memory_t memory{};
    void *pFreeHead{ nullptr };
    usize cbBlockStride{ 0u };
    usize cbPayload{ 0u };
    usize alignment{ 0u };
    usize nBlockCount{ 0u };
    usize nFreeCount{ 0u };
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
    usize alignment ) noexcept;

CYPHER_COMMON_API void BlockMemory_Reset( block_memory_t *pMemory ) noexcept;

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

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BLOCKMEMORY_H
