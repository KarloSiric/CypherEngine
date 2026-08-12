//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/ResourceSystem/CypherCommon_ResourceSystem_ResourceHandle_Tests.cpp
//  Purpose: Tests packed runtime resource-handle contracts.
//  Details: Coverage protects field boundaries, invalid sentinels, output
//           transactions, generation identity, and malformed packed values.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ResourceHandle.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using namespace cypher::common;

static_assert( sizeof( resource_handle_t ) == sizeof( u64 ) );
static_assert( std::is_standard_layout_v<resource_handle_t> );
static_assert( std::is_trivially_copyable_v<resource_handle_t> );

TEST_CASE( "Resource handles round trip every packed-field boundary",
           "[CypherCommon][ResourceSystem][ResourceHandle]" )
{
    constexpr resource_slot_t slots[]{ 0u, 1u, CY_U32_MAX };
    constexpr resource_generation_t generations[]{
        CY_RESOURCE_GENERATION_FIRST,
        CY_RESOURCE_GENERATION_MAX
    };
    constexpr resource_type_slot_t types[]{ 1u, CY_RESOURCE_TYPE_SLOT_MAX };

    for ( resource_slot_t iSlot : slots ) {
        for ( resource_generation_t nGeneration : generations ) {
            for ( resource_type_slot_t iTypeSlot : types ) {
                CAPTURE( iSlot, nGeneration, iTypeSlot );
                resource_handle_t handle{};
                REQUIRE( ResourceHandle_TryMake(
                    iSlot, nGeneration, iTypeSlot, &handle ) );
                REQUIRE( ResourceHandle_IsValid( handle ) );

                const resource_handle_parts_t parts =
                    ResourceHandle_Unpack( handle );
                REQUIRE( parts.iSlot == iSlot );
                REQUIRE( parts.nGeneration == nGeneration );
                REQUIRE( parts.iTypeSlot == iTypeSlot );
                REQUIRE( ResourceHandle_Slot( handle ) == iSlot );
                REQUIRE( ResourceHandle_Generation( handle ) == nGeneration );
                REQUIRE( ResourceHandle_TypeSlot( handle ) == iTypeSlot );
            }
        }
    }
}

TEST_CASE( "Resource handle checked construction rejects reserved fields",
           "[CypherCommon][ResourceSystem][ResourceHandle]" )
{
    resource_handle_t output = ResourceHandle_Make( 7u, 2u, 3u );
    REQUIRE_FALSE( ResourceHandle_TryMake(
        7u, CY_RESOURCE_GENERATION_INVALID, 3u, &output ) );
    REQUIRE_FALSE( ResourceHandle_IsValid( output ) );

    output = ResourceHandle_Make( 7u, 2u, 3u );
    REQUIRE_FALSE( ResourceHandle_TryMake(
        7u, CY_RESOURCE_GENERATION_MAX + 1u, 3u, &output ) );
    REQUIRE_FALSE( ResourceHandle_IsValid( output ) );

    output = ResourceHandle_Make( 7u, 2u, 3u );
    REQUIRE_FALSE( ResourceHandle_TryMake(
        7u, 2u, CY_RESOURCE_TYPE_SLOT_INVALID, &output ) );
    REQUIRE_FALSE( ResourceHandle_IsValid( output ) );

    output = ResourceHandle_Make( 7u, 2u, 3u );
    REQUIRE_FALSE( ResourceHandle_TryMake(
        7u, 2u, CY_RESOURCE_TYPE_SLOT_MAX + 1u, &output ) );
    REQUIRE_FALSE( ResourceHandle_IsValid( output ) );
    REQUIRE_FALSE( ResourceHandle_TryMake( 7u, 2u, 3u, nullptr ) );
}

TEST_CASE( "Resource handle validity rejects malformed packed identities",
           "[CypherCommon][ResourceSystem][ResourceHandle]" )
{
    resource_handle_t zeroGeneration{};
    zeroGeneration.packed = Cy_Handle64Make( 5u, 0u, 2u );
    REQUIRE( Cy_Handle64IsValid( zeroGeneration.packed ) );
    REQUIRE_FALSE( ResourceHandle_IsValid( zeroGeneration ) );

    resource_handle_t zeroType{};
    zeroType.packed = Cy_Handle64Make( 5u, 2u, 0u );
    REQUIRE( Cy_Handle64IsValid( zeroType.packed ) );
    REQUIRE_FALSE( ResourceHandle_IsValid( zeroType ) );
    REQUIRE_FALSE( ResourceHandle_IsValid( CY_RESOURCE_HANDLE_INVALID ) );
}

TEST_CASE( "Resource handle identity includes slot generation and type",
           "[CypherCommon][ResourceSystem][ResourceHandle]" )
{
    const resource_handle_t base = ResourceHandle_Make( 11u, 7u, 3u );
    REQUIRE( ResourceHandle_Equals( base, base ) );
    REQUIRE_FALSE( ResourceHandle_Equals(
        base, ResourceHandle_Make( 12u, 7u, 3u ) ) );
    REQUIRE_FALSE( ResourceHandle_Equals(
        base, ResourceHandle_Make( 11u, 8u, 3u ) ) );
    REQUIRE_FALSE( ResourceHandle_Equals(
        base, ResourceHandle_Make( 11u, 7u, 4u ) ) );
    REQUIRE( ResourceHandle_Equals(
        CY_RESOURCE_HANDLE_INVALID, CY_RESOURCE_HANDLE_INVALID ) );
}
