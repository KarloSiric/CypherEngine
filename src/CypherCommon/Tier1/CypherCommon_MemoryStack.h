//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryStack.h
//  Purpose: Declares non-owning linear stack allocation over fixed memory.
//  Details: MemoryStack allocates by bumping an offset and frees in bulk by restoring
//           markers. It is not thread-safe and does not run object destructors.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_MEMORYSTACK_H
#define CYPHER_COMMON_TIER1_MEMORYSTACK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_FixedMemory.h"

namespace cypher::common
{

using memory_stack_marker_t = usize;

struct memory_stack_t {
    fixed_memory_t memory{};
    usize iOffset{ 0u };
    usize cbHighWater{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStack_Init(
    memory_stack_t *pStack,
    byte_span_t memory ) noexcept;

CYPHER_COMMON_API void MemoryStack_Reset( memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *MemoryStack_Allocate(
    memory_stack_t *pStack,
    usize cbSize,
    usize alignment ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
memory_stack_marker_t MemoryStack_Mark(
    const memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStack_Restore(
    memory_stack_t *pStack,
    memory_stack_marker_t marker ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryStack_Remaining( const memory_stack_t *pStack ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MEMORYSTACK_H
