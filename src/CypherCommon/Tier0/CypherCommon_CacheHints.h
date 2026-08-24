//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CacheHints.h
//  Purpose: Declares optional CPU prefetch hints and cache-line discovery.
//  Details: Prefetch calls are performance hints only; callers must never depend
//           on them for validity, ordering, coherency, or fault suppression.
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

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

enum class cache_prefetch_locality_t : u8 {
    NonTemporal = 0u, // Expected single use; minimize cache retention.
    Low,              // Low temporal reuse.
    Medium,           // Moderate temporal reuse.
    High              // Strong temporal reuse; prefer nearby cache levels.
};

// Hints that a memory address will soon be read. Null is a no-op.
CYPHER_COMMON_API void Cy_CachePrefetchRead(
    const void *pMemory,
    cache_prefetch_locality_t locality = cache_prefetch_locality_t::High ) noexcept;

// Hints that a memory address will soon be written. Null is a no-op.
CYPHER_COMMON_API void Cy_CachePrefetchWrite(
    const void *pMemory,
    cache_prefetch_locality_t locality = cache_prefetch_locality_t::High ) noexcept;

// Returns the detected coherency/cache-line size, or the conservative fallback.
CYPHER_NODISCARD CYPHER_COMMON_API usize Cy_CacheGetLineSize() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CACHEHINTS_H
