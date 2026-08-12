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

TEST_CASE( "Resource type IDs reject every ASCII separator and control byte",
           "[CypherCommon][ResourceSystem][ResourceId]" )
{
    constexpr char invalidCharacters[]{
        ' ', '\t', '\n', '\r', '\v', '\f', static_cast<char>( 0x1Fu ),
        static_cast<char>( 0x7Fu )
    };

    for ( char invalid : invalidCharacters ) {
        const char name[]{ 'm', 'a', invalid, 'p' };
        CAPTURE( static_cast<u32>( static_cast<unsigned char>( invalid ) ) );
        REQUIRE( ResourceTypeId_FromName( { name, sizeof( name ) } ) == 0u );
    }

    REQUIRE( ResourceTypeId_FromName(
        StringView_FromCString( "material.instance-v2" ) ) != 0u );
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

TEST_CASE( "Resource ID hashing preserves the caller's canonical path bytes",
           "[CypherCommon][ResourceSystem][ResourceId]" )
{
    const resource_type_id_t type = ResourceTypeId_FromName(
        StringView_FromCString( "texture" ) );
    const resource_id_t lower = ResourceId_FromPath(
        StringView_FromCString( "textures/facility/panel.cytex" ), type );
    const resource_id_t upper = ResourceId_FromPath(
        StringView_FromCString( "Textures/Facility/Panel.cytex" ), type );
    const resource_id_t trailingSlash = ResourceId_FromPath(
        StringView_FromCString( "textures/facility/panel.cytex/" ), type );

    REQUIRE( ResourceId_IsValid( lower ) );
    REQUIRE_FALSE( ResourceId_Equals( lower, upper ) );
    REQUIRE_FALSE( ResourceId_Equals( lower, trailingSlash ) );
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

TEST_CASE( "Resource ID text conversion round trips boundary bit patterns",
           "[CypherCommon][ResourceSystem][ResourceId]" )
{
    constexpr resource_id_t values[]{
        { 1u },
        { 0x00000000FFFFFFFFull },
        { 0xFFFFFFFF00000000ull },
        { CY_U64_MAX }
    };

    for ( resource_id_t source : values ) {
        char text[CY_RESOURCE_ID_STRING_CAPACITY]{};
        REQUIRE( ResourceId_ToString( source, text, sizeof( text ) ) ==
                 CY_RESOURCE_ID_STRING_LENGTH );
        REQUIRE( text[CY_RESOURCE_ID_STRING_LENGTH] == '\0' );

        resource_id_t parsed{};
        REQUIRE( ResourceId_FromString(
            { text, CY_RESOURCE_ID_STRING_LENGTH }, &parsed ) );
        REQUIRE( ResourceId_Equals( source, parsed ) );
    }
}

TEST_CASE( "Resource ID parsing is transactional for every malformed digit",
           "[CypherCommon][ResourceSystem][ResourceId]" )
{
    constexpr char validText[] = "0123456789abcdef";
    for ( usize iDigit = 0u; iDigit < CY_RESOURCE_ID_STRING_LENGTH; ++iDigit ) {
        char malformed[CY_RESOURCE_ID_STRING_CAPACITY]{};
        for ( usize i = 0u; i < CY_RESOURCE_ID_STRING_LENGTH; ++i ) {
            malformed[i] = validText[i];
        }
        malformed[iDigit] = 'g';

        resource_id_t output{ 0xA5A5A5A5A5A5A5A5ull };
        CAPTURE( iDigit );
        REQUIRE_FALSE( ResourceId_FromString(
            { malformed, CY_RESOURCE_ID_STRING_LENGTH }, &output ) );
        REQUIRE( output.value == 0xA5A5A5A5A5A5A5A5ull );
    }

    resource_id_t output{ 99u };
    REQUIRE_FALSE( ResourceId_FromString(
        StringView_FromCString( "0123456789abcde" ), &output ) );
    REQUIRE( output.value == 99u );
    REQUIRE_FALSE( ResourceId_FromString(
        StringView_FromCString( "0123456789abcdef0" ), &output ) );
    REQUIRE( output.value == 99u );
}
