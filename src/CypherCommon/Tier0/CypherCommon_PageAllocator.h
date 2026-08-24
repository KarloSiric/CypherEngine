//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PageAllocator.h
//  Purpose: Provides a linear page allocator over one virtual-memory reservation.
//  Details: Commit grows from the reservation base; Reset decommits physical pages
//           without giving up the stable virtual address range.
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
    void *pReservedBase;         // Base of the owned virtual address reservation.
    usize nReservedByteCount;    // Total reserved range in bytes.
    usize nCommittedByteCount;   // Committed prefix length in bytes.
    usize nPageSize;             // Commit granularity captured during Init.
};

// Reserves a linear virtual-memory range. The allocator must be zero initialized.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_PageAllocatorInit(
    page_allocator_t *pAllocator,
    usize nReserveByteCount ) noexcept;

// Releases the reservation and clears the allocator.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_PageAllocatorShutdown(
    page_allocator_t *pAllocator ) noexcept;

// Commits the next page-aligned portion of the linear reservation.
CYPHER_NODISCARD CYPHER_COMMON_API void *Cy_PageAllocatorCommit(
    page_allocator_t *pAllocator,
    usize nByteCount ) noexcept;

// Decommits every committed page while preserving the reservation.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_PageAllocatorReset(
    page_allocator_t *pAllocator ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PAGEALLOCATOR_H
