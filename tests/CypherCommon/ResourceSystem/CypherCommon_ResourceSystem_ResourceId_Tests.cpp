//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/ResourceSystem/CypherCommon_ResourceSystem_ResourceId_Tests.cpp
//  Purpose: Tests deterministic compact resource identifiers.
//  Details: These tests protect type-name canonicalization, path/type separation,
//           invalid sentinels, hexadecimal conversion, and parse transactions.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ResourceId.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Resource type IDs are stable and ASCII case-insensitive",
           "[CypherCommon][ResourceSystem][ResourceId]" )
{
    const resource_type_id_t lower = ResourceTypeId_FromName(
        StringView_FromCString( "material" ) );
    const resource_type_id_t upper = ResourceTypeId_FromName(
        StringView_FromCString( "MATERIAL" ) );
    REQUIRE( lower != 0u );
    REQUIRE( lower == upper );
    REQUIRE( lower == 0xD2E4D060u );
    REQUIRE( ResourceTypeId_FromName( {} ) == 0u );

    const char nonAscii[]{ static_cast<char>( 0xC3u ), static_cast<char>( 0xA9u ) };
    REQUIRE( ResourceTypeId_FromName( { nonAscii, sizeof( nonAscii ) } ) == 0u );
}

TEST_CASE( "Resource IDs separate canonical path and resource type",
           "[CypherCommon][ResourceSystem][ResourceId]" )
{
    const resource_type_id_t materialType = ResourceTypeId_FromName(
        StringView_FromCString( "material" ) );
    const resource_type_id_t textureType = ResourceTypeId_FromName(
        StringView_FromCString( "texture" ) );
    const string_view_t path = StringView_FromCString(
        "materials/facility/wall_01.cymat" );

    const resource_id_t material = ResourceId_FromPath( path, materialType );
    const resource_id_t repeated = ResourceId_FromPath( path, materialType );
    const resource_id_t texture = ResourceId_FromPath( path, textureType );
    const resource_id_t otherPath = ResourceId_FromPath(
        StringView_FromCString( "materials/facility/wall_02.cymat" ),
        materialType );

    REQUIRE( ResourceId_IsValid( material ) );
    REQUIRE( material.value == 0x0DCD4E8493231B66ull );
    REQUIRE( ResourceId_Equals( material, repeated ) );
    REQUIRE_FALSE( ResourceId_Equals( material, texture ) );
    REQUIRE_FALSE( ResourceId_Equals( material, otherPath ) );
    REQUIRE_FALSE( ResourceId_IsValid( ResourceId_FromPath( {}, materialType ) ) );
    REQUIRE_FALSE( ResourceId_IsValid( ResourceId_FromPath( path, 0u ) ) );
}

TEST_CASE( "Resource IDs round trip fixed-width hexadecimal text",
           "[CypherCommon][ResourceSystem][ResourceId]" )
{
    const resource_id_t source{ 0x0123456789ABCDEFull };
    char text[CY_RESOURCE_ID_STRING_CAPACITY]{};
    REQUIRE( ResourceId_ToString( source, text, sizeof( text ) ) ==
             CY_RESOURCE_ID_STRING_LENGTH );
    REQUIRE( StringView_Equals(
        StringView_FromCString( text ),
        StringView_FromCString( "0123456789abcdef" ) ) );

    resource_id_t parsed{};
    REQUIRE( ResourceId_FromString(
        StringView_FromCString( "0123456789ABCDEF" ),
        &parsed ) );
    REQUIRE( ResourceId_Equals( parsed, source ) );

    resource_id_t unchanged{ 42u };
    REQUIRE_FALSE( ResourceId_FromString(
        StringView_FromCString( "0123456789abcdeg" ),
        &unchanged ) );
    REQUIRE( unchanged.value == 42u );
    REQUIRE_FALSE( ResourceId_FromString(
        StringView_FromCString( "0000000000000000" ),
        &unchanged ) );
    REQUIRE( unchanged.value == 42u );
}
