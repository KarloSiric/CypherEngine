//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Simd.cpp
//  Purpose: Implements CypherCommon Tier0 SIMD capability policy.
//  Details: SIMD consumes CPUDetect facts and exposes the engine's safe SIMD
//           policy: compiled features, usable features, preferred level, and
//           vector register width. Actual SIMD math operations live elsewhere.
//
//  History:
//  - Created by Karlo Siric on 2026-07-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Simd.h"

#include "CypherCommon_CPUDetect.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

// CPUDetect reports hardware and operating-system state. This file translates
// those facts into the smaller SIMD vocabulary supported by engine algorithms.
namespace
{

void SimdAddFeature(
    flags64_t &features,
    cy_simd_feature_flags_t feature,
    bool_t enabled ) noexcept
{
    if ( enabled ) {
        features |= static_cast<flags64_t>( feature );
    }
}

flags64_t SimdConvertCpuFeatures( flags64_t cpuFeatures ) noexcept
{
    flags64_t simdFeatures = CY_SIMD_FEATURE_NONE;

    SimdAddFeature(
        simdFeatures,
        CY_SIMD_FEATURE_SSE2,
        Cy_CPUDetectHasFeature( cpuFeatures, CY_CPU_FEATURE_SSE2 )
    );

    SimdAddFeature(
        simdFeatures,
        CY_SIMD_FEATURE_SSE3,
        Cy_CPUDetectHasFeature( cpuFeatures, CY_CPU_FEATURE_SSE3 )
    );

    SimdAddFeature(
        simdFeatures,
        CY_SIMD_FEATURE_SSSE3,
        Cy_CPUDetectHasFeature( cpuFeatures, CY_CPU_FEATURE_SSSE3 )
    );

    SimdAddFeature(
        simdFeatures,
        CY_SIMD_FEATURE_SSE41,
        Cy_CPUDetectHasFeature( cpuFeatures, CY_CPU_FEATURE_SSE41 )
    );

    SimdAddFeature(
        simdFeatures,
        CY_SIMD_FEATURE_SSE42,
        Cy_CPUDetectHasFeature( cpuFeatures, CY_CPU_FEATURE_SSE42 )
    );

    SimdAddFeature(
        simdFeatures,
        CY_SIMD_FEATURE_AVX,
        Cy_CPUDetectHasFeature( cpuFeatures, CY_CPU_FEATURE_AVX )
    );

    SimdAddFeature(
        simdFeatures,
        CY_SIMD_FEATURE_AVX2,
        Cy_CPUDetectHasFeature( cpuFeatures, CY_CPU_FEATURE_AVX2 )
    );

    SimdAddFeature(
        simdFeatures,
        CY_SIMD_FEATURE_NEON,
        Cy_CPUDetectHasFeature( cpuFeatures, CY_CPU_FEATURE_NEON )
    );

    return simdFeatures;
}

flags64_t SimdDetectCompiledFeatures() noexcept
{
    // Compiler predefined macros describe which instruction families may appear
    // in this translation unit. Runtime CPUID alone cannot make absent code callable.
    flags64_t features = CY_SIMD_FEATURE_NONE;

#if CYPHER_ARCH_X64
    SimdAddFeature( features, CY_SIMD_FEATURE_SSE2, CY_TRUE );
#endif

#if defined( __SSE2__ ) || defined( _M_X64 ) || ( defined( _M_IX86_FP ) && _M_IX86_FP >= 2 )
    SimdAddFeature( features, CY_SIMD_FEATURE_SSE2, CY_TRUE );
#endif

#if defined( __SSE3__ )
    SimdAddFeature( features, CY_SIMD_FEATURE_SSE3, CY_TRUE );
#endif

#if defined( __SSSE3__ )
    SimdAddFeature( features, CY_SIMD_FEATURE_SSSE3, CY_TRUE );
#endif

#if defined( __SSE4_1__ )
    SimdAddFeature( features, CY_SIMD_FEATURE_SSE41, CY_TRUE );
#endif

#if defined( __SSE4_2__ )
    SimdAddFeature( features, CY_SIMD_FEATURE_SSE42, CY_TRUE );
#endif

#if defined( __AVX__ )
    SimdAddFeature( features, CY_SIMD_FEATURE_AVX, CY_TRUE );
#endif

#if defined( __AVX2__ )
    SimdAddFeature( features, CY_SIMD_FEATURE_AVX2, CY_TRUE );
#endif

#if defined( __ARM_NEON ) || defined( __ARM_NEON__ ) || defined( _M_ARM64 )
    SimdAddFeature( features, CY_SIMD_FEATURE_NEON, CY_TRUE );
#endif

    return features;
}

void SimdSelectBestLevel( cy_simd_caps_t &caps ) noexcept
{
    caps.bestLevel = CY_SIMD_LEVEL_SCALAR;
    caps.vectorRegisterBytes = CY_SIMD_SCALAR_REGISTER_BYTES;

    // Prefer the widest x86 implementation, then older x86 levels, then ARM NEON.
    // Only one ISA family is expected in a single target binary.
    if ( Cy_SimdHasFeature( caps.usableFeatures, CY_SIMD_FEATURE_AVX2 ) ) {
        caps.bestLevel = CY_SIMD_LEVEL_AVX2;
        caps.vectorRegisterBytes = CY_SIMD_256_REGISTER_BYTES;
    } else if ( Cy_SimdHasFeature( caps.usableFeatures, CY_SIMD_FEATURE_AVX ) ) {
        caps.bestLevel = CY_SIMD_LEVEL_AVX;
        caps.vectorRegisterBytes = CY_SIMD_256_REGISTER_BYTES;
    } else if ( Cy_SimdHasFeature( caps.usableFeatures, CY_SIMD_FEATURE_SSE41 ) ) {
        caps.bestLevel = CY_SIMD_LEVEL_SSE41;
        caps.vectorRegisterBytes = CY_SIMD_128_REGISTER_BYTES;
    } else if ( Cy_SimdHasFeature( caps.usableFeatures, CY_SIMD_FEATURE_SSE2 ) ) {
        caps.bestLevel = CY_SIMD_LEVEL_SSE2;
        caps.vectorRegisterBytes = CY_SIMD_128_REGISTER_BYTES;
    } else if ( Cy_SimdHasFeature( caps.usableFeatures, CY_SIMD_FEATURE_NEON ) ) {
        caps.bestLevel = CY_SIMD_LEVEL_NEON;
        caps.vectorRegisterBytes = CY_SIMD_128_REGISTER_BYTES;
    }

    caps.vectorRegisterBits = caps.vectorRegisterBytes * 8u;
}

cy_simd_caps_t SimdBuildCaps() noexcept
{
    const cy_cpu_detect_info_t *pCpu = Cy_CPUDetectGetInfo();

    cy_simd_caps_t caps = {};
    caps.cpuFeatures = SimdConvertCpuFeatures( pCpu->usableFeatures );
    caps.compiledFeatures = SimdDetectCompiledFeatures();
    // Safe dispatch requires both halves: the host can execute the instructions
    // and this exact binary contains code compiled for them.
    caps.usableFeatures = caps.cpuFeatures & caps.compiledFeatures;
    SimdSelectBestLevel( caps );
    return caps;
}

const cy_simd_caps_t &SimdGetCachedCaps() noexcept
{
    // CPU and build capabilities do not change after process startup.
    static const cy_simd_caps_t caps = SimdBuildCaps();
    return caps;
}

} // namespace

