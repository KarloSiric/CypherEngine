//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PageAllocator.cpp
//  Purpose: Implements CypherCommon Tier0 page allocator.
//  Details: The page allocator reserves a virtual address range and commits it
//           linearly for low-level memory experiments and future arenas.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PageAllocator.h"

#include "CypherCommon_Align.h"
#include "CypherCommon_PlatformMemory.h"

namespace cypher::common
{

namespace
{

bool_t PageAllocator_HasValidReservation(
    const page_allocator_t *pAllocator ) noexcept
{
    if ( pAllocator == nullptr ||
         pAllocator->pReservedBase == nullptr ||
         pAllocator->nReservedByteCount == 0u ||
         pAllocator->nPageSize == 0u ||
         !Cy_AlignIsPowerOfTwo( pAllocator->nPageSize ) ||
         pAllocator->nCommittedByteCount > pAllocator->nReservedByteCount ) {
        return CY_FALSE;
    }

    // The allocator commits one contiguous prefix. Every stored size must remain
    // page aligned or Reset could pass an invalid range to the operating system.
    return Cy_AlignIsAligned(
               reinterpret_cast<uintptr>( pAllocator->pReservedBase ),
               pAllocator->nPageSize ) &&
           Cy_AlignIsAligned(
               pAllocator->nReservedByteCount,
               pAllocator->nPageSize ) &&
           Cy_AlignIsAligned(
               pAllocator->nCommittedByteCount,
               pAllocator->nPageSize );
}

} // namespace

bool_t Cy_PageAllocatorInit(
    page_allocator_t *pAllocator,
    usize nReserveByteCount ) noexcept
{
    if ( pAllocator == nullptr || nReserveByteCount == 0u ) {
        return CY_FALSE;
    }
    // Reinitializing a live allocator would lose the only handle to its virtual
    // reservation. Require caller-owned state to begin fully zeroed.
    if ( pAllocator->pReservedBase != nullptr ||
         pAllocator->nReservedByteCount != 0u ||
         pAllocator->nCommittedByteCount != 0u ||
         pAllocator->nPageSize != 0u ) {
        return CY_FALSE;
    }

    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    usize nAlignedReserveByteCount = 0u;
    // Reserve on the stricter allocation granularity; later commits use page size.
    if ( !Cy_AlignUpChecked(
             nReserveByteCount,
             info.nAllocationGranularity,
             nAlignedReserveByteCount ) ) {
        return CY_FALSE;
    }

    void *pMemory = Cy_PlatformMemoryReserve( nAlignedReserveByteCount );
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }

    pAllocator->pReservedBase = pMemory;
    pAllocator->nReservedByteCount = nAlignedReserveByteCount;
    pAllocator->nCommittedByteCount = 0u;
    pAllocator->nPageSize = info.nPageSize;
    return CY_TRUE;
}

bool_t Cy_PageAllocatorShutdown( page_allocator_t *pAllocator ) noexcept
{
    if ( pAllocator == nullptr ) {
        return CY_FALSE;
    }
    if ( pAllocator->pReservedBase == nullptr ) {
        *pAllocator = {};
        return CY_TRUE;
    }

    if ( !Cy_PlatformMemoryRelease(
             pAllocator->pReservedBase,
             pAllocator->nReservedByteCount ) ) {
        return CY_FALSE;
    }

    *pAllocator = {};
    return CY_TRUE;
}

void *Cy_PageAllocatorCommit(
    page_allocator_t *pAllocator,
    usize nByteCount ) noexcept
{
    if ( !PageAllocator_HasValidReservation( pAllocator ) ||
         nByteCount == 0u ) {
        return nullptr;
    }

    usize nAlignedByteCount = 0u;
    if ( !Cy_AlignUpChecked(
             nByteCount,
             pAllocator->nPageSize,
             nAlignedByteCount ) ) {
        return nullptr;
    }
    if ( nAlignedByteCount >
         pAllocator->nReservedByteCount - pAllocator->nCommittedByteCount ) {
        return nullptr;
    }

    // Only a growing prefix is committed. This keeps returned ranges contiguous
    // and makes Reset a single decommit operation.
    void *pMemory =
        static_cast<byte *>( pAllocator->pReservedBase ) +
        pAllocator->nCommittedByteCount;
    if ( !Cy_PlatformMemoryCommit( pMemory, nAlignedByteCount ) ) {
        return nullptr;
    }

    pAllocator->nCommittedByteCount += nAlignedByteCount;
    return pMemory;
}

bool_t Cy_PageAllocatorReset( page_allocator_t *pAllocator ) noexcept
{
    if ( !PageAllocator_HasValidReservation( pAllocator ) ) {
        return CY_FALSE;
    }
    if ( pAllocator->nCommittedByteCount == 0u ) {
        return CY_TRUE;
    }
    if ( !Cy_PlatformMemoryDecommit(
             pAllocator->pReservedBase,
             pAllocator->nCommittedByteCount ) ) {
        return CY_FALSE;
    }

    // Keep the virtual addresses reserved so a future commit can reuse the same
    // base without address-space fragmentation or pointer relocation.
    pAllocator->nCommittedByteCount = 0u;
    return CY_TRUE;
}

} // namespace cypher::common
