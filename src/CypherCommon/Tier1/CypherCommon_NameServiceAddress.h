//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_NameServiceAddress.h
//  Purpose: Declares CypherCommon Tier1 NameServiceAddress support.
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

#ifndef CYPHER_COMMON_TIER1_NAMESERVICEADDRESS_H
#define CYPHER_COMMON_TIER1_NAMESERVICEADDRESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Name Service Address

Named network endpoint declarations.
================
*/

#include "CypherCommon_NetAddress.h"

namespace cypher::common
{

struct name_service_address_t {
    char host[256];
    u16 port;
};

bool_t NameServiceAddress_Parse( const char *pString, name_service_address_t *pOutAddress );
bool_t NameServiceAddress_Resolve( const name_service_address_t *pAddress, net_address_t *pOutAddress );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_NAMESERVICEADDRESS_H
