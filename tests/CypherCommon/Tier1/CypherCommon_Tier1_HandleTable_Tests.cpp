//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_HandleTable_Tests.cpp
//  Purpose: Tests typed generational handle storage.
//  Details: Covers stale-handle rejection, slot reuse, growth, object lifetime,
//           alignment, generation rollover, const access, and allocation rollback.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_HandleTable.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct tracked_handle_value_t {
    static inline u32 cConstructed = 0u;
    static inline u32 cDestroyed = 0u;

    i32 nValue;

    explicit tracked_handle_value_t( i32 nInitialValue ) noexcept
        : nValue( nInitialValue )
    {
        ++cConstructed;
    }

    tracked_handle_value_t( const tracked_handle_value_t &other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
    }

    tracked_handle_value_t( tracked_handle_value_t &&other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
        other.nValue = -1;
    }

    ~tracked_handle_value_t() noexcept
    {
        ++cDestroyed;
    }

    static void ResetCounts() noexcept
    {
        cConstructed = 0u;
        cDestroyed = 0u;
    }
};

struct alignas( 128 ) aligned_handle_value_t {
    u64 words[16]{};

    explicit aligned_handle_value_t( u64 nValue ) noexcept
    {
        words[0] = nValue;
    }
};

void *FailHandleTableAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

u32 g_handleTableAssertCount = 0u;

assert_action_t CaptureHandleTableAssert( const assert_info_t & ) noexcept
{
    ++g_handleTableAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "HandleTable rejects stale handles after deterministic slot reuse",
           "[CypherCommon][Tier1][HandleTable]" )
{
    handle_table_t<tracked_handle_value_t> table{};
    REQUIRE( HandleTable_Init( &table, Allocator_GetSystem(), 2u ) );

    const handle32_t first = HandleTable_Emplace( &table, 11 );
    const handle32_t second = HandleTable_Emplace( &table, 17 );
    REQUIRE( Cy_Handle32IsValid( first ) );
    REQUIRE( Cy_Handle32IsValid( second ) );
    REQUIRE( HandleTable_Get( &table, first )->nValue == 11 );
    REQUIRE( HandleTable_Get( &table, second )->nValue == 17 );

    REQUIRE( HandleTable_Remove( &table, first ) );
    REQUIRE( HandleTable_Get( &table, first ) == nullptr );
    REQUIRE_FALSE( HandleTable_Remove( &table, first ) );

    const handle32_t replacement = HandleTable_Emplace( &table, 23 );
    REQUIRE( Cy_Handle32Index( replacement ) == Cy_Handle32Index( first ) );
    REQUIRE(
        Cy_Handle32Generation( replacement ) !=
        Cy_Handle32Generation( first ) );
    REQUIRE( HandleTable_Get( &table, replacement )->nValue == 23 );
    REQUIRE( HandleTable_Count( &table ) == 2u );
}

TEST_CASE( "HandleTable growth preserves handles and over-aligned values",
           "[CypherCommon][Tier1][HandleTable]" )
{
    handle_table_t<aligned_handle_value_t> table{};
    REQUIRE( HandleTable_Init( &table, Allocator_GetSystem(), 1u ) );

    handle32_t handles[32]{};
    for ( usize iValue = 0u; iValue < 32u; ++iValue ) {
        handles[iValue] = HandleTable_Emplace(
            &table,
            static_cast<u64>( iValue + 100u ) );
        REQUIRE( Cy_Handle32IsValid( handles[iValue] ) );
    }

    REQUIRE( HandleTable_Capacity( &table ) >= 32u );
    const handle_table_t<aligned_handle_value_t> &constTable = table;
    for ( usize iValue = 0u; iValue < 32u; ++iValue ) {
        const aligned_handle_value_t *pValue = HandleTable_Get(
            &constTable,
            handles[iValue] );
        REQUIRE( pValue != nullptr );
        REQUIRE( pValue->words[0] == iValue + 100u );
        REQUIRE(
            reinterpret_cast<uintptr>( pValue ) %
            alignof( aligned_handle_value_t ) == 0u );
    }
}

