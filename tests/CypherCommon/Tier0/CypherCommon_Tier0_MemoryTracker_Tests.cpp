//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_MemoryTracker_Tests.cpp
//  Purpose: Tests Tier0 allocation tracker behavior.
//  Details: Verifies allocation replacement, lookup, counters, unknown frees, and
//           reset semantics for the fixed-capacity allocation-free tracker.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryTracker.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "MemoryTracker records finds replaces and frees allocations", "[CypherCommon][Tier0][MemoryTracker]" )
{
    Cy_MemoryTrackerReset();

    u64 value = 0u;
    memory_allocation_record_t record{
        &value,
        64u,
        alignof( u64 ),
        "tracker-test",
        __FILE__,
        static_cast<u32>( __LINE__ )
    };

    REQUIRE( Cy_MemoryTrackerRecordAlloc( record ) );
    memory_tracker_stats_t stats = Cy_MemoryTrackerGetStats();
    REQUIRE( stats.nCapacity == CY_MEMORY_TRACKER_CAPACITY );
    REQUIRE( stats.nLiveAllocationCount == 1u );
    REQUIRE( stats.nLiveByteCount == 64u );
    REQUIRE( stats.nPeakAllocationCount == 1u );
    REQUIRE( stats.nPeakByteCount == 64u );
    REQUIRE( stats.nAllocationEvents == 1u );

    memory_allocation_record_t found{};
    REQUIRE( Cy_MemoryTrackerFind( &value, found ) );
    REQUIRE( found.pMemory == &value );
    REQUIRE( found.nByteCount == 64u );
    REQUIRE( found.nAlignment == alignof( u64 ) );

    record.nByteCount = 96u;
    REQUIRE( Cy_MemoryTrackerRecordAlloc( record ) );
    stats = Cy_MemoryTrackerGetStats();
    REQUIRE( stats.nLiveAllocationCount == 1u );
    REQUIRE( stats.nLiveByteCount == 96u );
    REQUIRE( stats.nPeakByteCount == 96u );
    REQUIRE( stats.nAllocationEvents == 2u );

    REQUIRE( Cy_MemoryTrackerRecordFree( &value ) );
    stats = Cy_MemoryTrackerGetStats();
    REQUIRE( stats.nLiveAllocationCount == 0u );
    REQUIRE( stats.nLiveByteCount == 0u );
    REQUIRE( stats.nFreeEvents == 1u );
    REQUIRE_FALSE( Cy_MemoryTrackerFind( &value, found ) );
}

TEST_CASE( "MemoryTracker rejects invalid records and counts unknown frees", "[CypherCommon][Tier0][MemoryTracker]" )
{
    Cy_MemoryTrackerReset();

    memory_allocation_record_t invalid{};
    REQUIRE_FALSE( Cy_MemoryTrackerRecordAlloc( invalid ) );
    REQUIRE_FALSE( Cy_MemoryTrackerRecordFree( nullptr ) );

    i32 value = 0;
    REQUIRE_FALSE( Cy_MemoryTrackerRecordFree( &value ) );

    memory_tracker_stats_t stats = Cy_MemoryTrackerGetStats();
    REQUIRE( stats.nLiveAllocationCount == 0u );
    REQUIRE( stats.nUnknownFreeEvents == 1u );

    Cy_MemoryTrackerReset();
    stats = Cy_MemoryTrackerGetStats();
    REQUIRE( stats.nUnknownFreeEvents == 0u );
    REQUIRE( stats.nPeakAllocationCount == 0u );
}
