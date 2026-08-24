//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CPUDetect.h
//  Purpose: Declares the process-wide CPU identity and feature snapshot.
//  Details: Hardware features and OS-usable features are kept separate. A CPU may
//           advertise AVX while the operating system has not enabled the register
//           state needed to execute AVX instructions safely.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_CPUDETECT_H
#define CYPHER_COMMON_TIER0_CPUDETECT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"

namespace cypher::common
{

constexpr usize CY_CPU_VENDOR_MAX = 32u; // CPUID vendor plus the null terminator.
constexpr usize CY_CPU_BRAND_MAX = 128u; // Human-readable processor description.

enum cy_cpu_vendor_t : u32 {
    CY_CPU_VENDOR_UNKNOWN = 0u, // Vendor could not be identified reliably.
    CY_CPU_VENDOR_INTEL,        // GenuineIntel CPUID vendor string.
    CY_CPU_VENDOR_AMD,          // AuthenticAMD CPUID vendor string.
    CY_CPU_VENDOR_APPLE,        // Apple Silicon host.
    CY_CPU_VENDOR_ARM,          // Generic ARM host without a narrower vendor.
    CY_CPU_VENDOR_QUALCOMM      // Qualcomm ARM host.
};

// Feature bits are stable Cypher values; they are not raw CPUID/HWCAP bits.
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
    cy_cpu_vendor_t vendor;                    // Normalized processor vendor.

    char szVendor[CY_CPU_VENDOR_MAX];           // Raw or synthesized vendor name.
    char szBrand[CY_CPU_BRAND_MAX];             // User-facing model/brand string.

    u32 family;                                 // x86 family; zero on other ISAs.
    u32 model;                                  // x86 model; zero on other ISAs.
    u32 stepping;                               // x86 stepping; zero on other ISAs.

    u32 logicalThreadCount;                     // Scheduler-visible hardware threads.
    u32 physicalCoreCount;                      // Best available physical-core count.

    usize cacheLineSize;                        // L1 data cache line size in bytes.

    flags64_t hardwareFeatures;                 // Features reported by the processor.
    flags64_t usableFeatures;                   // Features safe to execute in this OS.
};

// Initializes the immutable process-lifetime CPU snapshot.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_CPUDetectInit() noexcept;

// Returns the immutable CPU snapshot, initializing it on first use.
CYPHER_NODISCARD CYPHER_COMMON_API const cy_cpu_detect_info_t *Cy_CPUDetectGetInfo() noexcept;

// Returns true when a feature bit exists in the given feature mask.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_CPUDetectHasFeature(
    flags64_t features,
    cy_cpu_feature_flags_t feature ) noexcept;

// Returns a stable human-readable feature name.
CYPHER_NODISCARD CYPHER_COMMON_API const char *Cy_CPUDetectFeatureName(
    cy_cpu_feature_flags_t feature ) noexcept;

// Returns a stable human-readable CPU vendor name.
CYPHER_NODISCARD CYPHER_COMMON_API const char *Cy_CPUDetectVendorName(
    cy_cpu_vendor_t vendor ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CPUDETECT_H
