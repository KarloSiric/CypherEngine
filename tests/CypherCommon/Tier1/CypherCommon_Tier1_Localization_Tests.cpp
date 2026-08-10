//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Localization_Tests.cpp
//  Purpose: Tests owned locale catalogs and deterministic substitution.
//  Details: Covers copied input, replacement, ID and key lookup, escaped braces,
//           unresolved fields, truncation, size queries, updates, and clear.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Localization.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Localization owns entries and resolves stable IDs and exact keys",
           "[CypherCommon][Tier1][Localization]" )
{
    localization_catalog_t *pCatalog = Localization_CreateCatalog(
        { Allocator_GetSystem(), StringView_FromCString( "en-US" ), 1u } );
    REQUIRE( pCatalog != nullptr );

    char mutableText[]{ "Welcome" };
    REQUIRE( Localization_Add(
        pCatalog,
        StringView_FromCString( "menu.welcome" ),
        StringView_FromCString( mutableText ) ) );
    mutableText[0] = 'X';

    const localized_string_id_t id = Localization_IdFromKey(
        StringView_FromCString( "menu.welcome" ) );
    REQUIRE( StringView_Equals(
        Localization_Find( pCatalog, id ),
        StringView_FromCString( "Welcome" ) ) );
    REQUIRE( StringView_Equals(
        Localization_FindByKey(
            pCatalog,
            StringView_FromCString( "menu.welcome" ) ),
        StringView_FromCString( "Welcome" ) ) );
    REQUIRE( Localization_Count( pCatalog ) == 1u );
    REQUIRE( StringView_Equals(
        Localization_LocaleTag( pCatalog ),
        StringView_FromCString( "en-US" ) ) );

    REQUIRE( Localization_Add(
        pCatalog,
        StringView_FromCString( "menu.welcome" ),
        StringView_FromCString( "Hello" ) ) );
    REQUIRE( Localization_Count( pCatalog ) == 1u );
    REQUIRE( StringView_Equals(
        Localization_Find( pCatalog, id ),
        StringView_FromCString( "Hello" ) ) );
    Localization_DestroyCatalog( pCatalog );
}

TEST_CASE( "Localization formats named fields and reports required output",
           "[CypherCommon][Tier1][Localization]" )
{
    localization_catalog_t *pCatalog = Localization_CreateCatalog(
        { Allocator_GetSystem(), StringView_FromCString( "en" ), 0u } );
    REQUIRE( pCatalog != nullptr );
    const string_view_t key = StringView_FromCString( "hud.score" );
    REQUIRE( Localization_Add(
        pCatalog,
        key,
        StringView_FromCString( "{{Player}} {name}: {score}; {missing}" ) ) );
    const localization_argument_t arguments[]{
        { StringView_FromCString( "name" ), StringView_FromCString( "Karlo" ) },
        { StringView_FromCString( "score" ), StringView_FromCString( "42" ) }
    };

    const localized_string_id_t id = Localization_IdFromKey( key );
    const usize cchRequired = Localization_Format(
        pCatalog, id, arguments, 2u, nullptr, 0u );
    REQUIRE( cchRequired == 29u );

    char output[64]{};
    REQUIRE( Localization_Format(
        pCatalog, id, arguments, 2u, output, sizeof( output ) ) == cchRequired );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "{Player} Karlo: 42; {missing}" ) ) );

    char truncated[8]{};
    REQUIRE( Localization_Format(
        pCatalog, id, arguments, 2u, truncated, sizeof( truncated ) ) == cchRequired );
    REQUIRE( truncated[sizeof( truncated ) - 1u] == '\0' );
    Localization_Clear( pCatalog );
    REQUIRE( Localization_Count( pCatalog ) == 0u );
    Localization_DestroyCatalog( pCatalog );
}
