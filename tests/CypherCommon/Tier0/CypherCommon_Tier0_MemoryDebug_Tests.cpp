//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_MemoryDebug_Tests.cpp
//  Purpose: Tests Tier0 memory-debug callback behavior.
//  Details: Verifies callback state, context forwarding, and reentrancy protection
//           without depending on the allocation tracker.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryDebug.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct memory_debug_test_context_t {
    u32 nCallCount;
    memory_debug_record_t lastRecord;
};

void MemoryDebugTestCallback(
    const memory_debug_record_t &record,
    void *pContext ) noexcept
{
    auto *pState = static_cast<memory_debug_test_context_t *>( pContext );
    ++pState->nCallCount;
    pState->lastRecord = record;

    // A callback may allocate; recursively generated events must not recurse forever.
    Cy_MemoryDebugReportEvent( record );
}

} // namespace

TEST_CASE( "MemoryDebug stores callback context and forwards records", "[CypherCommon][Tier0][MemoryDebug]" )
{
    memory_debug_test_context_t context{};
    Cy_MemoryDebugSetCallback( MemoryDebugTestCallback, &context );

    void *pStoredContext = nullptr;
    REQUIRE( Cy_MemoryDebugGetCallback( &pStoredContext ) == MemoryDebugTestCallback );
    REQUIRE( pStoredContext == &context );

    i32 value = 0;
    const memory_debug_record_t record{
        memory_debug_event_t::Alloc,
        &value,
        sizeof( value ),
        alignof( i32 ),
        "memory-debug-test",
        __FILE__,
        static_cast<u32>( __LINE__ )
    };
    Cy_MemoryDebugReportEvent( record );

    REQUIRE( context.nCallCount == 1u );
    REQUIRE( context.lastRecord.eventType == memory_debug_event_t::Alloc );
    REQUIRE( context.lastRecord.pMemory == &value );
    REQUIRE( context.lastRecord.nByteCount == sizeof( value ) );
    REQUIRE( context.lastRecord.nAlignment == alignof( i32 ) );

    Cy_MemoryDebugSetCallback( nullptr );
    REQUIRE( Cy_MemoryDebugGetCallback( &pStoredContext ) == nullptr );
    REQUIRE( pStoredContext == nullptr );
}
