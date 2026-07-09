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

bool_t PageAllocator_Init( page_allocator_t *pAllocator, usize cbReserve )
{
    if ( pAllocator == nullptr || cbReserve == 0u ) {
        return CY_FALSE;
    }

    *pAllocator = {};

    const platform_memory_info_t info = PlatformMemory_GetInfo();
    const usize cbAlignedReserve = AlignUp( cbReserve, info.page_size );
    void *pMemory = PlatformMemory_Reserve( cbAlignedReserve );
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }

    pAllocator->pReservedBase = pMemory;
    pAllocator->cbReserved = cbAlignedReserve;
    pAllocator->cbCommitted = 0u;
    pAllocator->page_size = info.page_size;
    return CY_TRUE;
}

void PageAllocator_Shutdown( page_allocator_t *pAllocator )
{
    if ( pAllocator == nullptr || pAllocator->pReservedBase == nullptr ) {
        return;
    }

    PlatformMemory_Release( pAllocator->pReservedBase, pAllocator->cbReserved );
    *pAllocator = {};
}

void *PageAllocator_Commit( page_allocator_t *pAllocator, usize cbSize )
{
    if ( pAllocator == nullptr || pAllocator->pReservedBase == nullptr || cbSize == 0u ) {
        return nullptr;
    }

    const usize cbAlignedSize = AlignUp( cbSize, pAllocator->page_size );
    if ( cbAlignedSize > pAllocator->cbReserved - pAllocator->cbCommitted ) {
        return nullptr;
    }

    void *pMemory = static_cast<byte *>( pAllocator->pReservedBase ) + pAllocator->cbCommitted;
    if ( !PlatformMemory_Commit( pMemory, cbAlignedSize ) ) {
        return nullptr;
    }

    pAllocator->cbCommitted += cbAlignedSize;
    return pMemory;
}

void PageAllocator_Reset( page_allocator_t *pAllocator )
{
    if ( pAllocator == nullptr || pAllocator->pReservedBase == nullptr || pAllocator->cbCommitted == 0u ) {
        return;
    }

    PlatformMemory_Decommit( pAllocator->pReservedBase, pAllocator->cbCommitted );
    pAllocator->cbCommitted = 0u;
}

} // namespace cypher::common
