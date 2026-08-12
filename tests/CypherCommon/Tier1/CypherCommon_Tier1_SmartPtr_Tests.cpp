//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_SmartPtr_Tests.cpp
//  Purpose: Tests explicit unique and intrusive pointer ownership.
//  Details: Verifies destructive moves, release semantics, callback user data,
//           explicit intrusive copies, reference balancing, and empty states.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SmartPtr.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct owned_value_t {
    i32 nValue{};
};

void CountDestroy( owned_value_t *, void *pUserData ) noexcept
{
    ++*static_cast<u32 *>( pUserData );
}

struct referenced_value_t {
    u32 nReferences{};
};

void AddReference( referenced_value_t *pValue ) noexcept
{
    ++pValue->nReferences;
}

void ReleaseReference( referenced_value_t *pValue ) noexcept
{
    --pValue->nReferences;
}

} // namespace

TEST_CASE( "UniquePtr destroys once and transfers ownership destructively",
           "[CypherCommon][Tier1][SmartPtr]" )
{
    owned_value_t firstObject{ 7 };
    owned_value_t secondObject{ 11 };
    u32 cDestroyed = 0u;

    {
        unique_ptr_t<owned_value_t> first =
            UniquePtr_Make( &firstObject, CountDestroy, &cDestroyed );
        unique_ptr_t<owned_value_t> second =
            UniquePtr_Make( &secondObject, CountDestroy, &cDestroyed );
        REQUIRE( UniquePtr_IsValid( &first ) );
        REQUIRE( UniquePtr_Get( &first )->nValue == 7 );

        UniquePtr_Reset( &second );
        REQUIRE( cDestroyed == 1u );
        REQUIRE( UniquePtr_IsValid( &second ) );
        REQUIRE( UniquePtr_Get( &second ) == nullptr );
        second = UniquePtr_Make( &secondObject, CountDestroy, &cDestroyed );

        second = static_cast<unique_ptr_t<owned_value_t> &&>( first );
        REQUIRE( cDestroyed == 2u );
        REQUIRE( UniquePtr_Get( &first ) == nullptr );
        REQUIRE( UniquePtr_Get( &second ) == &firstObject );

        REQUIRE( UniquePtr_Release( &second ) == &firstObject );
        REQUIRE( UniquePtr_Get( &second ) == nullptr );
    }
    REQUIRE( cDestroyed == 2u );
}

TEST_CASE( "IntrusivePtr balances explicit copies, moves, and resets",
           "[CypherCommon][Tier1][SmartPtr]" )
{
    referenced_value_t object{};
    {
        intrusive_ptr_t<referenced_value_t> first =
            IntrusivePtr_Acquire( &object, AddReference, ReleaseReference );
        REQUIRE( object.nReferences == 1u );
        REQUIRE( IntrusivePtr_IsValid( &first ) );

        intrusive_ptr_t<referenced_value_t> second = IntrusivePtr_Copy( first );
        REQUIRE( object.nReferences == 2u );
        REQUIRE( IntrusivePtr_Get( &second ) == &object );

        intrusive_ptr_t<referenced_value_t> moved(
            static_cast<intrusive_ptr_t<referenced_value_t> &&>( second ) );
        REQUIRE( object.nReferences == 2u );
        REQUIRE( IntrusivePtr_Get( &second ) == nullptr );
        IntrusivePtr_Reset( &moved );
        REQUIRE( object.nReferences == 1u );
        REQUIRE( IntrusivePtr_IsValid( &moved ) );
        REQUIRE( IntrusivePtr_Get( &moved ) == nullptr );
    }
    REQUIRE( object.nReferences == 0u );
}
