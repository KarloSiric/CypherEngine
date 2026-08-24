//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryTracker.h
//  Purpose: Tracks a bounded set of live allocations without recursive allocation.
//  Details: Metadata strings are borrowed until the allocation is removed; callers
//           must keep tag and source strings alive for that interval.
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
    void *pMemory;       // Allocation base used as the unique lookup key.
    usize nByteCount;    // Live allocation size in bytes.
    usize nAlignment;    // Requested alignment in bytes; zero means unspecified.
    const char *pszTag;  // Borrowed allocation category; may be null.
    const char *pszFile; // Borrowed source path; may be null.
    u32 nLine;           // One-based source line; zero means unavailable.
};

struct memory_tracker_stats_t {
    usize nCapacity;              // Maximum simultaneously tracked allocations.
    usize nLiveAllocationCount;   // Records currently occupied.
    usize nLiveByteCount;         // Sum of bytes in live records.
    usize nPeakAllocationCount;   // Highest live record count since Reset.
    usize nPeakByteCount;         // Highest live byte total since Reset.
    u64 nAllocationEvents;        // Successful allocation records since Reset.
    u64 nFreeEvents;              // Successful record removals since Reset.
    u64 nUnknownFreeEvents;       // Free requests for untracked pointers.
    u64 nDroppedAllocationEvents; // Allocations omitted because the table was full.
};

constexpr usize CY_MEMORY_TRACKER_CAPACITY = 16384u; // Fixed storage avoids tracker recursion.

// Records a live allocation without allocating memory inside the tracker.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemoryTrackerRecordAlloc(
    const memory_allocation_record_t &record ) noexcept;

// Removes a live allocation. Returns false for null or unknown pointers.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemoryTrackerRecordFree(
    void *pMemory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemoryTrackerFind(
    const void *pMemory,
    memory_allocation_record_t &outRecord ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API memory_tracker_stats_t Cy_MemoryTrackerGetStats() noexcept;

// Clears all records and counters. Call only when tracked allocators are quiescent.
CYPHER_COMMON_API void Cy_MemoryTrackerReset() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MEMORYTRACKER_H
