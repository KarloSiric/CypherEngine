//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_UniqueId.h
//  Purpose: Declares CypherCommon Tier1 UniqueId support.
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

#ifndef CYPHER_COMMON_TIER1_UNIQUEID_H
#define CYPHER_COMMON_TIER1_UNIQUEID_H
#pragma once

/*
================
CypherCommon Unique ID

Unique identifier declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct unique_id_t {
    u64 high;
    u64 low;
};

unique_id_t UniqueId_Create();
unique_id_t UniqueId_FromString( const char *pString );
usize UniqueId_ToString( unique_id_t id, char *pDest, usize cchDest );
bool_t UniqueId_Equals( unique_id_t a, unique_id_t b );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_UNIQUEID_H
