//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Array_Tests.cpp
//  Purpose: Tests allocator-backed exact-size array ownership.
//  Details: Protects construction, destruction, resize rollback, alignment,
//           allocator retention, bounds checks, const access, and move transfer.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Array.h"
#include "CypherCommon_Assert.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct tracked_value_t {
    static inline u32 cConstructed = 0u;
    static inline u32 cDestroyed = 0u;
    static inline u32 cMoved = 0u;

    i32 nValue{};

    tracked_value_t() noexcept
    {
        ++cConstructed;
    }

    tracked_value_t( const tracked_value_t &other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
    }

    tracked_value_t( tracked_value_t &&other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
        ++cMoved;
        other.nValue = -1;
    }

    ~tracked_value_t() noexcept
    {
        ++cDestroyed;
    }

    static void ResetCounts() noexcept
    {
        cConstructed = 0u;
        cDestroyed = 0u;
        cMoved = 0u;
    }
};

struct alignas( 64 ) aligned_value_t {
    u64 words[8]{};
};

void *FailAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

u32 g_arrayAssertCount = 0u;

assert_action_t CaptureArrayAssert( const assert_info_t & ) noexcept
{
    ++g_arrayAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Array owns each constructed element exactly once",
           "[CypherCommon][Tier1][Array]" )
{
    tracked_value_t::ResetCounts();
    {
        array_t<tracked_value_t> array{};
        REQUIRE( Array_Init( &array, Allocator_GetSystem(), 3u ) );
        REQUIRE( Array_IsValid( &array ) );
        REQUIRE_FALSE( Array_IsEmpty( &array ) );
        REQUIRE( Array_Count( &array ) == 3u );
        REQUIRE( tracked_value_t::cConstructed == 3u );

        Array_At( &array, 0u )->nValue = 11;
        Array_At( &array, 1u )->nValue = 13;
        Array_At( &array, 2u )->nValue = 17;
        REQUIRE( Array_Front( &array )->nValue == 11 );
        REQUIRE( Array_Back( &array )->nValue == 17 );
    }

    REQUIRE( tracked_value_t::cConstructed == tracked_value_t::cDestroyed );
}

TEST_CASE( "Array resize preserves values while growing and shrinking",
           "[CypherCommon][Tier1][Array]" )
{
    tracked_value_t::ResetCounts();
    {
        array_t<tracked_value_t> array{};
        REQUIRE( Array_Init( &array, Allocator_GetSystem(), 3u ) );
        for ( usize iIndex = 0u; iIndex < 3u; ++iIndex ) {
            Array_At( &array, iIndex )->nValue = static_cast<i32>( iIndex + 1u );
        }

        REQUIRE( Array_Resize( &array, 5u ) );
        REQUIRE( Array_Count( &array ) == 5u );
        REQUIRE( Array_At( &array, 0u )->nValue == 1 );
        REQUIRE( Array_At( &array, 1u )->nValue == 2 );
        REQUIRE( Array_At( &array, 2u )->nValue == 3 );
        REQUIRE( Array_At( &array, 3u )->nValue == 0 );
        REQUIRE( Array_At( &array, 4u )->nValue == 0 );

        REQUIRE( Array_Resize( &array, 2u ) );
        REQUIRE( Array_Count( &array ) == 2u );
        REQUIRE( Array_At( &array, 0u )->nValue == 1 );
        REQUIRE( Array_At( &array, 1u )->nValue == 2 );
        REQUIRE( tracked_value_t::cMoved == 5u );
    }

    REQUIRE( tracked_value_t::cConstructed == tracked_value_t::cDestroyed );
}

