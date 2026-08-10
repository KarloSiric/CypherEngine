//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Interface_Tests.cpp
//  Purpose: Tests versioned function-table factories.
//  Details: Copied names, major/minor compatibility, duplicate registration,
//           callback metadata, release, and unregistration are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Interface.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct test_api_t {
    u32 nValue{ 0u };
};

struct factory_state_t {
    test_api_t api{ 77u };
    usize nCreateCount{ 0u };
    usize nReleaseCount{ 0u };
};

void *CreateTestApi( const interface_id_t &, void *pUserData ) noexcept
{
    auto *pState = static_cast<factory_state_t *>( pUserData );
    ++pState->nCreateCount;
    return &pState->api;
}

void ReleaseTestApi( void *, void *pUserData ) noexcept
{
    ++static_cast<factory_state_t *>( pUserData )->nReleaseCount;
}

} // namespace

TEST_CASE( "InterfaceRegistry resolves compatible function-table versions",
           "[CypherCommon][Tier1][Interface]" )
{
    interface_registry_t *pRegistry = InterfaceRegistry_Create(
        Allocator_GetSystem(),
        1u );
    REQUIRE( pRegistry != nullptr );
    factory_state_t state{};
    char mutableName[] = "CypherRenderer";
    const interface_factory_desc_t factory{
        { StringView_FromCString( mutableName ), 2u, 4u },
        CreateTestApi,
        ReleaseTestApi,
        &state
    };
    REQUIRE( InterfaceRegistry_Register( pRegistry, factory ) );
    mutableName[0] = 'x';
    const interface_factory_desc_t duplicate{
        { StringView_FromCString( "CypherRenderer" ), 2u, 4u },
        CreateTestApi,
        ReleaseTestApi,
        &state
    };
    REQUIRE_FALSE( InterfaceRegistry_Register( pRegistry, duplicate ) );

    interface_release_fn_t pfnRelease = nullptr;
    void *pReleaseUserData = nullptr;
    void *pRaw = InterfaceRegistry_CreateInterface(
        pRegistry,
        { StringView_FromCString( "CypherRenderer" ), 2u, 3u },
        &pfnRelease,
        &pReleaseUserData );
    REQUIRE( pRaw == &state.api );
    REQUIRE( pfnRelease == ReleaseTestApi );
    REQUIRE( pReleaseUserData == &state );
    REQUIRE( state.nCreateCount == 1u );
    pfnRelease( pRaw, pReleaseUserData );
    REQUIRE( state.nReleaseCount == 1u );

    REQUIRE( InterfaceRegistry_CreateInterface(
        pRegistry,
        { StringView_FromCString( "CypherRenderer" ), 2u, 5u } ) == nullptr );
    REQUIRE( InterfaceRegistry_CreateInterface(
        pRegistry,
        { StringView_FromCString( "CypherRenderer" ), 3u, 0u } ) == nullptr );
    REQUIRE( InterfaceRegistry_Unregister(
        pRegistry,
        StringView_FromCString( "CypherRenderer" ),
        2u ) );
    REQUIRE( InterfaceRegistry_CreateInterface(
        pRegistry,
        { StringView_FromCString( "CypherRenderer" ), 2u, 0u } ) == nullptr );
    InterfaceRegistry_Destroy( pRegistry );
}
