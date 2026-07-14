//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashFNV.h
//  Purpose: Declares CypherCommon Tier1 HashFNV support.
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

#ifndef CYPHER_COMMON_TIER1_HASHFNV_H
#define CYPHER_COMMON_TIER1_HASHFNV_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon FNV Hash

FNV hash declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

hash32_t HashFNV1a32_Data( const void *pData, usize cbData );
hash64_t HashFNV1a64_Data( const void *pData, usize cbData );
hash32_t HashFNV1a32_String( const char *pString );
hash64_t HashFNV1a64_String( const char *pString );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHFNV_H
