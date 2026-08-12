//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Stack_Tests.cpp
//  Purpose: Tests the vector-backed last-in-first-out adapter.
//  Details: Protects LIFO order, extraction, object lifetime, capacity reuse,
//           destructive move, and output-alias rejection.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Stack.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct stack_value_t {
    static inline i32 s_liveCount = 0;

    i32 value{ 0 };

    stack_value_t() noexcept
    {
        ++s_liveCount;
    }

    explicit stack_value_t( i32 nValue ) noexcept
        : value( nValue )
    {
        ++s_liveCount;
    }

    stack_value_t( const stack_value_t &source ) noexcept
        : value( source.value )
    {
        ++s_liveCount;
    }

    stack_value_t( stack_value_t &&source ) noexcept
        : value( source.value )
    {
        source.value = -1;
        ++s_liveCount;
    }

    stack_value_t &operator=( const stack_value_t &source ) noexcept
    {
        value = source.value;
        return *this;
    }

    stack_value_t &operator=( stack_value_t &&source ) noexcept
    {
        value = source.value;
        source.value = -1;
        return *this;
    }

    ~stack_value_t() noexcept
    {
        --s_liveCount;
    }
};

u32 g_stackAssertCount = 0u;

assert_action_t CaptureStackAssert( const assert_info_t & ) noexcept
{
    ++g_stackAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Stack pushes and pops in last-in-first-out order",
           "[CypherCommon][Tier1][Stack]" )
{
    cy_stack_t<u32> stack{};
    REQUIRE( Stack_Init( &stack, Allocator_GetSystem(), 2u ) );
    REQUIRE( Stack_IsValid( &stack ) );
    REQUIRE( Stack_IsEmpty( &stack ) );
    REQUIRE( Stack_Push( &stack, 2u ) );
    REQUIRE( Stack_Push( &stack, 3u ) );
    REQUIRE( Stack_Push( &stack, 5u ) );
    REQUIRE( Stack_Count( &stack ) == 3u );
    REQUIRE( *Stack_Top( &stack ) == 5u );

    u32 value = 0u;
    REQUIRE( Stack_Pop( &stack, &value ) );
    REQUIRE( value == 5u );
    REQUIRE( Stack_Pop( &stack, &value ) );
    REQUIRE( value == 3u );
    REQUIRE( Stack_Pop( &stack, &value ) );
    REQUIRE( value == 2u );
    REQUIRE_FALSE( Stack_Pop( &stack, &value ) );
    REQUIRE( Stack_Top( &stack ) == nullptr );
}

TEST_CASE( "Stack emplace clear and shutdown own object lifetimes",
           "[CypherCommon][Tier1][Stack]" )
{
    stack_value_t::s_liveCount = 0;
    {
        cy_stack_t<stack_value_t> stack{};
        REQUIRE( Stack_Init( &stack, Allocator_GetSystem() ) );
        REQUIRE( Stack_Emplace( &stack, 11 ) != nullptr );
        REQUIRE( Stack_Emplace( &stack, 13 ) != nullptr );
        REQUIRE( stack_value_t::s_liveCount == 2 );

        stack_value_t output{};
        REQUIRE( stack_value_t::s_liveCount == 3 );
        REQUIRE( Stack_Pop( &stack, &output ) );
        REQUIRE( output.value == 13 );
        REQUIRE( stack_value_t::s_liveCount == 2 );

        Stack_Clear( &stack );
        REQUIRE( stack_value_t::s_liveCount == 1 );
        REQUIRE( Stack_Capacity( &stack ) > 0u );
    }
    REQUIRE( stack_value_t::s_liveCount == 0 );
}

TEST_CASE( "Stack reserve and move preserve vector ownership",
           "[CypherCommon][Tier1][Stack]" )
{
    cy_stack_t<u32> source{};
    cy_stack_t<u32> destination{};
    REQUIRE( Stack_Init( &source, Allocator_GetSystem() ) );
    REQUIRE( Stack_Reserve( &source, 64u ) );
    REQUIRE( Stack_Push( &source, 7u ) );

    Stack_Move( &destination, &source );
    REQUIRE( source.storage.pData == nullptr );
    REQUIRE( source.storage.pAllocator == nullptr );
    REQUIRE( Stack_Capacity( &destination ) == 64u );
    REQUIRE( *Stack_Top( &destination ) == 7u );
}

TEST_CASE( "Stack rejects extraction into its own storage",
           "[CypherCommon][Tier1][Stack]" )
{
    cy_stack_t<u32> stack{};
    REQUIRE( Stack_Init( &stack, Allocator_GetSystem() ) );
    REQUIRE( Stack_Push( &stack, 17u ) );

    g_stackAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureStackAssert );
    REQUIRE_FALSE( Stack_Pop( &stack, Stack_Top( &stack ) ) );
    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( Stack_Count( &stack ) == 1u );
    REQUIRE(
        g_stackAssertCount ==
        static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "Stack move push and explicit shutdown preserve lifecycle invariants",
           "[CypherCommon][Tier1][Stack]" )
{
    cy_stack_t<u32> stack{};
    REQUIRE( Stack_Init( &stack, Allocator_GetSystem() ) );
    u32 nValue = 53u;

    REQUIRE( Stack_PushMove( &stack, static_cast<u32 &&>( nValue ) ) );
    REQUIRE( *Stack_Top( &stack ) == 53u );

    Stack_Shutdown( &stack );
    REQUIRE( Stack_IsValid( &stack ) );
    REQUIRE( Stack_IsEmpty( &stack ) );
}
