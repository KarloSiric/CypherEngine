//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_DataManager_Tests.cpp
//  Purpose: Tests the named data ownership registry.
//  Details: Copied names, duplicate rejection, lookup, detach, removal callbacks,
//           growth, and reverse-order destruction are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_DataManager.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct destroy_state_t {
    i32 order[4]{};
    usize nCount{ 0u };
};

void RecordDestroy( void *pData, void *pUserData ) noexcept
{
    auto *pState = static_cast<destroy_state_t *>( pUserData );
    pState->order[pState->nCount++] = *static_cast<i32 *>( pData );
}

} // namespace

TEST_CASE( "DataManager owns names and explicit destruction policy",
           "[CypherCommon][Tier1][DataManager]" )
{
    data_manager_t *pManager = DataManager_Create( Allocator_GetSystem(), 1u );
    REQUIRE( pManager != nullptr );
    destroy_state_t state{};
    i32 first = 1;
    i32 second = 2;

    char mutableName[] = "first";
    REQUIRE( DataManager_Register(
        pManager,
        { StringView_FromCString( mutableName ), &first, RecordDestroy, &state } ) );
    mutableName[0] = 'x';
    REQUIRE( DataManager_Find( pManager, StringView_FromCString( "first" ) ) == &first );
    REQUIRE_FALSE( DataManager_Register(
        pManager,
        { StringView_FromCString( "first" ), &second, nullptr, nullptr } ) );
    REQUIRE( DataManager_Register(
        pManager,
        { StringView_FromCString( "second" ), &second, RecordDestroy, &state } ) );

    REQUIRE( DataManager_Detach(
        pManager,
        StringView_FromCString( "first" ) ) == &first );
    REQUIRE( state.nCount == 0u );
    REQUIRE( DataManager_Remove( pManager, StringView_FromCString( "second" ) ) );
    REQUIRE( state.nCount == 1u );
    REQUIRE( state.order[0] == 2 );
    DataManager_Destroy( pManager );
}

TEST_CASE( "DataManager clears owned entries in reverse registration order",
           "[CypherCommon][Tier1][DataManager]" )
{
    data_manager_t *pManager = DataManager_Create( Allocator_GetSystem() );
    REQUIRE( pManager != nullptr );
    destroy_state_t state{};
    i32 values[]{ 1, 2, 3 };
    REQUIRE( DataManager_Register( pManager, {
        StringView_FromCString( "one" ), &values[0], RecordDestroy, &state } ) );
    REQUIRE( DataManager_Register( pManager, {
        StringView_FromCString( "two" ), &values[1], RecordDestroy, &state } ) );
    REQUIRE( DataManager_Register( pManager, {
        StringView_FromCString( "three" ), &values[2], RecordDestroy, &state } ) );
    DataManager_Clear( pManager );
    REQUIRE( state.nCount == 3u );
    REQUIRE( state.order[0] == 3 );
    REQUIRE( state.order[1] == 2 );
    REQUIRE( state.order[2] == 1 );
    DataManager_Destroy( pManager );
}
