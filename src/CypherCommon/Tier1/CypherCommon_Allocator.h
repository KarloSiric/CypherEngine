//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Allocator.h
//  Purpose: Declares the Tier1 allocator interface used by owning utilities.
//  Details: The interface keeps allocation ownership explicit without making
//           CypherCommon depend on the higher-level CypherMemory subsystem.
//
//  History:
//  - Created by Karlo Siric on 2026-08-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_ALLOCATOR_H
#define CYPHER_COMMON_TIER1_ALLOCATOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Allocator

Borrowed allocation interface shared by Tier1 owning containers and buffers.

Rules:
- The allocator object and pUserData must outlive every allocation owner using it.
- Allocations are released through the same allocator that created them.
- cbSize == 0 produces no allocation; freeing nullptr is a no-op.
- alignment must be a non-zero power of two.
- pfnReallocate is optional; callers must support allocate-copy-free fallback.
- The interface does not imply thread safety.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

using allocator_allocate_fn_t = void *( * )(
    void *pUserData,
    usize cbSize,
    usize alignment ) noexcept;

using allocator_reallocate_fn_t = void *( * )(
    void *pUserData,
    void *pMemory,
    usize cbOldSize,
    usize cbNewSize,
    usize alignment ) noexcept;

using allocator_free_fn_t = void ( * )(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize alignment ) noexcept;

struct allocator_t {
    allocator_allocate_fn_t pfnAllocate{ nullptr };
    allocator_reallocate_fn_t pfnReallocate{ nullptr };
    allocator_free_fn_t pfnFree{ nullptr };
    void *pUserData{ nullptr };
};

// Transfer record for raw memory whose deallocation responsibility changes owner.
struct owned_allocation_t {
    void *pData{ nullptr };
    usize cbSize{ 0u };
    usize alignment{ 0u };
    const allocator_t *pAllocator{ nullptr };
};

// Returns true when the required allocation and release callbacks are present.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Allocator_IsValid( const allocator_t *pAllocator ) noexcept;

// Allocates cbSize bytes with the requested alignment.
CYPHER_NODISCARD CYPHER_COMMON_API
void *Allocator_Allocate(
    const allocator_t *pAllocator,
    usize cbSize,
    usize alignment ) noexcept;

// Resizes an allocation when supported; returns nullptr when unavailable or failed.
CYPHER_NODISCARD CYPHER_COMMON_API
void *Allocator_Reallocate(
    const allocator_t *pAllocator,
    void *pMemory,
    usize cbOldSize,
    usize cbNewSize,
    usize alignment ) noexcept;

// Releases memory through its originating allocator.
CYPHER_COMMON_API void Allocator_Free(
    const allocator_t *pAllocator,
    void *pMemory,
    usize cbSize,
    usize alignment ) noexcept;

// Frees a transferred allocation and resets the record to its empty state.
CYPHER_COMMON_API void Allocator_FreeOwned(
    owned_allocation_t *pAllocation ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_ALLOCATOR_H
