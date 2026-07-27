//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_BuildId_Tests.cpp
//  Purpose: Tests Tier0 immutable build identity.
//  Details: These tests validate configured engine/game metadata, identity
//           validation, exact required formatting size, and bounded output.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BuildId.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace cypher::common;

TEST_CASE( "BuildId exposes immutable engine and game identities", "[CypherCommon][Tier0][BuildId]" )
{
    const build_id_t *pEngine = Cy_BuildIdGetEngine();
    const build_id_t *pGame = Cy_BuildIdGetGame();

    REQUIRE( Cy_BuildIdIsValid( pEngine ) );
    REQUIRE( Cy_BuildIdIsValid( pGame ) );
    REQUIRE( pEngine == Cy_BuildIdGetEngine() );
    REQUIRE( pGame == Cy_BuildIdGetGame() );
    REQUIRE( std::strcmp( pEngine->pszProductName, "CypherEngine" ) == 0 );
    REQUIRE( std::strcmp( pGame->pszProductName, "REAP" ) == 0 );
    REQUIRE( pEngine->pszVersion[0] != '\0' );
}

TEST_CASE( "BuildId formatting is queryable and bounded", "[CypherCommon][Tier0][BuildId]" )
{
    const build_id_t *pBuild = Cy_BuildIdGetEngine();
    const usize cchRequired = Cy_BuildIdFormat( pBuild, nullptr, 0u );
    REQUIRE( cchRequired > 0u );

    char szText[256] = {};
    REQUIRE( Cy_BuildIdFormat( pBuild, szText, sizeof( szText ) ) == cchRequired );
    REQUIRE( std::strlen( szText ) == cchRequired );

    char szSmall[8] = {};
    REQUIRE( Cy_BuildIdFormat( pBuild, szSmall, sizeof( szSmall ) ) == cchRequired );
    REQUIRE( szSmall[sizeof( szSmall ) - 1u] == '\0' );

    REQUIRE_FALSE( Cy_BuildIdIsValid( nullptr ) );
    REQUIRE( Cy_BuildIdFormat( nullptr, szText, sizeof( szText ) ) == 0u );
    REQUIRE( szText[0] == '\0' );
}
