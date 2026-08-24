//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Simd.h
//  Purpose: Declares SIMD features usable by both hardware and this binary.
//  Details: Runtime hardware support is intersected with instructions compiled
//           into the executable. Dispatch code must use usableFeatures, not CPUID.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_SIMD_H
#define CYPHER_COMMON_TIER0_SIMD_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon SIMD

SIMD capability declarations. Math code can query this layer without scattering
compiler or CPU feature checks through the engine.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"

namespace cypher::common
{

constexpr u32 CY_SIMD_SCALAR_REGISTER_BYTES = 0u; // Scalar fallback has no vector width.
constexpr u32 CY_SIMD_128_REGISTER_BYTES = 16u;   // SSE/NEON register width.
constexpr u32 CY_SIMD_256_REGISTER_BYTES = 32u;   // AVX/AVX2 register width.

enum cy_simd_level_t : u32 {
    CY_SIMD_LEVEL_SCALAR = 0u, // Portable scalar implementation.
    CY_SIMD_LEVEL_SSE2,        // Baseline 128-bit x86 SIMD.
    CY_SIMD_LEVEL_SSE41,       // SSE4.1 implementation family.
    CY_SIMD_LEVEL_AVX,         // 256-bit AVX implementation family.
    CY_SIMD_LEVEL_AVX2,        // 256-bit integer-capable AVX2 family.
    CY_SIMD_LEVEL_NEON         // 128-bit ARM Advanced SIMD family.
};

enum cy_simd_feature_flags_t : flags64_t {
    CY_SIMD_FEATURE_NONE = 0ull,

    CY_SIMD_FEATURE_SSE2 = CYPHER_BIT64( 0 ),
    CY_SIMD_FEATURE_SSE3 = CYPHER_BIT64( 1 ),
    CY_SIMD_FEATURE_SSSE3 = CYPHER_BIT64( 2 ),
    CY_SIMD_FEATURE_SSE41 = CYPHER_BIT64( 3 ),
    CY_SIMD_FEATURE_SSE42 = CYPHER_BIT64( 4 ),

    CY_SIMD_FEATURE_AVX = CYPHER_BIT64( 5 ),
    CY_SIMD_FEATURE_AVX2 = CYPHER_BIT64( 6 ),

    CY_SIMD_FEATURE_NEON = CYPHER_BIT64( 7 )
};

struct cy_simd_caps_t {
    flags64_t cpuFeatures;      // Features CPUDetect says the OS permits.
    flags64_t compiledFeatures; // Implementations present in this binary.
    flags64_t usableFeatures;   // Intersection used for runtime dispatch.

    cy_simd_level_t bestLevel;  // Highest preferred usable implementation.

    u32 vectorRegisterBytes;    // Width associated with bestLevel.
    u32 vectorRegisterBits;     // Same width expressed in bits for diagnostics.
};

// Initializes the immutable process-lifetime SIMD capability snapshot.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_SimdInit() noexcept;

// Returns immutable SIMD capabilities, initializing on first use.
CYPHER_NODISCARD CYPHER_COMMON_API const cy_simd_caps_t *Cy_SimdGetCaps() noexcept;

// Returns true if feature exists in the supplied SIMD feature mask.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_SimdHasFeature(
    flags64_t features,
    cy_simd_feature_flags_t feature ) noexcept;

// Returns true if the current engine build can use this SIMD feature.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_SimdCanUse(
    cy_simd_feature_flags_t feature ) noexcept;

// Returns the preferred SIMD level for this machine/build.
CYPHER_NODISCARD CYPHER_COMMON_API cy_simd_level_t Cy_SimdGetBestLevel() noexcept;

// Returns the preferred vector register width in bytes.
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_SimdGetVectorRegisterBytes() noexcept;

// Returns a stable diagnostic name for a SIMD feature.
CYPHER_NODISCARD CYPHER_COMMON_API const char *Cy_SimdFeatureName(
    cy_simd_feature_flags_t feature ) noexcept;

// Returns a stable diagnostic name for a SIMD level.
CYPHER_NODISCARD CYPHER_COMMON_API const char *Cy_SimdLevelName(
    cy_simd_level_t level ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_SIMD_H
