//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Optional_Tests.cpp
//  Purpose: Tests allocation-free optional-value storage and ownership.
//  Details: Protects empty-state access, in-place replacement, destruction,
//           destructive move transfer, value extraction, and over-alignment.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Optional.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct lifetime_state_t {
    u32 cConstructed{};
    u32 cDestroyed{};
    u32 cMoved{};
    u32 cMoveAssigned{};
};

struct tracked_value_t {
    lifetime_state_t *pState{};
    i32 nValue{};

    tracked_value_t( lifetime_state_t *pLifetimeState, i32 nInitialValue ) noexcept
        : pState( pLifetimeState )
        , nValue( nInitialValue )
    {
        ++pState->cConstructed;
    }

    tracked_value_t( const tracked_value_t &other ) noexcept
        : pState( other.pState )
        , nValue( other.nValue )
    {
        ++pState->cConstructed;
    }

    tracked_value_t( tracked_value_t &&other ) noexcept
        : pState( other.pState )
        , nValue( other.nValue )
    {
        ++pState->cConstructed;
        ++pState->cMoved;
        other.nValue = -1;
    }

    tracked_value_t &operator=( tracked_value_t &&other ) noexcept
    {
        pState = other.pState;
        nValue = other.nValue;
        ++pState->cMoveAssigned;
        other.nValue = -1;
        return *this;
    }

    ~tracked_value_t() noexcept
    {
        ++pState->cDestroyed;
    }
};

struct alignas( 64 ) aligned_value_t {
    u64 nValue{};

    explicit aligned_value_t( u64 nInitialValue ) noexcept
        : nValue( nInitialValue )
    {
    }
};

u32 g_optionalAssertCount = 0u;

assert_action_t CaptureOptionalAssert( const assert_info_t & ) noexcept
{
    ++g_optionalAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Optional is move-only in-place storage",
           "[CypherCommon][Tier1][Optional]" )
{
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<optional_t<u32>> );
    STATIC_REQUIRE_FALSE( is_copy_assignable_v<optional_t<u32>> );
    STATIC_REQUIRE( is_nothrow_move_constructible_v<optional_t<u32>> );
    STATIC_REQUIRE( is_nothrow_move_assignable_v<optional_t<u32>> );
    STATIC_REQUIRE( alignof( optional_t<aligned_value_t> ) >= 64u );
}

TEST_CASE( "Optional empty state has no accessible value",
           "[CypherCommon][Tier1][Optional]" )
{
    optional_t<u32> optional{};

    REQUIRE_FALSE( Optional_HasValue( optional ) );
    REQUIRE( Optional_Get( &optional ) == nullptr );

    const optional_t<u32> &constOptional = optional;
    STATIC_REQUIRE( is_same_v<
        decltype( Optional_Get( &constOptional ) ),
        const u32 *> );
    REQUIRE( Optional_Get( &constOptional ) == nullptr );
}

TEST_CASE( "Optional replacement destroys the previous value exactly once",
           "[CypherCommon][Tier1][Optional]" )
{
    lifetime_state_t state{};
    {
        optional_t<tracked_value_t> optional{};
        tracked_value_t *pFirst = Optional_EmplaceArgs( &optional, &state, 11 );

        REQUIRE( pFirst != nullptr );
        REQUIRE( pFirst->nValue == 11 );
        REQUIRE( Optional_HasValue( optional ) );
        REQUIRE( state.cConstructed == 1u );
        REQUIRE( state.cDestroyed == 0u );

        tracked_value_t *pSecond = Optional_EmplaceArgs( &optional, &state, 29 );
        REQUIRE( pSecond != nullptr );
        REQUIRE( pSecond->nValue == 29 );
        REQUIRE( state.cConstructed == 2u );
        REQUIRE( state.cDestroyed == 1u );

        Optional_Reset( &optional );
        REQUIRE_FALSE( Optional_HasValue( optional ) );
        REQUIRE( state.cDestroyed == 2u );
    }

    REQUIRE( state.cConstructed == state.cDestroyed );
}

