//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_NetAddress_Tests.cpp
//  Purpose: Tests portable resolved network endpoint values.
//  Details: IPv4, IPv6, ports, numeric scopes, classification, formatting, and
//           invalid host text are tested without opening sockets or resolving DNS.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_NetAddress.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "NetAddress parses and formats numeric IPv4 endpoints",
           "[CypherCommon][Tier1][NetAddress]" )
{
    net_address_t address{};
    REQUIRE( NetAddress_Parse(
        StringView_FromCString( "127.0.0.1:27015" ),
        0u,
        &address ) );
    REQUIRE( address.family == net_address_family_t::IPV4 );
    REQUIRE( address.nPort == 27015u );
    REQUIRE( NetAddress_IsLoopback( address ) );

    char output[64]{};
    REQUIRE( NetAddress_Format( address, CY_TRUE, output, sizeof( output ) ) == 15u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "127.0.0.1:27015" ) ) );
}

TEST_CASE( "NetAddress parses bracketed IPv6 and scope IDs",
           "[CypherCommon][Tier1][NetAddress]" )
{
    net_address_t address{};
    REQUIRE( NetAddress_Parse(
        StringView_FromCString( "[fe80::1%7]:443" ),
        0u,
        &address ) );
    REQUIRE( address.family == net_address_family_t::IPV6 );
    REQUIRE( address.nScopeId == 7u );
    REQUIRE( address.nPort == 443u );

    char output[64]{};
    const usize cchRequired = NetAddress_Format(
        address,
        CY_TRUE,
        output,
        sizeof( output ) );
    REQUIRE( cchRequired == 15u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "[fe80::1%7]:443" ) ) );
}

TEST_CASE( "NetAddress classifies and compares hosts",
           "[CypherCommon][Tier1][NetAddress]" )
{
    net_address_t multicast{};
    REQUIRE( NetAddress_Parse(
        StringView_FromCString( "239.1.2.3" ),
        99u,
        &multicast ) );
    REQUIRE( NetAddress_IsMulticast( multicast ) );

    net_address_t sameHost = multicast;
    sameHost.nPort = 100u;
    REQUIRE( NetAddress_HostEquals( multicast, sameHost ) );
    REQUIRE_FALSE( NetAddress_Equals( multicast, sameHost ) );
    REQUIRE( NetAddress_Equals(
        NetAddress_LoopbackIpv6( 1u ),
        NetAddress_LoopbackIpv6( 1u ) ) );

    const net_address_t loopback = NetAddress_LoopbackIpv4( 27015u );
    REQUIRE( NetAddress_IsValid( loopback ) );
    REQUIRE( NetAddress_IsLoopback( loopback ) );
    REQUIRE( loopback.nPort == 27015u );

    const net_address_t any = NetAddress_AnyIpv4( 27016u );
    REQUIRE( NetAddress_IsValid( any ) );
    REQUIRE_FALSE( NetAddress_IsLoopback( any ) );
    REQUIRE( any.nPort == 27016u );
    REQUIRE_FALSE( NetAddress_IsValid( {} ) );
}

TEST_CASE( "NetAddress rejects names and malformed numeric endpoints",
           "[CypherCommon][Tier1][NetAddress]" )
{
    net_address_t address{};
    REQUIRE_FALSE( NetAddress_Parse(
        StringView_FromCString( "example.com:80" ),
        0u,
        &address ) );
    REQUIRE_FALSE( NetAddress_Parse(
        StringView_FromCString( "127.0.0.1:70000" ),
        0u,
        &address ) );
    REQUIRE_FALSE( NetAddress_Parse(
        StringView_FromCString( "[::1" ),
        0u,
        &address ) );
}
