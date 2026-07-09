//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_TLS_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 TLS slot behavior.
//  Details: These tests validate per-thread value isolation and generational
//           slot invalidation used by scratch memory, profiling, and logging.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TLS.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace cypher::common;

TEST_CASE( "TLS creates valid slots and destroys them", "[CypherCommon][Tier0][TLS]" )
{
    const tls_slot_t slot = Cy_TLSCreateSlot();

    REQUIRE( slot != CY_TLS_INVALID_SLOT );
    REQUIRE( Cy_TLSIsValidSlot( slot ) );

    Cy_TLSDestroySlot( slot );
    REQUIRE_FALSE( Cy_TLSIsValidSlot( slot ) );
    REQUIRE( Cy_TLSGetValue( slot ) == nullptr );
}

TEST_CASE( "TLS stores and clears current thread values", "[CypherCommon][Tier0][TLS]" )
{
    const tls_slot_t slot = Cy_TLSCreateSlot();
    i32 nValue = 42;

    REQUIRE( Cy_TLSSetValue( slot, &nValue ) );
    REQUIRE( Cy_TLSGetValue( slot ) == &nValue );

    Cy_TLSClearValue( slot );
    REQUIRE( Cy_TLSGetValue( slot ) == nullptr );

    REQUIRE( Cy_TLSSetValue( slot, &nValue ) );
    REQUIRE( Cy_TLSSetValue( slot, nullptr ) );
    REQUIRE( Cy_TLSGetValue( slot ) == nullptr );

    Cy_TLSDestroySlot( slot );
}

TEST_CASE( "TLS values are isolated between threads", "[CypherCommon][Tier0][TLS]" )
{
    const tls_slot_t slot = Cy_TLSCreateSlot();
    i32 nMainValue = 11;
    i32 nWorkerValue = 22;
    void *pWorkerRead = nullptr;

    REQUIRE( Cy_TLSSetValue( slot, &nMainValue ) );

    std::thread worker( [&]() {
        pWorkerRead = Cy_TLSGetValue( slot );
        Cy_TLSSetValue( slot, &nWorkerValue );
        REQUIRE( Cy_TLSGetValue( slot ) == &nWorkerValue );
    } );
    worker.join();

    REQUIRE( pWorkerRead == nullptr );
    REQUIRE( Cy_TLSGetValue( slot ) == &nMainValue );

    Cy_TLSDestroySlot( slot );
}

TEST_CASE( "TLS destroyed slot handles do not remain valid after reuse", "[CypherCommon][Tier0][TLS]" )
{
    const tls_slot_t oldSlot = Cy_TLSCreateSlot();
    i32 nValue = 99;

    REQUIRE( Cy_TLSSetValue( oldSlot, &nValue ) );
    REQUIRE( Cy_TLSGetValue( oldSlot ) == &nValue );

    Cy_TLSDestroySlot( oldSlot );

    const tls_slot_t newSlot = Cy_TLSCreateSlot();

    REQUIRE( newSlot != CY_TLS_INVALID_SLOT );
    REQUIRE( newSlot != oldSlot );
    REQUIRE_FALSE( Cy_TLSIsValidSlot( oldSlot ) );
    REQUIRE( Cy_TLSGetValue( oldSlot ) == nullptr );
    REQUIRE( Cy_TLSGetValue( newSlot ) == nullptr );

    Cy_TLSDestroySlot( newSlot );
}

TEST_CASE( "TLS stale handles cannot clear a reused slot value", "[CypherCommon][Tier0][TLS]" )
{
    const tls_slot_t oldSlot = Cy_TLSCreateSlot();
    REQUIRE( oldSlot != CY_TLS_INVALID_SLOT );

    Cy_TLSDestroySlot( oldSlot );

    const tls_slot_t newSlot = Cy_TLSCreateSlot();
    i32 nValue = 123;

    REQUIRE( newSlot != CY_TLS_INVALID_SLOT );
    REQUIRE( newSlot != oldSlot );
    REQUIRE( Cy_TLSSetValue( newSlot, &nValue ) );

    Cy_TLSClearValue( oldSlot );

    REQUIRE( Cy_TLSGetValue( newSlot ) == &nValue );

    Cy_TLSDestroySlot( newSlot );
}
