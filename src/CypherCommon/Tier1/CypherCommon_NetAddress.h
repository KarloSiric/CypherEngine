//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_NetAddress.h
//  Purpose: Declares portable resolved IPv4/IPv6 endpoint values.
//  Details: Ports are stored in host byte order. Parsing and formatting do not perform
//           DNS lookup; name resolution belongs to the network/platform layer.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Net Address Contract

Address values remain independent of native socket structures. Parsing and formatting are
bounded, while DNS or transport ownership belongs to the higher networking layer.
================
*/

#ifndef CYPHER_COMMON_TIER1_NETADDRESS_H
#define CYPHER_COMMON_TIER1_NETADDRESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class net_address_family_t : u8 {
    INVALID = 0u, // No usable address bytes.
    IPV4,         // First four address bytes contain an IPv4 address.
    IPV6          // All sixteen address bytes contain an IPv6 address.
};

struct net_address_t {
    byte address[16]{}; // Network-order address bytes selected by family.
    u32 nScopeId{ 0u }; // IPv6 interface scope; zero for unscoped and IPv4 addresses.
    u16 nPort{ 0u };    // Host-order transport port.
    net_address_family_t family{ net_address_family_t::INVALID }; // Active address layout.
};

CYPHER_NODISCARD CYPHER_COMMON_API
net_address_t NetAddress_AnyIpv4( u16 nPort ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
net_address_t NetAddress_LoopbackIpv4( u16 nPort ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
net_address_t NetAddress_LoopbackIpv6( u16 nPort ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t NetAddress_Parse(
    string_view_t text,
    u16 nDefaultPort,
    net_address_t *pAddressOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize NetAddress_Format(
    net_address_t address,
    bool_t bIncludePort,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t NetAddress_IsValid( net_address_t address ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t NetAddress_IsLoopback( net_address_t address ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t NetAddress_IsMulticast( net_address_t address ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t NetAddress_Equals( net_address_t left, net_address_t right ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t NetAddress_HostEquals( net_address_t left, net_address_t right ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_NETADDRESS_H
