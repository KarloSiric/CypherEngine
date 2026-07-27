//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PageAllocator.h
//  Purpose: Declares CypherCommon Tier0 PageAllocator support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_PAGEALLOCATOR_H
#define CYPHER_COMMON_TIER0_PAGEALLOCATOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Page Allocator

Page-granular allocator declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct page_allocator_t {
    void *pReservedBase;
    usize nReservedByteCount;
    usize nCommittedByteCount;
    usize nPageSize;
};

// Reserves a linear virtual-memory range. The allocator must be zero initialized.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_PageAllocatorInit(
    page_allocator_t *pAllocator,
    usize nReserveByteCount ) noexcept;

// Releases the reservation and clears the allocator.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_PageAllocatorShutdown(
    page_allocator_t *pAllocator ) noexcept;

// Commits the next page-aligned portion of the linear reservation.
[[nodiscard]] CYPHER_COMMON_API void *Cy_PageAllocatorCommit(
    page_allocator_t *pAllocator,
    usize nByteCount ) noexcept;

// Decommits every committed page while preserving the reservation.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_PageAllocatorReset(
    page_allocator_t *pAllocator ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PAGEALLOCATOR_H
