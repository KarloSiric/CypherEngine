//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryTracker.cpp
//  Purpose: Implements CypherCommon Tier0 allocation tracking.
//  Details: This tracker records live pointer sizes for diagnostics and tests.
//           High-volume allocator telemetry can replace internals later.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryTracker.h"

#include "CypherCommon_MemoryDebug.h"

#include <mutex>
#include <unordered_map>

namespace cypher::common
{
namespace
{

std::mutex g_memoryTrackerMutex;
std::unordered_map<void *, memory_allocation_record_t> g_memoryTrackerRecords;
usize g_memoryTrackerLiveBytes = 0u;

} // namespace

void MemoryTracker_RecordAlloc( const memory_allocation_record_t &record )
{
    if ( record.pMemory == nullptr ) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );

        auto existing = g_memoryTrackerRecords.find( record.pMemory );
        if ( existing != g_memoryTrackerRecords.end() ) {
            g_memoryTrackerLiveBytes -= existing->second.cbSize;
            existing->second = record;
        } else {
            g_memoryTrackerRecords.emplace( record.pMemory, record );
        }

        g_memoryTrackerLiveBytes += record.cbSize;
    }

    MemoryDebug_ReportEvent( memory_debug_event_t::Alloc, record.pMemory, record.cbSize, record.pTag );
}

void MemoryTracker_RecordFree( void *pMemory )
{
    if ( pMemory == nullptr ) {
        return;
    }

    usize cbSize = 0u;
    const char *pTag = nullptr;

    {
        std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );

        auto it = g_memoryTrackerRecords.find( pMemory );
        if ( it == g_memoryTrackerRecords.end() ) {
            return;
        }

        cbSize = it->second.cbSize;
        pTag = it->second.pTag;
        g_memoryTrackerLiveBytes -= cbSize;
        g_memoryTrackerRecords.erase( it );
    }

    MemoryDebug_ReportEvent( memory_debug_event_t::Free, pMemory, cbSize, pTag );
}

usize MemoryTracker_GetLiveAllocationCount()
{
    std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );
    return g_memoryTrackerRecords.size();
}

usize MemoryTracker_GetLiveByteCount()
{
    std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );
    return g_memoryTrackerLiveBytes;
}

} // namespace cypher::common
