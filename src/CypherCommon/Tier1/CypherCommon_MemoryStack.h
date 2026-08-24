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

using memory_stack_marker_t = usize; // Saved byte offset used for LIFO bulk release.

struct memory_stack_t {
    fixed_memory_t memory{};  // Caller-owned backing region; never resized or released here.
    usize iOffset{ 0u };      // First unused byte before alignment is applied.
    usize cbHighWater{ 0u };  // Largest live offset observed since initialization.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStack_Init(
    memory_stack_t *pStack,
    byte_span_t memory ) noexcept;

// Rewinds all allocations while retaining lifetime high-water statistics.
CYPHER_COMMON_API void MemoryStack_Reset( memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStack_IsValid( const memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *MemoryStack_Allocate(
    memory_stack_t *pStack,
    usize cbSize,
    usize nAlignment = alignof( std::max_align_t ) ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *MemoryStack_AllocateZeroed(
    memory_stack_t *pStack,
    usize cbSize,
    usize nAlignment = alignof( std::max_align_t ) ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *MemoryStack_AllocateArrayStorage(
    memory_stack_t *pStack,
    usize nCount,
    usize nAlignment = alignof( type_t ) ) noexcept
{
    usize cbSize = 0u;
    const bool_t bValidByteCount = Cy_TryArrayByteCount<type_t>( nCount, cbSize );
    CY_ASSERT_MSG(
        bValidByteCount,
        "MemoryStack array allocation byte count overflowed." );
    if ( !bValidByteCount ) {
        return nullptr;
    }

    return static_cast<type_t *>(
        MemoryStack_Allocate( pStack, cbSize, nAlignment ) );
}

CYPHER_NODISCARD CYPHER_COMMON_API
memory_stack_marker_t MemoryStack_Mark(
    const memory_stack_t *pStack ) noexcept;

// Restoring a marker invalidates every pointer returned after that marker was captured.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStack_Restore(
    memory_stack_t *pStack,
    memory_stack_marker_t marker ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryStack_Capacity( const memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryStack_Used( const memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryStack_Remaining( const memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryStack_HighWater( const memory_stack_t *pStack ) noexcept;

// Restarts high-water accounting at the current live offset.
CYPHER_COMMON_API void MemoryStack_ClearHighWater(
    memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t MemoryStack_AllocatedSpan( memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t MemoryStack_RemainingSpan( memory_stack_t *pStack ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStack_Owns(
    const memory_stack_t *pStack,
    const void *pAddress ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MEMORYSTACK_H
