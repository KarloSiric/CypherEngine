//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Vector_Tests.cpp
//  Purpose: Tests allocator-backed growable array ownership.
//  Details: Protects capacity growth, object lifetime, self-aliasing, ordered and
//           unordered erase, allocation rollback, alignment, and move transfer.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Vector.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct tracked_vector_value_t {
    static inline u32 cConstructed = 0u;
    static inline u32 cDestroyed = 0u;
    static inline u32 cMoved = 0u;
    static inline u32 cMoveAssigned = 0u;

    i32 nValue{};

    tracked_vector_value_t() noexcept
    {
        ++cConstructed;
    }

    explicit tracked_vector_value_t( i32 nInitialValue ) noexcept
        : nValue( nInitialValue )
    {
        ++cConstructed;
    }

    tracked_vector_value_t( const tracked_vector_value_t &other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
    }

    tracked_vector_value_t( tracked_vector_value_t &&other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
        ++cMoved;
        other.nValue = -1;
    }

    tracked_vector_value_t &operator=(
        tracked_vector_value_t &&other ) noexcept
    {
        nValue = other.nValue;
        ++cMoveAssigned;
        other.nValue = -1;
        return *this;
    }

    ~tracked_vector_value_t() noexcept
    {
        ++cDestroyed;
    }

    static void ResetCounts() noexcept
    {
        cConstructed = 0u;
        cDestroyed = 0u;
        cMoved = 0u;
        cMoveAssigned = 0u;
    }
};

struct alignas( 128 ) aligned_vector_value_t {
    u64 words[16]{};
};

void *FailVectorAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

u32 g_vectorAssertCount = 0u;

assert_action_t CaptureVectorAssert( const assert_info_t & ) noexcept
{
    ++g_vectorAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Vector grows geometrically and preserves contiguous values",
           "[CypherCommon][Tier1][Vector]" )
{
    vector_t<u32> vector{};
    REQUIRE( Vector_Init( &vector, Allocator_GetSystem() ) );

    for ( u32 nValue = 0u; nValue < 100u; ++nValue ) {
        REQUIRE( Vector_PushBack( &vector, nValue ) );
    }

    REQUIRE( Vector_Count( &vector ) == 100u );
    REQUIRE( Vector_Capacity( &vector ) >= 100u );
    REQUIRE( Vector_Capacity( &vector ) < 200u );
    REQUIRE( Vector_Data( &vector ) == Vector_Span( &vector ).pData );
    for ( usize iIndex = 0u; iIndex < 100u; ++iIndex ) {
        REQUIRE( *Vector_At( &vector, iIndex ) == iIndex );
    }
}

TEST_CASE( "Vector manages non-trivial lifetimes through resize and clear",
           "[CypherCommon][Tier1][Vector]" )
{
    tracked_vector_value_t::ResetCounts();
    {
        vector_t<tracked_vector_value_t> vector{};
        REQUIRE( Vector_Init( &vector, Allocator_GetSystem(), 2u ) );
        REQUIRE( Vector_EmplaceBack( &vector, 7 ) != nullptr );
        REQUIRE( Vector_EmplaceBack( &vector, 11 ) != nullptr );
        REQUIRE( Vector_Resize( &vector, 5u ) );
        REQUIRE( Vector_Count( &vector ) == 5u );
        REQUIRE( Vector_At( &vector, 0u )->nValue == 7 );
        REQUIRE( Vector_At( &vector, 1u )->nValue == 11 );

        REQUIRE( Vector_Resize( &vector, 1u ) );
        REQUIRE( Vector_Front( &vector )->nValue == 7 );
        Vector_Clear( &vector );
        REQUIRE( Vector_IsEmpty( &vector ) );
        REQUIRE( Vector_Capacity( &vector ) >= 5u );
    }

    REQUIRE(
        tracked_vector_value_t::cConstructed ==
        tracked_vector_value_t::cDestroyed );
}

TEST_CASE( "Vector push and append safely rebase internal sources",
           "[CypherCommon][Tier1][Vector]" )
{
    vector_t<u32> vector{};
    REQUIRE( Vector_Init( &vector, Allocator_GetSystem(), 2u ) );
    REQUIRE( Vector_PushBack( &vector, 3u ) );
    REQUIRE( Vector_PushBack( &vector, 5u ) );

    REQUIRE( Vector_PushBack( &vector, *Vector_Front( &vector ) ) );
    REQUIRE( Vector_Count( &vector ) == 3u );
    REQUIRE( *Vector_Back( &vector ) == 3u );

    const span_t<const u32> source{ vector.pData, 2u };
    REQUIRE( Vector_Append( &vector, source ) );
    REQUIRE( Vector_Count( &vector ) == 5u );
    REQUIRE( vector.pData[0] == 3u );
    REQUIRE( vector.pData[1] == 5u );
    REQUIRE( vector.pData[2] == 3u );
    REQUIRE( vector.pData[3] == 3u );
    REQUIRE( vector.pData[4] == 5u );
}

