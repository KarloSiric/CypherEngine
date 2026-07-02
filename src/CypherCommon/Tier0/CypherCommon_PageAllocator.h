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
#pragma once

/*
================
CypherCommon Page Allocator

Page-granular allocator declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct page_allocator_t {
    void *pReservedBase;
    usize cbReserved;
    usize cbCommitted;
    usize page_size;
};

bool_t PageAllocator_Init( page_allocator_t *pAllocator, usize cbReserve );
void PageAllocator_Shutdown( page_allocator_t *pAllocator );
void *PageAllocator_Commit( page_allocator_t *pAllocator, usize cbSize );
void PageAllocator_Reset( page_allocator_t *pAllocator );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PAGEALLOCATOR_H
