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

namespace cypher::common
{
namespace
{

enum class memory_tracker_slot_state_t : u8 {
    Empty = 0u,
    Occupied,
    Tombstone
};

struct memory_tracker_slot_t {
    memory_allocation_record_t record;
    memory_tracker_slot_state_t state;
};

static_assert( ( CY_MEMORY_TRACKER_CAPACITY & ( CY_MEMORY_TRACKER_CAPACITY - 1u ) ) == 0u,
               "Memory tracker capacity must be a power of two." );

std::mutex g_memoryTrackerMutex;
memory_tracker_slot_t g_memoryTrackerSlots[CY_MEMORY_TRACKER_CAPACITY] = {};
memory_tracker_stats_t g_memoryTrackerStats{
    CY_MEMORY_TRACKER_CAPACITY,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u
};

usize MemoryTracker_HashPointer( const void *pMemory ) noexcept
{
    u64 nValue = static_cast<u64>( reinterpret_cast<uintptr>( pMemory ) );
    nValue >>= 4u;
    nValue ^= nValue >> 33u;
    nValue *= 0xFF51AFD7ED558CCDull;
    nValue ^= nValue >> 33u;
    return static_cast<usize>( nValue ) & ( CY_MEMORY_TRACKER_CAPACITY - 1u );
}

memory_tracker_slot_t *MemoryTracker_FindSlot( const void *pMemory ) noexcept
{
    const usize nStart = MemoryTracker_HashPointer( pMemory );
    for ( usize nProbe = 0u; nProbe < CY_MEMORY_TRACKER_CAPACITY; ++nProbe ) {
        memory_tracker_slot_t &slot =
            g_memoryTrackerSlots[( nStart + nProbe ) & ( CY_MEMORY_TRACKER_CAPACITY - 1u )];

        if ( slot.state == memory_tracker_slot_state_t::Empty ) {
            return nullptr;
        }
        if ( slot.state == memory_tracker_slot_state_t::Occupied &&
             slot.record.pMemory == pMemory ) {
            return &slot;
        }
    }
    return nullptr;
}

memory_tracker_slot_t *MemoryTracker_FindInsertSlot(
    const void *pMemory,
    bool_t &outAlreadyPresent ) noexcept
{
    const usize nStart = MemoryTracker_HashPointer( pMemory );
    memory_tracker_slot_t *pFirstTombstone = nullptr;

    for ( usize nProbe = 0u; nProbe < CY_MEMORY_TRACKER_CAPACITY; ++nProbe ) {
        memory_tracker_slot_t &slot =
            g_memoryTrackerSlots[( nStart + nProbe ) & ( CY_MEMORY_TRACKER_CAPACITY - 1u )];

        if ( slot.state == memory_tracker_slot_state_t::Occupied ) {
            if ( slot.record.pMemory == pMemory ) {
                outAlreadyPresent = CY_TRUE;
                return &slot;
            }
            continue;
        }

        if ( slot.state == memory_tracker_slot_state_t::Tombstone ) {
            if ( pFirstTombstone == nullptr ) {
                pFirstTombstone = &slot;
            }
            continue;
        }

        outAlreadyPresent = CY_FALSE;
        return pFirstTombstone != nullptr ? pFirstTombstone : &slot;
    }

    outAlreadyPresent = CY_FALSE;
    return pFirstTombstone;
}

memory_debug_record_t MemoryTracker_MakeDebugRecord(
    memory_debug_event_t eventType,
    const memory_allocation_record_t &record ) noexcept
{
    return memory_debug_record_t{
        eventType,
        record.pMemory,
        record.nByteCount,
        record.nAlignment,
        record.pszTag,
        record.pszFile,
        record.nLine
    };
}

} // namespace

bool_t Cy_MemoryTrackerRecordAlloc( const memory_allocation_record_t &record ) noexcept
{
    if ( record.pMemory == nullptr ) {
        return CY_FALSE;
    }

    memory_debug_event_t eventType = memory_debug_event_t::Alloc;
    {
        std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );

        bool_t isAlreadyPresent = CY_FALSE;
        memory_tracker_slot_t *pSlot =
            MemoryTracker_FindInsertSlot( record.pMemory, isAlreadyPresent );
        if ( pSlot == nullptr ) {
            ++g_memoryTrackerStats.nDroppedAllocationEvents;
            return CY_FALSE;
        }

        const usize nPreviousByteCount =
            isAlreadyPresent ? pSlot->record.nByteCount : 0u;
        const usize nBaseByteCount =
            g_memoryTrackerStats.nLiveByteCount - nPreviousByteCount;
        if ( record.nByteCount > CY_USIZE_MAX - nBaseByteCount ) {
            ++g_memoryTrackerStats.nDroppedAllocationEvents;
            return CY_FALSE;
        }

        if ( isAlreadyPresent ) {
            eventType = memory_debug_event_t::Realloc;
        } else {
            ++g_memoryTrackerStats.nLiveAllocationCount;
        }

        pSlot->record = record;
        pSlot->state = memory_tracker_slot_state_t::Occupied;
        g_memoryTrackerStats.nLiveByteCount = nBaseByteCount + record.nByteCount;
        ++g_memoryTrackerStats.nAllocationEvents;

        if ( g_memoryTrackerStats.nLiveAllocationCount >
             g_memoryTrackerStats.nPeakAllocationCount ) {
            g_memoryTrackerStats.nPeakAllocationCount =
                g_memoryTrackerStats.nLiveAllocationCount;
        }
        if ( g_memoryTrackerStats.nLiveByteCount >
             g_memoryTrackerStats.nPeakByteCount ) {
            g_memoryTrackerStats.nPeakByteCount =
                g_memoryTrackerStats.nLiveByteCount;
        }
    }

