//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryTracker.h
//  Purpose: Declares CypherCommon Tier0 MemoryTracker support.
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

#ifndef CYPHER_COMMON_TIER0_MEMORYTRACKER_H
#define CYPHER_COMMON_TIER0_MEMORYTRACKER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Memory Tracker

Allocation tracking declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct memory_allocation_record_t {
    void *pMemory;
    usize cbSize;
    usize alignment;
    const char *pTag;
    const char *pFile;
    i32 line;
};

void MemoryTracker_RecordAlloc( const memory_allocation_record_t &record );
void MemoryTracker_RecordFree( void *pMemory );
usize MemoryTracker_GetLiveAllocationCount();
usize MemoryTracker_GetLiveByteCount();

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MEMORYTRACKER_H
