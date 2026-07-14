//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_NetAddress.h
//  Purpose: Declares CypherCommon Tier1 NetAddress support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_NETADDRESS_H
#define CYPHER_COMMON_TIER1_NETADDRESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Net Address

Network address parsing/storage declarations. Sockets live in CypherNetwork.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum class net_address_type_t : u32 {
    Invalid = 0u,
    IPv4,
    IPv6,
    Loopback
};

struct net_address_t {
    net_address_type_t type;
    u8 bytes[16];
    u16 port;
};

bool_t NetAddress_Parse( const char *pString, net_address_t *pOutAddress );
usize NetAddress_ToString( const net_address_t *pAddress, char *pDest, usize cchDest );
bool_t NetAddress_Equals( const net_address_t *pAddressA, const net_address_t *pAddressB );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_NETADDRESS_H
