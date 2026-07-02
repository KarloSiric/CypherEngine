//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ContentHash.h
//  Purpose: Declares CypherCommon Tier1 ContentHash support.
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

#ifndef CYPHER_COMMON_TIER1_CONTENTHASH_H
#define CYPHER_COMMON_TIER1_CONTENTHASH_H
#pragma once

/*
================
CypherCommon Content Hash

Stable asset/content hash declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct content_hash_t {
    u64 high;
    u64 low;
};

content_hash_t ContentHash_Data( const void *pData, usize cbData );
bool_t ContentHash_Equals( content_hash_t a, content_hash_t b );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONTENTHASH_H
