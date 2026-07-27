//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_PlatformMemory_Tests.cpp
//  Purpose: Tests Tier0 operating-system virtual-memory behavior.
//  Details: Exercises page geometry, validation, reserve/commit/decommit/recommit,
//           byte access, and release through the portable API.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Align.h"
#include "CypherCommon_PlatformMemory.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "PlatformMemory reports usable page geometry", "[CypherCommon][Tier0][PlatformMemory]" )
{
    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();

    REQUIRE( info.nPageSize != 0u );
    REQUIRE( Cy_AlignIsPowerOfTwo( info.nPageSize ) );
    REQUIRE( info.nAllocationGranularity >= info.nPageSize );
    REQUIRE( Cy_AlignIsPowerOfTwo( info.nAllocationGranularity ) );
    REQUIRE( ( info.nAllocationGranularity % info.nPageSize ) == 0u );
}

TEST_CASE( "PlatformMemory rejects invalid and overflowing requests", "[CypherCommon][Tier0][PlatformMemory]" )
{
    REQUIRE( Cy_PlatformMemoryReserve( 0u ) == nullptr );
    REQUIRE( Cy_PlatformMemoryReserve( CY_USIZE_MAX ) == nullptr );
    REQUIRE_FALSE( Cy_PlatformMemoryCommit( nullptr, 4096u ) );
    REQUIRE_FALSE( Cy_PlatformMemoryDecommit( nullptr, 4096u ) );
    REQUIRE_FALSE( Cy_PlatformMemoryRelease( nullptr, 4096u ) );
}

TEST_CASE( "PlatformMemory reserves commits decommits and releases pages", "[CypherCommon][Tier0][PlatformMemory]" )
{
    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    const usize nReserveByteCount = info.nAllocationGranularity * 2u;
    void *pReservation = Cy_PlatformMemoryReserve( nReserveByteCount );
    REQUIRE( pReservation != nullptr );
    REQUIRE( Cy_AlignIsAligned(
        reinterpret_cast<uintptr>( pReservation ),
        info.nAllocationGranularity ) );

    REQUIRE_FALSE( Cy_PlatformMemoryCommit(
        static_cast<u8 *>( pReservation ) + 1u,
        info.nPageSize ) );
    REQUIRE( Cy_PlatformMemoryCommit( pReservation, info.nPageSize ) );

    auto *pBytes = static_cast<u8 *>( pReservation );
    pBytes[0u] = 0x12u;
    pBytes[info.nPageSize - 1u] = 0x34u;
    REQUIRE( pBytes[0u] == 0x12u );
    REQUIRE( pBytes[info.nPageSize - 1u] == 0x34u );

    REQUIRE( Cy_PlatformMemoryDecommit( pReservation, info.nPageSize ) );
    REQUIRE( Cy_PlatformMemoryCommit( pReservation, info.nPageSize ) );
    pBytes[0u] = 0x56u;
    REQUIRE( pBytes[0u] == 0x56u );

    REQUIRE( Cy_PlatformMemoryRelease( pReservation, nReserveByteCount ) );
}
