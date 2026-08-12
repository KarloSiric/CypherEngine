//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_RingBuffer_Tests.cpp
//  Purpose: Tests non-owning fixed-capacity circular buffers.
//  Details: Protects wraparound, overwrite order, zero capacity, borrowed object
//           lifetime, shutdown, and output-alias rejection.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_RingBuffer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct ring_value_t {
    static inline i32 s_liveCount = 0;

    i32 value{ 0 };

    ring_value_t() noexcept
    {
        ++s_liveCount;
    }

    ring_value_t( const ring_value_t &source ) noexcept
        : value( source.value )
    {
        ++s_liveCount;
    }

    ring_value_t &operator=( const ring_value_t &source ) noexcept
    {
        value = source.value;
        return *this;
    }

    ~ring_value_t() noexcept
    {
        --s_liveCount;
    }
};

u32 g_ringBufferAssertCount = 0u;

assert_action_t CaptureRingBufferAssert( const assert_info_t & ) noexcept
{
    ++g_ringBufferAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "RingBuffer preserves logical order across wraparound",
           "[CypherCommon][Tier1][RingBuffer]" )
{
    u32 storage[4]{};
    ring_buffer_t<u32> buffer{};
    REQUIRE( RingBuffer_Init( &buffer, { storage, 4u } ) );
    REQUIRE( RingBuffer_IsValid( &buffer ) );
    REQUIRE( RingBuffer_IsEmpty( &buffer ) );
    REQUIRE_FALSE( RingBuffer_IsFull( &buffer ) );
    REQUIRE( RingBuffer_Capacity( &buffer ) == 4u );

    REQUIRE( RingBuffer_Push( &buffer, 1u ) );
    REQUIRE( RingBuffer_Push( &buffer, 2u ) );
    REQUIRE( RingBuffer_Push( &buffer, 3u ) );
    u32 value = 0u;
    REQUIRE( RingBuffer_Pop( &buffer, &value ) );
    REQUIRE( value == 1u );
    REQUIRE( RingBuffer_Push( &buffer, 4u ) );
    REQUIRE( RingBuffer_Push( &buffer, 5u ) );
    REQUIRE( RingBuffer_IsFull( &buffer ) );
    REQUIRE_FALSE( RingBuffer_Push( &buffer, 6u ) );

    REQUIRE( *RingBuffer_At( &buffer, 0u ) == 2u );
    REQUIRE( *RingBuffer_At( &buffer, 1u ) == 3u );
    REQUIRE( *RingBuffer_At( &buffer, 2u ) == 4u );
    REQUIRE( *RingBuffer_At( &buffer, 3u ) == 5u );
    REQUIRE( *RingBuffer_Front( &buffer ) == 2u );
    REQUIRE( *RingBuffer_Back( &buffer ) == 5u );
}

TEST_CASE( "RingBuffer overwrite reports and replaces the oldest value",
           "[CypherCommon][Tier1][RingBuffer]" )
{
    u32 storage[3]{};
    ring_buffer_t<u32> buffer{};
    REQUIRE( RingBuffer_Init( &buffer, { storage, 3u } ) );
    REQUIRE( RingBuffer_Push( &buffer, 7u ) );
    REQUIRE( RingBuffer_Push( &buffer, 11u ) );
    REQUIRE( RingBuffer_Push( &buffer, 13u ) );

    u32 overwritten = 0u;
    REQUIRE( RingBuffer_PushOverwrite( &buffer, 17u, &overwritten ) );
    REQUIRE( overwritten == 7u );
    REQUIRE( RingBuffer_Count( &buffer ) == 3u );
    REQUIRE( *RingBuffer_At( &buffer, 0u ) == 11u );
    REQUIRE( *RingBuffer_At( &buffer, 1u ) == 13u );
    REQUIRE( *RingBuffer_At( &buffer, 2u ) == 17u );
}

TEST_CASE( "RingBuffer zero capacity is valid but cannot accept values",
           "[CypherCommon][Tier1][RingBuffer]" )
{
    ring_buffer_t<u32> buffer{};
    REQUIRE( RingBuffer_Init( &buffer, {} ) );
    REQUIRE( RingBuffer_IsValid( &buffer ) );
    REQUIRE( RingBuffer_IsEmpty( &buffer ) );
    REQUIRE_FALSE( RingBuffer_IsFull( &buffer ) );
    REQUIRE_FALSE( RingBuffer_Push( &buffer, 1u ) );
    REQUIRE_FALSE( RingBuffer_PushOverwrite( &buffer, 1u ) );
    REQUIRE_FALSE( RingBuffer_Pop( &buffer ) );
}

TEST_CASE( "RingBuffer clear never changes borrowed object lifetimes",
           "[CypherCommon][Tier1][RingBuffer]" )
{
    ring_value_t::s_liveCount = 0;
    {
        ring_value_t storage[3]{};
        REQUIRE( ring_value_t::s_liveCount == 3 );
        ring_buffer_t<ring_value_t> buffer{};
        REQUIRE( RingBuffer_Init( &buffer, { storage, 3u } ) );

        ring_value_t value{};
        value.value = 23;
        REQUIRE( RingBuffer_Push( &buffer, value ) );
        REQUIRE( ring_value_t::s_liveCount == 4 );
        RingBuffer_Clear( &buffer );
        REQUIRE( ring_value_t::s_liveCount == 4 );
        REQUIRE( storage[0].value == 23 );

        RingBuffer_Shutdown( &buffer );
        REQUIRE( buffer.pData == nullptr );
        REQUIRE( ring_value_t::s_liveCount == 4 );
    }
    REQUIRE( ring_value_t::s_liveCount == 0 );
}

TEST_CASE( "RingBuffer rejects extraction into borrowed ring storage",
           "[CypherCommon][Tier1][RingBuffer]" )
{
    u32 storage[2]{};
    ring_buffer_t<u32> buffer{};
    REQUIRE( RingBuffer_Init( &buffer, { storage, 2u } ) );
    REQUIRE( RingBuffer_Push( &buffer, 29u ) );

    g_ringBufferAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureRingBufferAssert );
    REQUIRE_FALSE( RingBuffer_Pop( &buffer, storage ) );
    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( RingBuffer_Count( &buffer ) == 1u );
    REQUIRE(
        g_ringBufferAssertCount ==
        static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