TEST_CASE( "Array allocation failure leaves the original value untouched",
           "[CypherCommon][Tier1][Array]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    array_t<u32> array{};
    REQUIRE( Array_Init( &array, &allocator, 3u ) );
    array.pData[0] = 2u;
    array.pData[1] = 3u;
    array.pData[2] = 5u;
    u32 *pOriginalData = array.pData;

    allocator.pfnAllocate = FailAllocation;
    REQUIRE_FALSE( Array_Resize( &array, 8u ) );
    REQUIRE( array.pData == pOriginalData );
    REQUIRE( Array_Count( &array ) == 3u );
    REQUIRE( array.pData[0] == 2u );
    REQUIRE( array.pData[1] == 3u );
    REQUIRE( array.pData[2] == 5u );

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
}

TEST_CASE( "Array clear retains its allocator binding for reuse",
           "[CypherCommon][Tier1][Array]" )
{
    const allocator_t *pAllocator = Allocator_GetSystem();
    array_t<u32> array{};
    REQUIRE( Array_Init( &array, pAllocator, 4u ) );

    Array_Clear( &array );
    REQUIRE( Array_IsValid( &array ) );
    REQUIRE( Array_IsEmpty( &array ) );
    REQUIRE( array.pAllocator == pAllocator );
    REQUIRE( Array_Data( &array ) == nullptr );

    REQUIRE( Array_Resize( &array, 2u ) );
    REQUIRE( Array_Count( &array ) == 2u );
}

TEST_CASE( "Array preserves over-alignment and const-qualified access",
           "[CypherCommon][Tier1][Array]" )
{
    array_t<aligned_value_t> array{};
    REQUIRE( Array_Init( &array, Allocator_GetSystem(), 2u ) );
    REQUIRE(
        reinterpret_cast<uintptr>( Array_Data( &array ) ) %
        alignof( aligned_value_t ) == 0u );

    const array_t<aligned_value_t> &constArray = array;
    STATIC_REQUIRE( is_same_v<
        decltype( Array_Data( &constArray ) ),
        const aligned_value_t *> );
    STATIC_REQUIRE( is_same_v<
        decltype( Array_Span( &constArray ) ),
        span_t<const aligned_value_t>> );
    REQUIRE( Array_Span( &constArray ).nCount == 2u );
}

TEST_CASE( "Array move transfers allocation ownership destructively",
           "[CypherCommon][Tier1][Array]" )
{
    array_t<u32> source{};
    array_t<u32> destination{};
    REQUIRE( Array_Init( &source, Allocator_GetSystem(), 2u ) );
    source.pData[0] = 23u;
    source.pData[1] = 29u;
    u32 *pOriginalData = source.pData;

    Array_Move( &destination, &source );
    REQUIRE( Array_IsValid( &source ) );
    REQUIRE( Array_IsEmpty( &source ) );
    REQUIRE( source.pAllocator == nullptr );
    REQUIRE( destination.pData == pOriginalData );
    REQUIRE( Array_Back( &destination ) != nullptr );
    REQUIRE( *Array_Back( &destination ) == 29u );
}

TEST_CASE( "Array invalid operations assert and fail safely",
           "[CypherCommon][Tier1][Array]" )
{
    g_arrayAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureArrayAssert );

    array_t<u32> array{};
    REQUIRE_FALSE( Array_Init<u32>( nullptr, Allocator_GetSystem(), 1u ) );
    REQUIRE_FALSE( Array_Resize( &array, 1u ) );
    REQUIRE( Array_Data( static_cast<array_t<u32> *>( nullptr ) ) == nullptr );
    REQUIRE( Array_At( &array, 0u ) == nullptr );
    Array_Move( &array, &array );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_arrayAssertCount ==
        5u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "Array shutdown restores the canonical empty state",
           "[CypherCommon][Tier1][Array][Lifecycle]" )
{
    array_t<u32> array{};
    REQUIRE( Array_Init( &array, Allocator_GetSystem(), 4u ) );
    Array_Shutdown( &array );
    REQUIRE( Array_IsValid( &array ) );
    REQUIRE( Array_IsEmpty( &array ) );
    REQUIRE( array.pData == nullptr );
    REQUIRE( array.pAllocator == nullptr );
    Array_Shutdown( &array );
}
