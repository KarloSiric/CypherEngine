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

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

// Metadata strings are borrowed. They must remain valid while the allocation is
// tracked and while memory-debug callbacks consume the corresponding event.
struct memory_allocation_record_t {
    void *pMemory;
    usize nByteCount;
    usize nAlignment;
    const char *pszTag;
    const char *pszFile;
    u32 nLine;
};

struct memory_tracker_stats_t {
    usize nCapacity;
    usize nLiveAllocationCount;
    usize nLiveByteCount;
    usize nPeakAllocationCount;
    usize nPeakByteCount;
    u64 nAllocationEvents;
    u64 nFreeEvents;
    u64 nUnknownFreeEvents;
    u64 nDroppedAllocationEvents;
};

constexpr usize CY_MEMORY_TRACKER_CAPACITY = 16384u;

// Records a live allocation without allocating memory inside the tracker.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemoryTrackerRecordAlloc(
    const memory_allocation_record_t &record ) noexcept;

// Removes a live allocation. Returns false for null or unknown pointers.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemoryTrackerRecordFree(
    void *pMemory ) noexcept;

// Copies the live record for a pointer when present.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemoryTrackerFind(
    const void *pMemory,
    memory_allocation_record_t &outRecord ) noexcept;

// Returns a consistent snapshot of tracker counters.
CYPHER_NODISCARD CYPHER_COMMON_API memory_tracker_stats_t Cy_MemoryTrackerGetStats() noexcept;

// Clears all records and counters. Call only when tracked allocators are quiescent.
CYPHER_COMMON_API void Cy_MemoryTrackerReset() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MEMORYTRACKER_H
