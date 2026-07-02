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
#pragma once

/*
================
CypherCommon SIMD

SIMD capability declarations. Math code can query this layer without scattering
compiler or CPU feature checks through the engine.
================
*/

#include "CypherCommon_Defines.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

enum simd_feature_flags_t : flags64_t {
    SIMD_FEATURE_NONE = 0ull,
    SIMD_FEATURE_SSE2 = CYPHER_BIT64( 0 ),
    SIMD_FEATURE_SSE3 = CYPHER_BIT64( 1 ),
    SIMD_FEATURE_SSSE3 = CYPHER_BIT64( 2 ),
    SIMD_FEATURE_SSE41 = CYPHER_BIT64( 3 ),
    SIMD_FEATURE_SSE42 = CYPHER_BIT64( 4 ),
    SIMD_FEATURE_AVX = CYPHER_BIT64( 5 ),
    SIMD_FEATURE_AVX2 = CYPHER_BIT64( 6 ),
    SIMD_FEATURE_NEON = CYPHER_BIT64( 7 )
};

struct simd_caps_t {
    flags64_t features;
    u32 vectorRegisterBytes;
};

simd_caps_t Cy_Simd_GetCaps();
bool_t Cy_Simd_HasFeature( flags64_t features, simd_feature_flags_t feature );
const char *Cy_Simd_FeatureName( simd_feature_flags_t feature );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_SIMD_H
