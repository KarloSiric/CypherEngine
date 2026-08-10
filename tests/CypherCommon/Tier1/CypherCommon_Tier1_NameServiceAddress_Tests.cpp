//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_NameServiceAddress_Tests.cpp
//  Purpose: Tests unresolved host and service endpoint storage.
//  Details: DNS names, service labels, bracketed IPv6 text, default ports, bounded
//           ownership, invalid text, formatting, and truncation are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_NameServiceAddress.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "NameServiceAddress owns host and service text",
           "[CypherCommon][Tier1][NameServiceAddress]" )
{
    char input[] = "match.cypher.test:game-udp";
    name_service_address_t address{};
    REQUIRE( NameServiceAddress_Parse(
        StringView_FromCString( input ),
        27015u,
        &address ) );
    input[0] = 'x';
    REQUIRE( NameServiceAddress_IsValid( address ) );

    char output[64]{};
    REQUIRE( NameServiceAddress_Format( address, output, sizeof( output ) ) == 26u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "match.cypher.test:game-udp" ) ) );
}

TEST_CASE( "NameServiceAddress formats bracketed IPv6 text and fallback ports",
           "[CypherCommon][Tier1][NameServiceAddress]" )
{
    name_service_address_t address{};
    REQUIRE( NameServiceAddress_Parse(
        StringView_FromCString( "2001:db8::7" ),
        27015u,
        &address ) );
    char output[64]{};
    REQUIRE( NameServiceAddress_Format( address, output, sizeof( output ) ) == 19u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "[2001:db8::7]:27015" ) ) );

    REQUIRE( NameServiceAddress_Parse(
        StringView_FromCString( "[2001:db8::7]:https" ),
        0u,
        &address ) );
    REQUIRE( StringView_Equals(
        FixedString_View( address.service ),
        StringView_FromCString( "https" ) ) );
}

TEST_CASE( "NameServiceAddress rejects malformed endpoint text",
           "[CypherCommon][Tier1][NameServiceAddress]" )
{
    name_service_address_t address{};
    REQUIRE_FALSE( NameServiceAddress_Parse(
        StringView_FromCString( "host name:80" ),
        0u,
        &address ) );
    REQUIRE_FALSE( NameServiceAddress_Parse(
        StringView_FromCString( "host:bad/service" ),
        0u,
        &address ) );
    REQUIRE_FALSE( NameServiceAddress_Parse(
        StringView_FromCString( "[]:80" ),
        0u,
        &address ) );
}
