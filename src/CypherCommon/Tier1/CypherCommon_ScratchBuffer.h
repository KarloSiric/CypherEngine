//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ScratchBuffer.h
//  Purpose: Declares scoped temporary byte storage with local-first allocation.
//  Details: ScratchBuffer uses caller-provided local bytes when they fit and falls
//           back to an explicit allocator otherwise. It owns at most one allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Scratch Buffer Contract

The caller offers cheap local storage first. Acquire uses it only when both size and alignment can
be satisfied; otherwise it obtains one fallback allocation. pData always identifies the selected
storage, while fallback alone records ownership that must be released.
================
*/

#ifndef CYPHER_COMMON_TIER1_SCRATCHBUFFER_H
#define CYPHER_COMMON_TIER1_SCRATCHBUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

struct scratch_buffer_t {
    scratch_buffer_t() noexcept = default;
    ~scratch_buffer_t() noexcept;
    CYPHER_NO_COPY_MOVE( scratch_buffer_t );

    byte *pData{ nullptr };               // Selected local or fallback address.
    usize cbSize{ 0u };                   // Requested usable byte count.
    usize nAlignment{ 0u };               // Alignment guaranteed for pData.
    byte_span_t localStorage{};           // Borrowed candidate storage supplied by the caller.
    owned_allocation_t fallback{};        // Non-empty only when local storage could not be used.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ScratchBuffer_Acquire(
    scratch_buffer_t *pBuffer,
    byte_span_t localStorage,
    const allocator_t *pFallbackAllocator,
    usize cbSize,
    usize nAlignment = CY_ALLOCATOR_DEFAULT_ALIGNMENT ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ScratchBuffer_AcquireZeroed(
    scratch_buffer_t *pBuffer,
    byte_span_t localStorage,
    const allocator_t *pFallbackAllocator,
    usize cbSize,
    usize nAlignment = CY_ALLOCATOR_DEFAULT_ALIGNMENT ) noexcept;

CYPHER_COMMON_API void ScratchBuffer_Release(
    scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ScratchBuffer_IsValid( const scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte *ScratchBuffer_Data( scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const byte *ScratchBuffer_Data( const scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ScratchBuffer_Size( const scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t ScratchBuffer_Span( scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t ScratchBuffer_Block(
    const scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ScratchBuffer_UsesLocalStorage(
    const scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ScratchBuffer_UsesFallbackAllocation(
    const scratch_buffer_t *pBuffer ) noexcept;

CYPHER_COMMON_API void ScratchBuffer_Clear(
    scratch_buffer_t *pBuffer ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SCRATCHBUFFER_H
