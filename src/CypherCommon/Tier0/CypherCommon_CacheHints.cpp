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

#if CYPHER_COMPILER_MSVC && CYPHER_ARCH_X64
    #include <intrin.h>
#endif

namespace cypher::common
{

// Prefetch is a non-binding performance hint. Unsupported targets intentionally
// degrade to a no-op rather than changing program correctness.
namespace
{

#if CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
template <i32 nWrite>
void Cache_BuiltinPrefetch(
    const void *pMemory,
    cache_prefetch_locality_t locality ) noexcept
{
    static_assert( nWrite == 0 || nWrite == 1, "Prefetch mode must be read or write." );
    // GCC/Clang locality values map directly from 0 (none) to 3 (high).
    switch ( locality ) {
        case cache_prefetch_locality_t::NonTemporal:
            __builtin_prefetch( pMemory, nWrite, 0 );
            break;
        case cache_prefetch_locality_t::Low:
            __builtin_prefetch( pMemory, nWrite, 1 );
            break;
        case cache_prefetch_locality_t::Medium:
            __builtin_prefetch( pMemory, nWrite, 2 );
            break;
        case cache_prefetch_locality_t::High:
        default:
            __builtin_prefetch( pMemory, nWrite, 3 );
            break;
    }
}
#endif

} // namespace

void Cy_CachePrefetchRead(
    const void *pMemory,
    cache_prefetch_locality_t locality ) noexcept
{
    if ( pMemory == nullptr ) {
        return;
    }

#if CYPHER_COMPILER_MSVC && CYPHER_ARCH_X64
    // MSVC exposes x86 read-prefetch hints only; write-prefetch falls back to the
    // same non-binding hint below on unsupported compiler/architecture pairs.
    switch ( locality ) {
        case cache_prefetch_locality_t::NonTemporal:
            _mm_prefetch( static_cast<const char *>( pMemory ), _MM_HINT_NTA );
            break;
        case cache_prefetch_locality_t::Low:
            _mm_prefetch( static_cast<const char *>( pMemory ), _MM_HINT_T2 );
            break;
        case cache_prefetch_locality_t::Medium:
            _mm_prefetch( static_cast<const char *>( pMemory ), _MM_HINT_T1 );
            break;
        case cache_prefetch_locality_t::High:
        default:
            _mm_prefetch( static_cast<const char *>( pMemory ), _MM_HINT_T0 );
            break;
    }
#elif CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
    Cache_BuiltinPrefetch<0>( pMemory, locality );
#else
    ( void )locality;
#endif
}

void Cy_CachePrefetchWrite(
    const void *pMemory,
    cache_prefetch_locality_t locality ) noexcept
{
    if ( pMemory == nullptr ) {
        return;
    }

#if CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
    Cache_BuiltinPrefetch<1>( pMemory, locality );
#else
    Cy_CachePrefetchRead( pMemory, locality );
#endif
}

usize Cy_CacheGetLineSize() noexcept
{
    // CPUDetect owns platform queries and SystemInfo owns the cached snapshot.
    // Never issue CPUID/sysctl from a hot prefetch call site.
    const cy_system_info_t *pInfo = Cy_SystemInfoGet();
    const usize nDetectedSize =
        pInfo != nullptr ? pInfo->cpu.cacheLineSize : 0u;
    return nDetectedSize != 0u ? nDetectedSize : CY_DEFAULT_CACHE_LINE_SIZE;
}

} // namespace cypher::common