    Cy_MemoryDebugReportEvent( MemoryTracker_MakeDebugRecord( eventType, record ) );
    return CY_TRUE;
}

bool_t Cy_MemoryTrackerRecordFree( void *pMemory ) noexcept
{
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }

    memory_allocation_record_t record{};

    {
        std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );

        memory_tracker_slot_t *pSlot = MemoryTracker_FindSlot( pMemory );
        if ( pSlot == nullptr ) {
            ++g_memoryTrackerStats.nUnknownFreeEvents;
            return CY_FALSE;
        }

        record = pSlot->record;
        g_memoryTrackerStats.nLiveByteCount -= record.nByteCount;
        --g_memoryTrackerStats.nLiveAllocationCount;
        ++g_memoryTrackerStats.nFreeEvents;
        pSlot->record = {};
        pSlot->state = memory_tracker_slot_state_t::Tombstone;
    }

    Cy_MemoryDebugReportEvent(
        MemoryTracker_MakeDebugRecord( memory_debug_event_t::Free, record ) );
    return CY_TRUE;
}

bool_t Cy_MemoryTrackerFind(
    const void *pMemory,
    memory_allocation_record_t &outRecord ) noexcept
{
    outRecord = {};
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );
    const memory_tracker_slot_t *pSlot = MemoryTracker_FindSlot( pMemory );
    if ( pSlot == nullptr ) {
        return CY_FALSE;
    }

    outRecord = pSlot->record;
    return CY_TRUE;
}

memory_tracker_stats_t Cy_MemoryTrackerGetStats() noexcept
{
    std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );
    return g_memoryTrackerStats;
}

void Cy_MemoryTrackerReset() noexcept
{
    std::lock_guard<std::mutex> lock( g_memoryTrackerMutex );
    for ( memory_tracker_slot_t &slot : g_memoryTrackerSlots ) {
        slot = {};
    }
    g_memoryTrackerStats = {
        CY_MEMORY_TRACKER_CAPACITY,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u
    };
}

} // namespace cypher::common
