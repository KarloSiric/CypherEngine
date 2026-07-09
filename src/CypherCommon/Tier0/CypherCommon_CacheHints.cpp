//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CacheHints.cpp
//  Purpose: Implements CypherCommon Tier0 cache hint helpers.
//  Details: Cache hints provide explicit prefetch entry points for hot loops and
//           data-oriented containers without scattering compiler intrinsics.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CacheHints.h"

#include "CypherCommon_Platform.h"
#include "CypherCommon_SystemInfo.h"

#if CYPHER_COMPILER_MSVC
    #include <intrin.h>
#endif

namespace cypher::common
{

void Cache_PrefetchRead( const void *pMemory )
{
    if ( pMemory == nullptr ) {
        return;
    }

#if CYPHER_COMPILER_MSVC
    _mm_prefetch( static_cast<const char *>( pMemory ), _MM_HINT_T0 );
#elif CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
    __builtin_prefetch( pMemory, 0, 3 );
#endif
}

void Cache_PrefetchWrite( const void *pMemory )
{
    if ( pMemory == nullptr ) {
        return;
    }

#if CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
    __builtin_prefetch( pMemory, 1, 3 );
#else
    Cache_PrefetchRead( pMemory );
#endif
}

usize Cache_GetLineSize()
{
    Cy_SystemInfoInit();
    const cy_system_info_t *pInfo = Cy_SystemInfoGet();
    return pInfo != nullptr ? pInfo->cpu.cacheLineSize : CY_CACHE_LINE_SIZE;
}

} // namespace cypher::common
