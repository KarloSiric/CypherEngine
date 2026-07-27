//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Simd.h
//  Purpose: Declares CypherCommon Tier0 Simd support.
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

constexpr u32 CY_SIMD_SCALAR_REGISTER_BYTES = 0u;
constexpr u32 CY_SIMD_128_REGISTER_BYTES = 16u;
constexpr u32 CY_SIMD_256_REGISTER_BYTES = 32u;

enum cy_simd_level_t : u32 {
    CY_SIMD_LEVEL_SCALAR = 0u,
    CY_SIMD_LEVEL_SSE2,
    CY_SIMD_LEVEL_SSE41,
    CY_SIMD_LEVEL_AVX,
    CY_SIMD_LEVEL_AVX2,
    CY_SIMD_LEVEL_NEON
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
    flags64_t cpuFeatures;
    flags64_t compiledFeatures;
    flags64_t usableFeatures;

    cy_simd_level_t bestLevel;

    u32 vectorRegisterBytes;
    u32 vectorRegisterBits;
};

// Initializes the immutable process-lifetime SIMD capability snapshot.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SimdInit() noexcept;

// Returns immutable SIMD capabilities, initializing on first use.
[[nodiscard]] CYPHER_COMMON_API const cy_simd_caps_t *Cy_SimdGetCaps() noexcept;

// Returns true if feature exists in the supplied SIMD feature mask.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SimdHasFeature(
    flags64_t features,
    cy_simd_feature_flags_t feature ) noexcept;

// Returns true if the current engine build can use this SIMD feature.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SimdCanUse(
    cy_simd_feature_flags_t feature ) noexcept;

// Returns the preferred SIMD level for this machine/build.
[[nodiscard]] CYPHER_COMMON_API cy_simd_level_t Cy_SimdGetBestLevel() noexcept;

// Returns the preferred vector register width in bytes.
[[nodiscard]] CYPHER_COMMON_API u32 Cy_SimdGetVectorRegisterBytes() noexcept;

// Returns a stable diagnostic name for a SIMD feature.
[[nodiscard]] CYPHER_COMMON_API const char *Cy_SimdFeatureName(
    cy_simd_feature_flags_t feature ) noexcept;

// Returns a stable diagnostic name for a SIMD level.
[[nodiscard]] CYPHER_COMMON_API const char *Cy_SimdLevelName(
    cy_simd_level_t level ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_SIMD_H
