//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Hash.h
//  Purpose: Declares CypherCommon Tier1 Hash support.
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

#ifndef CYPHER_COMMON_TIER1_HASH_H
#define CYPHER_COMMON_TIER1_HASH_H
#pragma once

/*
================
CypherCommon Hash

General hash declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

hash32_t Hash32_Data( const void *pData, usize cbData, hash32_t seed );
hash64_t Hash64_Data( const void *pData, usize cbData, hash64_t seed );
hash32_t Hash32_String( const char *pString );
hash64_t Hash64_String( const char *pString );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASH_H
