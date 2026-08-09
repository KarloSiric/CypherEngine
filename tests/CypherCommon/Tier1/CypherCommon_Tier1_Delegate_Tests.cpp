//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Delegate_Tests.cpp
//  Purpose: Tests non-owning type-erased callbacks.
//  Details: Covers free functions, mutable and const methods, borrowed callable
//           objects, reference arguments, identity comparison, and reset behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Delegate.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

i32 AddValues( i32 left, i32 right ) noexcept
{
    return left + right;
}

void IncrementValue( i32 &value ) noexcept
{
    ++value;
}

struct delegate_target_t {
    i32 nValue{ 0 };

    i32 Add( i32 delta ) noexcept
    {
        nValue += delta;
        return nValue;
    }

    i32 ReadScaled( i32 scale ) const noexcept
    {
        return nValue * scale;
    }
};

struct borrowed_callable_t {
    i32 nBias;

    i32 operator()( i32 value ) const noexcept
    {
        return value + nBias;
    }
};

} // namespace

TEST_CASE( "Delegate binds free functions without object storage",
           "[CypherCommon][Tier1][Delegate]" )
{
    const delegate_t<i32( i32, i32 )> delegate =
        Delegate_BindFunction<i32( i32, i32 ), AddValues>();

    STATIC_REQUIRE( sizeof( delegate_t<void()> ) == sizeof( void * ) * 2u );
    REQUIRE( Delegate_IsBound( delegate ) );
    REQUIRE( delegate.pObject == nullptr );
    REQUIRE( Delegate_Invoke( delegate, 17, 25 ) == 42 );

    i32 value = 8;
    const delegate_t<void( i32 & )> increment =
        Delegate_BindFunction<void( i32 & ), IncrementValue>();
    Delegate_Invoke( increment, value );
    REQUIRE( value == 9 );
}

TEST_CASE( "Delegate binds mutable and const member functions",
           "[CypherCommon][Tier1][Delegate]" )
{
    delegate_target_t target{ 10 };
    const delegate_t<i32( i32 )> add =
        Delegate_BindMethod<i32( i32 ), &delegate_target_t::Add>( &target );
    REQUIRE( Delegate_Invoke( add, 7 ) == 17 );
    REQUIRE( target.nValue == 17 );

    const delegate_target_t &constTarget = target;
    const delegate_t<i32( i32 )> scale =
        Delegate_BindMethod<
            i32( i32 ),
            &delegate_target_t::ReadScaled>( &constTarget );
    REQUIRE( Delegate_Invoke( scale, 3 ) == 51 );
}

TEST_CASE( "Delegate borrows callable objects without taking ownership",
           "[CypherCommon][Tier1][Delegate]" )
{
    const borrowed_callable_t callable{ 9 };
    const delegate_t<i32( i32 )> delegate =
        Delegate_BindCallable<i32( i32 )>( &callable );

    REQUIRE( Delegate_IsBound( delegate ) );
    REQUIRE( Delegate_Invoke( delegate, 33 ) == 42 );
}

TEST_CASE( "Delegate identity includes both target and dispatch thunk",
           "[CypherCommon][Tier1][Delegate]" )
{
    delegate_target_t first{};
    delegate_target_t second{};
    delegate_t<i32( i32 )> firstA =
        Delegate_BindMethod<i32( i32 ), &delegate_target_t::Add>( &first );
    const delegate_t<i32( i32 )> firstB =
        Delegate_BindMethod<i32( i32 ), &delegate_target_t::Add>( &first );
    const delegate_t<i32( i32 )> secondDelegate =
        Delegate_BindMethod<i32( i32 ), &delegate_target_t::Add>( &second );

    REQUIRE( Delegate_Equals( firstA, firstB ) );
    REQUIRE_FALSE( Delegate_Equals( firstA, secondDelegate ) );

    Delegate_Reset( &firstA );
    REQUIRE_FALSE( Delegate_IsBound( firstA ) );
    REQUIRE( firstA.pObject == nullptr );
}

TEST_CASE( "Delegate null object binding produces an unbound delegate",
           "[CypherCommon][Tier1][Delegate]" )
{
    delegate_target_t *pTarget = nullptr;
    const delegate_t<i32( i32 )> delegate =
        Delegate_BindMethod<i32( i32 ), &delegate_target_t::Add>( pTarget );
    REQUIRE_FALSE( Delegate_IsBound( delegate ) );
}