TEST_CASE( "HandleTable clear destroys values and invalidates every live handle",
           "[CypherCommon][Tier1][HandleTable]" )
{
    tracked_handle_value_t::ResetCounts();
    handle32_t handles[3]{};
    {
        handle_table_t<tracked_handle_value_t> table{};
        REQUIRE( HandleTable_Init( &table, Allocator_GetSystem(), 3u ) );
        for ( usize iValue = 0u; iValue < 3u; ++iValue ) {
            handles[iValue] = HandleTable_Emplace(
                &table,
                static_cast<i32>( iValue ) );
        }

        HandleTable_Clear( &table );
        REQUIRE( HandleTable_IsEmpty( &table ) );
        REQUIRE( HandleTable_Capacity( &table ) == 3u );
        for ( handle32_t handle : handles ) {
            REQUIRE_FALSE( HandleTable_Contains( &table, handle ) );
        }
    }
    REQUIRE(
        tracked_handle_value_t::cConstructed ==
        tracked_handle_value_t::cDestroyed );
}

TEST_CASE( "HandleTable failed growth preserves storage and live identities",
           "[CypherCommon][Tier1][HandleTable]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    handle_table_t<u32> table{};
    REQUIRE( HandleTable_Init( &table, &allocator, 2u ) );
    const handle32_t first = HandleTable_Emplace( &table, 31u );
    const handle32_t second = HandleTable_Emplace( &table, 37u );
    auto *pOriginalSlots = table.pSlots;

    allocator.pfnAllocate = FailHandleTableAllocation;
    REQUIRE_FALSE( Cy_Handle32IsValid( HandleTable_Emplace( &table, 41u ) ) );
    REQUIRE_FALSE( HandleTable_Reserve( &table, 64u ) );
    REQUIRE( table.pSlots == pOriginalSlots );
    REQUIRE( HandleTable_Count( &table ) == 2u );
    REQUIRE( *HandleTable_Get( &table, first ) == 31u );
    REQUIRE( *HandleTable_Get( &table, second ) == 37u );

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
}

TEST_CASE( "HandleTable generation rollover skips the invalid zero generation",
           "[CypherCommon][Tier1][HandleTable]" )
{
    handle_table_t<u32> table{};
    REQUIRE( HandleTable_Init( &table, Allocator_GetSystem(), 1u ) );
    handle32_t handle = HandleTable_Emplace( &table, 5u );
    REQUIRE( Cy_Handle32IsValid( handle ) );

    table.pSlots[0].nGeneration = CY_HANDLE32_GENERATION_MAX;
    handle = Cy_Handle32Make( 0u, CY_HANDLE32_GENERATION_MAX );
    REQUIRE( HandleTable_Remove( &table, handle ) );
    const handle32_t replacement = HandleTable_Emplace( &table, 7u );
    REQUIRE( Cy_Handle32Generation( replacement ) == 1u );
    REQUIRE( *HandleTable_Get( &table, replacement ) == 7u );
}

TEST_CASE( "HandleTable rejects capacities outside its compact handle contract",
           "[CypherCommon][Tier1][HandleTable]" )
{
    handle_table_t<u32> table{};
    REQUIRE( HandleTable_Init( &table, Allocator_GetSystem() ) );

    g_handleTableAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureHandleTableAssert );
    REQUIRE_FALSE( HandleTable_Reserve(
        &table,
        CY_HANDLE_TABLE_MAX_CAPACITY + 1u ) );
    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE(
        g_handleTableAssertCount ==
        static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "HandleTable traversal visits live slots and honors early stop",
           "[CypherCommon][Tier1][HandleTable]" )
{
    handle_table_t<u32> table{};
    REQUIRE( HandleTable_Init( &table, Allocator_GetSystem(), 4u ) );
    const handle32_t first = HandleTable_Emplace( &table, 3u );
    const handle32_t removed = HandleTable_Emplace( &table, 5u );
    const handle32_t third = HandleTable_Emplace( &table, 7u );
    REQUIRE( HandleTable_Remove( &table, removed ) );

    u32 nSum = 0u;
    const usize nVisited = HandleTable_ForEach(
        &table,
        [&nSum]( handle32_t, u32 &value ) noexcept -> bool_t {
            nSum += value;
            return CY_TRUE;
        } );
    REQUIRE( nVisited == 2u );
    REQUIRE( nSum == 10u );

    const handle_table_t<u32> &constTable = table;
    handle32_t visitedHandle{};
    const usize nStopped = HandleTable_ForEach(
        &constTable,
        [&visitedHandle]( handle32_t handle, const u32 & ) noexcept -> bool_t {
            visitedHandle = handle;
            return CY_FALSE;
        } );
    REQUIRE( nStopped == 1u );
    REQUIRE( visitedHandle.value == first.value );
    REQUIRE( HandleTable_Contains( &table, third ) );
}
