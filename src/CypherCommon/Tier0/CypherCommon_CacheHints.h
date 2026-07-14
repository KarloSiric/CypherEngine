//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CacheHints.h
//  Purpose: Declares CypherCommon Tier0 CacheHints support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_CACHEHINTS_H
#define CYPHER_COMMON_TIER0_CACHEHINTS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Cache Hints

Cache prefetch and cache-line helper declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

void Cache_PrefetchRead( const void *pMemory );
void Cache_PrefetchWrite( const void *pMemory );
usize Cache_GetLineSize();

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CACHEHINTS_H
