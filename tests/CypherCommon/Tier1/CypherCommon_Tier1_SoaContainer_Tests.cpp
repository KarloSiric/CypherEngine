//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_SoaContainer_Tests.cpp
//  Purpose: Tests aligned structure-of-arrays storage.
//  Details: Covers layout, growth preservation, zero initialization, swap erase,
//           overflow rejection, move transfer, and explicit ownership shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SoaContainer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "SoaContainer creates aligned columns and preserves data across growth",
           "[CypherCommon][Tier1][SoaContainer]" )
{
    const soa_column_desc_t columns[]{
        { sizeof( u32 ), alignof( u32 ) },
        { sizeof( f64 ), 32u }
    };
    soa_container_t container{};
    REQUIRE( SoaContainer_Init(
        &container,
        { columns, 2u, Allocator_GetSystem(), 2u } ) );
    REQUIRE( SoaContainer_Resize( &container, 2u ) );

    auto *pIds = static_cast<u32 *>( SoaContainer_Column( &container, 0u ) );
    auto *pValues = static_cast<f64 *>( SoaContainer_Column( &container, 1u ) );
    REQUIRE( Cy_AlignIsPointerAligned( pValues, 32u ) );
    pIds[0] = 11u;
    pIds[1] = 22u;
    pValues[0] = 1.5;
    pValues[1] = 2.5;

    REQUIRE( SoaContainer_Resize( &container, 5u ) );
    pIds = static_cast<u32 *>( SoaContainer_Column( &container, 0u ) );
    pValues = static_cast<f64 *>( SoaContainer_Column( &container, 1u ) );
    REQUIRE( pIds[0] == 11u );
    REQUIRE( pIds[1] == 22u );
    REQUIRE( pValues[0] == 1.5 );
    REQUIRE( pValues[1] == 2.5 );
    REQUIRE( pIds[4] == 0u );
    REQUIRE( pValues[4] == 0.0 );
    REQUIRE( SoaContainer_Count( &container ) == 5u );
    REQUIRE( SoaContainer_Capacity( &container ) >= 5u );
}

TEST_CASE( "SoaContainer swap erase keeps columns in row lockstep",
           "[CypherCommon][Tier1][SoaContainer]" )
{
    const soa_column_desc_t columns[]{
        { sizeof( u32 ), alignof( u32 ) },
        { sizeof( u16 ), alignof( u16 ) }
    };
    soa_container_t container{};
    REQUIRE( SoaContainer_Init(
        &container,
        { columns, 2u, Allocator_GetSystem(), 3u } ) );
    REQUIRE( SoaContainer_Resize( &container, 3u ) );
    auto *pIds = static_cast<u32 *>( SoaContainer_Column( &container, 0u ) );
    auto *pKinds = static_cast<u16 *>( SoaContainer_Column( &container, 1u ) );
    pIds[0] = 10u; pIds[1] = 20u; pIds[2] = 30u;
    pKinds[0] = 1u; pKinds[1] = 2u; pKinds[2] = 3u;

    SoaContainer_EraseSwap( &container, 1u );
    REQUIRE( SoaContainer_Count( &container ) == 2u );
    REQUIRE( *static_cast<const u32 *>(
        SoaContainer_Element( &container, 0u, 1u ) ) == 30u );
    REQUIRE( *static_cast<const u16 *>(
        SoaContainer_Element( &container, 1u, 1u ) ) == 3u );
    REQUIRE( SoaContainer_Element( &container, 0u, 2u ) == nullptr );
}

TEST_CASE( "SoaContainer rejects overflow transactionally and supports move",
           "[CypherCommon][Tier1][SoaContainer]" )
{
    const soa_column_desc_t columns[]{ { 16u, 16u } };
    soa_container_t source{};
    REQUIRE( SoaContainer_Init(
        &source,
        { columns, 1u, Allocator_GetSystem(), 2u } ) );
    REQUIRE( SoaContainer_Resize( &source, 1u ) );
    void *pOriginal = SoaContainer_Column( &source, 0u );
    const usize nOriginalCapacity = SoaContainer_Capacity( &source );

    REQUIRE_FALSE( SoaContainer_Reserve( &source, CY_USIZE_MAX ) );
    REQUIRE( SoaContainer_Column( &source, 0u ) == pOriginal );
    REQUIRE( SoaContainer_Capacity( &source ) == nOriginalCapacity );

    soa_container_t destination{};
    SoaContainer_Move( &destination, &source );
    REQUIRE( SoaContainer_IsValid( &destination ) );
    REQUIRE( SoaContainer_Count( &destination ) == 1u );
    REQUIRE( SoaContainer_IsValid( &source ) );
    REQUIRE( SoaContainer_Count( &source ) == 0u );
}
