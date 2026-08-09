//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Queue_Tests.cpp
//  Purpose: Tests allocator-backed first-in-first-out circular queues.
//  Details: Protects wraparound, order-preserving growth, aliases, object lifetime,
//           allocation rollback, shrinking, move, and output-alias rejection.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Queue.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct queue_value_t {
    static inline i32 s_liveCount = 0;

    i32 value{ 0 };

    queue_value_t() noexcept
    {
        ++s_liveCount;
    }

    explicit queue_value_t( i32 nValue ) noexcept
        : value( nValue )
    {
        ++s_liveCount;
    }

    queue_value_t( const queue_value_t &source ) noexcept
        : value( source.value )
    {
        ++s_liveCount;
    }

    queue_value_t( queue_value_t &&source ) noexcept
        : value( source.value )
    {
        source.value = -1;
        ++s_liveCount;
    }

    queue_value_t &operator=( const queue_value_t &source ) noexcept
    {
        value = source.value;
        return *this;
    }

    queue_value_t &operator=( queue_value_t &&source ) noexcept
    {
        value = source.value;
        source.value = -1;
        return *this;
    }

    ~queue_value_t() noexcept
    {
        --s_liveCount;
    }
};

void *FailQueueAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

u32 g_queueAssertCount = 0u;

assert_action_t CaptureQueueAssert( const assert_info_t & ) noexcept
{
    ++g_queueAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Queue preserves FIFO order across wraparound and reserve",
           "[CypherCommon][Tier1][Queue]" )
{
    queue_t<u32> queue{};
    REQUIRE( Queue_Init( &queue, Allocator_GetSystem(), 3u ) );
    REQUIRE( Queue_Push( &queue, 1u ) );
    REQUIRE( Queue_Push( &queue, 2u ) );
    REQUIRE( Queue_Push( &queue, 3u ) );

    u32 value = 0u;
    REQUIRE( Queue_Pop( &queue, &value ) );
    REQUIRE( value == 1u );
    REQUIRE( Queue_Pop( &queue, &value ) );
    REQUIRE( value == 2u );
    REQUIRE( Queue_Push( &queue, 4u ) );
    REQUIRE( Queue_Push( &queue, 5u ) );
    REQUIRE( queue.iHead != 0u );
    REQUIRE( *Queue_At( &queue, 0u ) == 3u );
    REQUIRE( *Queue_At( &queue, 1u ) == 4u );
    REQUIRE( *Queue_At( &queue, 2u ) == 5u );

    REQUIRE( Queue_Reserve( &queue, 8u ) );
    REQUIRE( queue.iHead == 0u );
    REQUIRE( Queue_Capacity( &queue ) == 8u );
    REQUIRE( *Queue_Front( &queue ) == 3u );
    REQUIRE( *Queue_Back( &queue ) == 5u );
}

TEST_CASE( "Queue growth preserves an aliased pushed value",
           "[CypherCommon][Tier1][Queue]" )
{
    queue_t<u32> queue{};
    REQUIRE( Queue_Init( &queue, Allocator_GetSystem(), 2u ) );
    REQUIRE( Queue_Push( &queue, 7u ) );
    REQUIRE( Queue_Push( &queue, 11u ) );
    REQUIRE( Queue_Push( &queue, *Queue_Front( &queue ) ) );
    REQUIRE( Queue_Count( &queue ) == 3u );
    REQUIRE( *Queue_At( &queue, 0u ) == 7u );
    REQUIRE( *Queue_At( &queue, 1u ) == 11u );
    REQUIRE( *Queue_At( &queue, 2u ) == 7u );
}

TEST_CASE( "Queue owns constructed object lifetimes",
           "[CypherCommon][Tier1][Queue]" )
{
    queue_value_t::s_liveCount = 0;
    {
        queue_t<queue_value_t> queue{};
        REQUIRE( Queue_Init( &queue, Allocator_GetSystem() ) );
        REQUIRE( Queue_Emplace( &queue, 17 ) != nullptr );
        REQUIRE( Queue_Emplace( &queue, 19 ) != nullptr );
        REQUIRE( queue_value_t::s_liveCount == 2 );

        queue_value_t output{};
        REQUIRE( queue_value_t::s_liveCount == 3 );
        REQUIRE( Queue_Pop( &queue, &output ) );
        REQUIRE( output.value == 17 );
        REQUIRE( queue_value_t::s_liveCount == 2 );

        Queue_Clear( &queue );
        REQUIRE( queue_value_t::s_liveCount == 1 );
        REQUIRE( Queue_IsEmpty( &queue ) );
    }
    REQUIRE( queue_value_t::s_liveCount == 0 );
}

TEST_CASE( "Queue failed growth leaves order and allocation unchanged",
           "[CypherCommon][Tier1][Queue]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    queue_t<u32> queue{};
    REQUIRE( Queue_Init( &queue, &allocator, 2u ) );
    REQUIRE( Queue_Push( &queue, 23u ) );
    REQUIRE( Queue_Push( &queue, 29u ) );
    u32 *pOriginalData = queue.pData;

    allocator.pfnAllocate = FailQueueAllocation;
    REQUIRE_FALSE( Queue_Push( &queue, 31u ) );
    REQUIRE_FALSE( Queue_Reserve( &queue, 64u ) );
    REQUIRE( queue.pData == pOriginalData );
    REQUIRE( Queue_Count( &queue ) == 2u );
    REQUIRE( *Queue_At( &queue, 0u ) == 23u );
    REQUIRE( *Queue_At( &queue, 1u ) == 29u );

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
}

TEST_CASE( "Queue shrink and move preserve logical ownership",
           "[CypherCommon][Tier1][Queue]" )
{
    queue_t<u32> source{};
    queue_t<u32> destination{};
    REQUIRE( Queue_Init( &source, Allocator_GetSystem(), 32u ) );
    REQUIRE( Queue_Push( &source, 37u ) );
    REQUIRE( Queue_Push( &source, 41u ) );
    REQUIRE( Queue_ShrinkToFit( &source ) );
    REQUIRE( Queue_Capacity( &source ) == 2u );

    Queue_Move( &destination, &source );
    REQUIRE( source.pData == nullptr );
    REQUIRE( source.pAllocator == nullptr );
    REQUIRE( Queue_Count( &destination ) == 2u );
    REQUIRE( *Queue_Front( &destination ) == 37u );

    Queue_Clear( &destination );
    REQUIRE( Queue_ShrinkToFit( &destination ) );
    REQUIRE( destination.pData == nullptr );
    REQUIRE( destination.pAllocator == Allocator_GetSystem() );
}

TEST_CASE( "Queue rejects extraction into its own storage",
           "[CypherCommon][Tier1][Queue]" )
{
    queue_t<u32> queue{};
    REQUIRE( Queue_Init( &queue, Allocator_GetSystem() ) );
    REQUIRE( Queue_Push( &queue, 43u ) );

    g_queueAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureQueueAssert );
    REQUIRE_FALSE( Queue_Pop( &queue, Queue_Front( &queue ) ) );
    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( Queue_Count( &queue ) == 1u );
    REQUIRE(
        g_queueAssertCount ==
        static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