TEST_CASE( "Vector ordered insert and erase preserve sequence",
           "[CypherCommon][Tier1][Vector]" )
{
    vector_t<u32> vector{};
    REQUIRE( Vector_Init( &vector, Allocator_GetSystem() ) );
    for ( u32 nValue : { 2u, 3u, 5u, 7u } ) {
        REQUIRE( Vector_PushBack( &vector, nValue ) );
    }

    REQUIRE( Vector_Insert( &vector, 2u, vector.pData[0] ) );
    REQUIRE( Vector_Count( &vector ) == 5u );
    REQUIRE( vector.pData[0] == 2u );
    REQUIRE( vector.pData[1] == 3u );
    REQUIRE( vector.pData[2] == 2u );
    REQUIRE( vector.pData[3] == 5u );
    REQUIRE( vector.pData[4] == 7u );

    Vector_Erase( &vector, 1u, 2u );
    REQUIRE( Vector_Count( &vector ) == 3u );
    REQUIRE( vector.pData[0] == 2u );
    REQUIRE( vector.pData[1] == 5u );
    REQUIRE( vector.pData[2] == 7u );

    Vector_EraseSwap( &vector, 0u );
    REQUIRE( Vector_Count( &vector ) == 2u );
    REQUIRE( vector.pData[0] == 7u );
    REQUIRE( vector.pData[1] == 5u );
    Vector_PopBack( &vector );
    REQUIRE( *Vector_Back( &vector ) == 7u );
}

TEST_CASE( "Vector failed growth retains allocation and values",
           "[CypherCommon][Tier1][Vector]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    vector_t<u32> vector{};
    REQUIRE( Vector_Init( &vector, &allocator, 2u ) );
    REQUIRE( Vector_PushBack( &vector, 13u ) );
    REQUIRE( Vector_PushBack( &vector, 17u ) );
    u32 *pOriginalData = vector.pData;

    allocator.pfnAllocate = FailVectorAllocation;
    REQUIRE_FALSE( Vector_PushBack( &vector, 19u ) );
    REQUIRE_FALSE( Vector_Reserve( &vector, 64u ) );
    REQUIRE( vector.pData == pOriginalData );
    REQUIRE( Vector_Count( &vector ) == 2u );
    REQUIRE( vector.pData[0] == 13u );
    REQUIRE( vector.pData[1] == 17u );

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
}

TEST_CASE( "Vector shrink-to-fit releases spare storage",
           "[CypherCommon][Tier1][Vector]" )
{
    vector_t<u32> vector{};
    REQUIRE( Vector_Init( &vector, Allocator_GetSystem(), 64u ) );
    REQUIRE( Vector_Resize( &vector, 5u ) );
    REQUIRE( Vector_ShrinkToFit( &vector ) );
    REQUIRE( Vector_Capacity( &vector ) == 5u );
    REQUIRE( Vector_Count( &vector ) == 5u );

    Vector_Clear( &vector );
    REQUIRE( Vector_ShrinkToFit( &vector ) );
    REQUIRE( Vector_Capacity( &vector ) == 0u );
    REQUIRE( Vector_Data( &vector ) == nullptr );
}

TEST_CASE( "Vector preserves over-alignment and const access",
           "[CypherCommon][Tier1][Vector]" )
{
    vector_t<aligned_vector_value_t> vector{};
    REQUIRE( Vector_Init( &vector, Allocator_GetSystem(), 2u ) );
    REQUIRE( Vector_Resize( &vector, 2u ) );
    REQUIRE(
        reinterpret_cast<uintptr>( Vector_Data( &vector ) ) %
        alignof( aligned_vector_value_t ) == 0u );

    const vector_t<aligned_vector_value_t> &constVector = vector;
    STATIC_REQUIRE( is_same_v<
        decltype( Vector_Data( &constVector ) ),
        const aligned_vector_value_t *> );
    STATIC_REQUIRE( is_same_v<
        decltype( Vector_Span( &constVector ) ),
        span_t<const aligned_vector_value_t>> );
}

TEST_CASE( "Vector move transfers storage ownership destructively",
           "[CypherCommon][Tier1][Vector]" )
{
    vector_t<u32> source{};
    vector_t<u32> destination{};
    REQUIRE( Vector_Init( &source, Allocator_GetSystem(), 4u ) );
    REQUIRE( Vector_PushBack( &source, 31u ) );
    u32 *pOriginalData = source.pData;

    Vector_Move( &destination, &source );
    REQUIRE( Vector_IsValid( &source ) );
    REQUIRE( source.pAllocator == nullptr );
    REQUIRE( destination.pData == pOriginalData );
    REQUIRE( *Vector_Front( &destination ) == 31u );
}

TEST_CASE( "Vector invalid operations assert and fail safely",
           "[CypherCommon][Tier1][Vector]" )
{
    g_vectorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureVectorAssert );

    vector_t<u32> vector{};
    REQUIRE_FALSE( Vector_Init<u32>( nullptr, Allocator_GetSystem() ) );
    REQUIRE_FALSE( Vector_Reserve( &vector, 1u ) );
    REQUIRE( Vector_At( &vector, 0u ) == nullptr );
    Vector_PopBack( &vector );
    Vector_Erase( &vector, 1u, 1u );
    Vector_Move( &vector, &vector );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_vectorAssertCount ==
        6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
