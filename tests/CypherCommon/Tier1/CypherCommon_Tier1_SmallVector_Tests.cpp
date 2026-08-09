//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_SmallVector_Tests.cpp
//  Purpose: Tests growable arrays with inline storage.
//  Details: Proves allocation-free inline operation, spill and shrink behavior,
//           object lifetimes, self-aliasing, alignment, and both move paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_SmallVector.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct counting_allocator_state_t {
    usize cAllocations{};
    usize cFrees{};
    bool_t bFailAllocations{ CY_FALSE };
};

void *CountingAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<counting_allocator_state_t *>( pUserData );
    if ( pState->bFailAllocations ) {
        return nullptr;
    }
    ++pState->cAllocations;
    return Allocator_Allocate( Allocator_GetSystem(), cbSize, nAlignment );
}

void CountingFree(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<counting_allocator_state_t *>( pUserData );
    ++pState->cFrees;
    Allocator_Free( Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

allocator_t MakeCountingAllocator(
    counting_allocator_state_t *pState ) noexcept
{
    return { CountingAllocate, nullptr, CountingFree, pState };
}

struct tracked_small_value_t {
    static inline u32 cConstructed = 0u;
    static inline u32 cDestroyed = 0u;

    i32 nValue{};

    tracked_small_value_t() noexcept
    {
        ++cConstructed;
    }

    explicit tracked_small_value_t( i32 nInitialValue ) noexcept
        : nValue( nInitialValue )
    {
        ++cConstructed;
    }

    tracked_small_value_t( const tracked_small_value_t &other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
    }

    tracked_small_value_t( tracked_small_value_t &&other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
        other.nValue = -1;
    }

    tracked_small_value_t &operator=( tracked_small_value_t &&other ) noexcept
    {
        nValue = other.nValue;
        other.nValue = -1;
        return *this;
    }

    ~tracked_small_value_t() noexcept
    {
        ++cDestroyed;
    }

    static void ResetCounts() noexcept
    {
        cConstructed = 0u;
        cDestroyed = 0u;
    }
};

struct alignas( 64 ) aligned_small_value_t {
    u64 words[8]{};
};

u32 g_smallVectorAssertCount = 0u;

assert_action_t CaptureSmallVectorAssert( const assert_info_t & ) noexcept
{
    ++g_smallVectorAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "SmallVector performs no allocation within inline capacity",
           "[CypherCommon][Tier1][SmallVector]" )
{
    counting_allocator_state_t state{};
    allocator_t allocator = MakeCountingAllocator( &state );
    {
        small_vector_t<u32, 4u> vector{};
        REQUIRE( SmallVector_Init( &vector, &allocator ) );
        REQUIRE( SmallVector_UsesInlineStorage( &vector ) );
        REQUIRE( SmallVector_Capacity( &vector ) == 4u );

        for ( u32 nValue = 0u; nValue < 4u; ++nValue ) {
            REQUIRE( SmallVector_PushBack( &vector, nValue ) );
        }
        REQUIRE( state.cAllocations == 0u );

        REQUIRE( SmallVector_PushBack( &vector, 4u ) );
        REQUIRE_FALSE( SmallVector_UsesInlineStorage( &vector ) );
        REQUIRE( state.cAllocations == 1u );
    }
    REQUIRE( state.cFrees == 1u );
}

TEST_CASE( "SmallVector spill and shrink preserve non-trivial lifetimes",
           "[CypherCommon][Tier1][SmallVector]" )
{
    tracked_small_value_t::ResetCounts();
    {
        small_vector_t<tracked_small_value_t, 2u> vector{};
        REQUIRE( SmallVector_Init( &vector, Allocator_GetSystem() ) );
        REQUIRE( SmallVector_EmplaceBack( &vector, 7 ) != nullptr );
        REQUIRE( SmallVector_EmplaceBack( &vector, 11 ) != nullptr );
        REQUIRE( SmallVector_EmplaceBack( &vector, 13 ) != nullptr );
        REQUIRE_FALSE( SmallVector_UsesInlineStorage( &vector ) );

        SmallVector_PopBack( &vector );
        REQUIRE( SmallVector_ShrinkToFit( &vector ) );
        REQUIRE( SmallVector_UsesInlineStorage( &vector ) );
        REQUIRE( SmallVector_Count( &vector ) == 2u );
        REQUIRE( SmallVector_At( &vector, 0u )->nValue == 7 );
        REQUIRE( SmallVector_At( &vector, 1u )->nValue == 11 );
    }
    REQUIRE(
        tracked_small_value_t::cConstructed ==
        tracked_small_value_t::cDestroyed );
}

TEST_CASE( "SmallVector rebases self-push and self-append across spill",
           "[CypherCommon][Tier1][SmallVector]" )
{
    small_vector_t<u32, 2u> vector{};
    REQUIRE( SmallVector_Init( &vector, Allocator_GetSystem() ) );
    REQUIRE( SmallVector_PushBack( &vector, 2u ) );
    REQUIRE( SmallVector_PushBack( &vector, 3u ) );
    REQUIRE( SmallVector_PushBack( &vector, *SmallVector_Front( &vector ) ) );

    const span_t<const u32> source{ vector.pData, 2u };
    REQUIRE( SmallVector_Append( &vector, source ) );
    REQUIRE( SmallVector_Count( &vector ) == 5u );
    REQUIRE( vector.pData[0] == 2u );
    REQUIRE( vector.pData[1] == 3u );
    REQUIRE( vector.pData[2] == 2u );
    REQUIRE( vector.pData[3] == 2u );
    REQUIRE( vector.pData[4] == 3u );
}

TEST_CASE( "SmallVector supports ordered and swap erasure",
           "[CypherCommon][Tier1][SmallVector]" )
{
    small_vector_t<u32, 8u> vector{};
    REQUIRE( SmallVector_Init( &vector, Allocator_GetSystem() ) );
    for ( u32 nValue : { 2u, 3u, 5u, 7u } ) {
        REQUIRE( SmallVector_PushBack( &vector, nValue ) );
    }
    REQUIRE( SmallVector_Insert( &vector, 1u, vector.pData[3] ) );
    REQUIRE( vector.pData[0] == 2u );
    REQUIRE( vector.pData[1] == 7u );
    REQUIRE( vector.pData[2] == 3u );

    SmallVector_Erase( &vector, 1u, 2u );
    REQUIRE( SmallVector_Count( &vector ) == 3u );
    REQUIRE( vector.pData[0] == 2u );
    REQUIRE( vector.pData[1] == 5u );
    REQUIRE( vector.pData[2] == 7u );

    SmallVector_EraseSwap( &vector, 0u );
    REQUIRE( SmallVector_Count( &vector ) == 2u );
    REQUIRE( vector.pData[0] == 7u );
    REQUIRE( vector.pData[1] == 5u );
}

TEST_CASE( "SmallVector failed spill retains inline data",
           "[CypherCommon][Tier1][SmallVector]" )
{
    counting_allocator_state_t state{};
    allocator_t allocator = MakeCountingAllocator( &state );
    small_vector_t<u32, 2u> vector{};
    REQUIRE( SmallVector_Init( &vector, &allocator ) );
    REQUIRE( SmallVector_PushBack( &vector, 17u ) );
    REQUIRE( SmallVector_PushBack( &vector, 19u ) );
    u32 *pInlineData = vector.pData;

    state.bFailAllocations = CY_TRUE;
    REQUIRE_FALSE( SmallVector_PushBack( &vector, 23u ) );
    REQUIRE( vector.pData == pInlineData );
    REQUIRE( SmallVector_UsesInlineStorage( &vector ) );
    REQUIRE( SmallVector_Count( &vector ) == 2u );
    REQUIRE( vector.pData[0] == 17u );
    REQUIRE( vector.pData[1] == 19u );
}

TEST_CASE( "SmallVector zero inline capacity behaves as a heap vector",
           "[CypherCommon][Tier1][SmallVector]" )
{
    small_vector_t<u32, 0u> vector{};
    REQUIRE( SmallVector_Init( &vector, Allocator_GetSystem() ) );
    REQUIRE_FALSE( SmallVector_UsesInlineStorage( &vector ) );
    REQUIRE( SmallVector_Data( &vector ) == nullptr );
    REQUIRE( SmallVector_PushBack( &vector, 41u ) );
    REQUIRE_FALSE( SmallVector_UsesInlineStorage( &vector ) );
    REQUIRE( *SmallVector_Front( &vector ) == 41u );
}

TEST_CASE( "SmallVector move relocates inline data and steals heap data",
           "[CypherCommon][Tier1][SmallVector]" )
{
    small_vector_t<u32, 2u> inlineSource{};
    small_vector_t<u32, 2u> inlineDestination{};
    REQUIRE( SmallVector_Init( &inlineSource, Allocator_GetSystem() ) );
    REQUIRE( SmallVector_PushBack( &inlineSource, 43u ) );
    u32 *pSourceInlineData = inlineSource.pData;

    SmallVector_Move( &inlineDestination, &inlineSource );
    REQUIRE( inlineDestination.pData != pSourceInlineData );
    REQUIRE( SmallVector_UsesInlineStorage( &inlineDestination ) );
    REQUIRE( *SmallVector_Front( &inlineDestination ) == 43u );
    REQUIRE( inlineSource.pAllocator == nullptr );

    small_vector_t<u32, 2u> heapSource{};
    small_vector_t<u32, 2u> heapDestination{};
    REQUIRE( SmallVector_Init( &heapSource, Allocator_GetSystem() ) );
    REQUIRE( SmallVector_Resize( &heapSource, 3u ) );
    u32 *pSourceHeapData = heapSource.pData;

    SmallVector_Move( &heapDestination, &heapSource );
    REQUIRE( heapDestination.pData == pSourceHeapData );
    REQUIRE_FALSE( SmallVector_UsesInlineStorage( &heapDestination ) );
    REQUIRE( heapSource.pAllocator == nullptr );
}

TEST_CASE( "SmallVector preserves inline and heap alignment",
           "[CypherCommon][Tier1][SmallVector]" )
{
    small_vector_t<aligned_small_value_t, 2u> vector{};
    REQUIRE( SmallVector_Init( &vector, Allocator_GetSystem() ) );
    REQUIRE(
        reinterpret_cast<uintptr>( SmallVector_Data( &vector ) ) %
        alignof( aligned_small_value_t ) == 0u );
    REQUIRE( SmallVector_Resize( &vector, 3u ) );
    REQUIRE(
        reinterpret_cast<uintptr>( SmallVector_Data( &vector ) ) %
        alignof( aligned_small_value_t ) == 0u );

    const auto &constVector = vector;
    STATIC_REQUIRE( is_same_v<
        decltype( SmallVector_Data( &constVector ) ),
        const aligned_small_value_t *> );
}

TEST_CASE( "SmallVector invalid operations assert and fail safely",
           "[CypherCommon][Tier1][SmallVector]" )
{
    g_smallVectorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureSmallVectorAssert );

    small_vector_t<u32, 4u> vector{};
    REQUIRE_FALSE( SmallVector_Init<u32, 4u>( nullptr, Allocator_GetSystem() ) );
    REQUIRE_FALSE( SmallVector_Reserve( &vector, 8u ) );
    REQUIRE( SmallVector_At( &vector, 0u ) == nullptr );
    SmallVector_PopBack( &vector );
    SmallVector_Erase( &vector, 1u, 1u );
    SmallVector_Move( &vector, &vector );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_smallVectorAssertCount ==
        6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
