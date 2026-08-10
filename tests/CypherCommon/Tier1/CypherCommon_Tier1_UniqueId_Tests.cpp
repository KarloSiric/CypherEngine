//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_UniqueId_Tests.cpp
//  Purpose: Tests UUID creation, parsing, formatting, and comparison.
//  Details: These tests protect canonical text, transactional parse failure, exact
//           byte import, nil handling, and version-4 random UUID marker bits.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_UniqueId.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "UniqueId round trips canonical UUID text",
           "[CypherCommon][Tier1][UniqueId]" )
{
    const string_view_t text = StringView_FromCString(
        "00112233-4455-6677-8899-aabbccddeeff" );
    unique_id_t id{};
    REQUIRE( UniqueId_FromString( text, &id ) );
    REQUIRE( UniqueId_IsValid( id ) );

    char output[CY_UNIQUE_ID_STRING_CAPACITY]{};
    REQUIRE( UniqueId_ToString( id, output, sizeof( output ) ) ==
             CY_UNIQUE_ID_STRING_LENGTH );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        text ) );

    unique_id_t uppercase{};
    REQUIRE( UniqueId_FromString(
        StringView_FromCString( "00112233-4455-6677-8899-AABBCCDDEEFF" ),
        &uppercase ) );
    REQUIRE( UniqueId_Equals( id, uppercase ) );
}

TEST_CASE( "UniqueId parsing is exact and preserves output on failure",
           "[CypherCommon][Tier1][UniqueId]" )
{
    unique_id_t output{};
    output.bytes[0] = 0xABu;
    const unique_id_t previous = output;

    REQUIRE_FALSE( UniqueId_FromString(
        StringView_FromCString( "001122334455-6677-8899-aabbccddeeff" ),
        &output ) );
    REQUIRE( UniqueId_Equals( output, previous ) );
    REQUIRE_FALSE( UniqueId_FromString(
        StringView_FromCString( "00112233-4455-6677-8899-aabbccddeefg" ),
        &output ) );
    REQUIRE( UniqueId_Equals( output, previous ) );

    char tooSmall[CY_UNIQUE_ID_STRING_LENGTH]{};
    REQUIRE( UniqueId_ToString( output, tooSmall, sizeof( tooSmall ) ) == 0u );
    REQUIRE( tooSmall[0] == '\0' );
}

TEST_CASE( "UniqueId imports bytes and compares lexicographically",
           "[CypherCommon][Tier1][UniqueId]" )
{
    byte source[CY_UNIQUE_ID_BYTE_COUNT]{};
    source[15] = 1u;
    unique_id_t first{};
    REQUIRE( UniqueId_FromBytes(
        { source, CY_UNIQUE_ID_BYTE_COUNT },
        &first ) );

    source[15] = 2u;
    unique_id_t second{};
    REQUIRE( UniqueId_FromBytes(
        { source, CY_UNIQUE_ID_BYTE_COUNT },
        &second ) );
    REQUIRE( UniqueId_Compare( first, second ) < 0 );
    REQUIRE( UniqueId_Compare( second, first ) > 0 );
    REQUIRE( UniqueId_Compare( first, first ) == 0 );
    REQUIRE_FALSE( UniqueId_IsValid( CY_UNIQUE_ID_INVALID ) );

    unique_id_t unchanged = second;
    REQUIRE_FALSE( UniqueId_FromBytes(
        { source, CY_UNIQUE_ID_BYTE_COUNT - 1u },
        &unchanged ) );
    REQUIRE( UniqueId_Equals( unchanged, second ) );
}

TEST_CASE( "UniqueId random creation sets UUID version and variant bits",
           "[CypherCommon][Tier1][UniqueId]" )
{
    unique_id_t id{};
    REQUIRE( UniqueId_CreateRandom( &id ) );
    REQUIRE( UniqueId_IsValid( id ) );
    REQUIRE( ( id.bytes[6] & 0xF0u ) == 0x40u );
    REQUIRE( ( id.bytes[8] & 0xC0u ) == 0x80u );
}