TEST_CASE( "Optional move construction transfers and clears ownership",
           "[CypherCommon][Tier1][Optional]" )
{
    lifetime_state_t state{};
    {
        optional_t<tracked_value_t> source{};
        REQUIRE( Optional_EmplaceArgs( &source, &state, 42 ) != nullptr );

        optional_t<tracked_value_t> destination{
            static_cast<optional_t<tracked_value_t> &&>( source )
        };

        REQUIRE_FALSE( Optional_HasValue( source ) );
        REQUIRE( Optional_HasValue( destination ) );
        REQUIRE( Optional_Get( &destination )->nValue == 42 );
        REQUIRE( state.cMoved == 1u );
    }

    REQUIRE( state.cConstructed == state.cDestroyed );
}

TEST_CASE( "Optional move assignment transfers empty and populated states",
           "[CypherCommon][Tier1][Optional]" )
{
    lifetime_state_t state{};
    {
        optional_t<tracked_value_t> source{};
        optional_t<tracked_value_t> destination{};
        REQUIRE( Optional_EmplaceArgs( &source, &state, 7 ) != nullptr );
        REQUIRE( Optional_EmplaceArgs( &destination, &state, 9 ) != nullptr );

        destination = static_cast<optional_t<tracked_value_t> &&>( source );
        REQUIRE_FALSE( Optional_HasValue( source ) );
        REQUIRE( Optional_HasValue( destination ) );
        REQUIRE( Optional_Get( &destination )->nValue == 7 );
        REQUIRE( state.cMoveAssigned == 1u );

        optional_t<tracked_value_t> empty{};
        destination = static_cast<optional_t<tracked_value_t> &&>( empty );
        REQUIRE_FALSE( Optional_HasValue( destination ) );
    }

    REQUIRE( state.cConstructed == state.cDestroyed );
}

TEST_CASE( "Optional take moves the value out and resets storage",
           "[CypherCommon][Tier1][Optional]" )
{
    lifetime_state_t state{};
    {
        optional_t<tracked_value_t> optional{};
        tracked_value_t output{ &state, 0 };
        REQUIRE( Optional_EmplaceArgs( &optional, &state, 73 ) != nullptr );

        REQUIRE( Optional_Take( &optional, &output ) );
        REQUIRE( output.nValue == 73 );
        REQUIRE_FALSE( Optional_HasValue( optional ) );
        REQUIRE_FALSE( Optional_Take( &optional, &output ) );
    }

    REQUIRE( state.cConstructed == state.cDestroyed );
}

TEST_CASE( "Optional preserves over-aligned value addresses",
           "[CypherCommon][Tier1][Optional]" )
{
    optional_t<aligned_value_t> optional{};
    aligned_value_t *pValue = Optional_EmplaceArgs( &optional, 99u );

    REQUIRE( pValue != nullptr );
    REQUIRE( pValue->nValue == 99u );
    REQUIRE( reinterpret_cast<uintptr>( pValue ) % alignof( aligned_value_t ) == 0u );
}

TEST_CASE( "Optional invalid pointers assert and fail safely",
           "[CypherCommon][Tier1][Optional]" )
{
    g_optionalAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureOptionalAssert );

    optional_t<u32> optional{};
    u32 value = 0u;
    REQUIRE( Optional_Get( static_cast<optional_t<u32> *>( nullptr ) ) == nullptr );
    REQUIRE( Optional_EmplaceArgs<u32>( nullptr, 1u ) == nullptr );
    Optional_Reset<u32>( nullptr );
    REQUIRE_FALSE( Optional_Take( &optional, static_cast<u32 *>( nullptr ) ) );
    REQUIRE_FALSE( Optional_Take( static_cast<optional_t<u32> *>( nullptr ), &value ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_optionalAssertCount ==
        5u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