bool_t Cy_SimdInit() noexcept
{
    CYPHER_UNUSED( SimdGetCachedCaps() );
    return CY_TRUE;
}

const cy_simd_caps_t *Cy_SimdGetCaps() noexcept
{
    return &SimdGetCachedCaps();
}

bool_t Cy_SimdHasFeature(
    flags64_t features,
    cy_simd_feature_flags_t feature ) noexcept
{
    const flags64_t featureMask = static_cast<flags64_t>( feature );
    return featureMask != 0u && ( features & featureMask ) == featureMask;
}

bool_t Cy_SimdCanUse( cy_simd_feature_flags_t feature ) noexcept
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    return Cy_SimdHasFeature( pCaps->usableFeatures, feature );
}

cy_simd_level_t Cy_SimdGetBestLevel() noexcept
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    return pCaps->bestLevel;
}

u32 Cy_SimdGetVectorRegisterBytes() noexcept
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    return pCaps->vectorRegisterBytes;
}

const char *Cy_SimdFeatureName( cy_simd_feature_flags_t feature ) noexcept
{
    switch ( feature ) {
        case CY_SIMD_FEATURE_NONE:
            return "none";
        case CY_SIMD_FEATURE_SSE2:
            return "sse2";
        case CY_SIMD_FEATURE_SSE3:
            return "sse3";
        case CY_SIMD_FEATURE_SSSE3:
            return "ssse3";
        case CY_SIMD_FEATURE_SSE41:
            return "sse4.1";
        case CY_SIMD_FEATURE_SSE42:
            return "sse4.2";
        case CY_SIMD_FEATURE_AVX:
            return "avx";
        case CY_SIMD_FEATURE_AVX2:
            return "avx2";
        case CY_SIMD_FEATURE_NEON:
            return "neon";
        default:
            return "unknown";
    }
}

const char *Cy_SimdLevelName( cy_simd_level_t level ) noexcept
{
    switch ( level ) {
        case CY_SIMD_LEVEL_SCALAR:
            return "scalar";
        case CY_SIMD_LEVEL_SSE2:
            return "sse2";
        case CY_SIMD_LEVEL_SSE41:
            return "sse4.1";
        case CY_SIMD_LEVEL_AVX:
            return "avx";
        case CY_SIMD_LEVEL_AVX2:
            return "avx2";
        case CY_SIMD_LEVEL_NEON:
            return "neon";
        default:
            return "unknown";
    }
}

} // namespace cypher::common
