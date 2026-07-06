//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CPUDetect.h
//  Purpose: Declares CypherCommon Tier0 CPUDetect support.
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

#ifndef CYPHER_COMMON_TIER0_CPUDETECT_H
#define CYPHER_COMMON_TIER0_CPUDETECT_H
#pragma once

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"

namespace cypher::common
{

constexpr usize CY_CPU_VENDOR_MAX = 32u;
constexpr usize CY_CPU_BRAND_MAX = 128u;

enum cy_cpu_vendor_t : u32 {
    CY_CPU_VENDOR_UNKNOWN = 0u,
    CY_CPU_VENDOR_INTEL,
    CY_CPU_VENDOR_AMD,
    CY_CPU_VENDOR_APPLE,
    CY_CPU_VENDOR_ARM,
    CY_CPU_VENDOR_QUALCOMM
};

enum cy_cpu_feature_flags_t : flags64_t {
    CY_CPU_FEATURE_NONE  = 0ull,

    CY_CPU_FEATURE_SSE2  = CYPHER_BIT64( 0 ),
    CY_CPU_FEATURE_SSE3  = CYPHER_BIT64( 1 ),
    CY_CPU_FEATURE_SSSE3 = CYPHER_BIT64( 2 ),
    CY_CPU_FEATURE_SSE41 = CYPHER_BIT64( 3 ),
    CY_CPU_FEATURE_SSE42 = CYPHER_BIT64( 4 ),

    CY_CPU_FEATURE_AVX   = CYPHER_BIT64( 5 ),
    CY_CPU_FEATURE_AVX2  = CYPHER_BIT64( 6 ),

    CY_CPU_FEATURE_NEON  = CYPHER_BIT64( 7 ),

    CY_CPU_FEATURE_AES   = CYPHER_BIT64( 8 ),
    CY_CPU_FEATURE_FMA   = CYPHER_BIT64( 9 ),
    CY_CPU_FEATURE_BMI1  = CYPHER_BIT64( 10 ),
    CY_CPU_FEATURE_BMI2  = CYPHER_BIT64( 11 ),
    CY_CPU_FEATURE_POPCNT = CYPHER_BIT64( 12 )
};

struct cy_cpu_detect_info_t {
    cy_cpu_vendor_t vendor;

    char szVendor[CY_CPU_VENDOR_MAX];
    char szBrand[CY_CPU_BRAND_MAX];

    u32 family;
    u32 model;
    u32 stepping;

    u32 logicalThreadCount;
    u32 physicalCoreCount;

    usize cacheLineSize;

    flags64_t hardwareFeatures;
    flags64_t usableFeatures;
};

// Initializes and caches CPU detection. Safe to call repeatedly.
bool_t Cy_CPUDetectInit();

// Clears cached CPU detection state during controlled shutdown.
void Cy_CPUDetectShutdown();

// Returns the cached CPU info snapshot, initializing on first use.
const cy_cpu_detect_info_t *Cy_CPUDetectGetInfo();

// Returns true when a feature bit exists in the given feature mask.
bool_t Cy_CPUDetectHasFeature( flags64_t features, cy_cpu_feature_flags_t feature );

// Returns a stable human-readable feature name.
const char *Cy_CPUDetectFeatureName( cy_cpu_feature_flags_t feature );

// Returns a stable human-readable CPU vendor name.
const char *Cy_CPUDetectVendorName( cy_cpu_vendor_t vendor );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CPUDETECT_H
