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

#include <atomic>
#include <mutex>

namespace cypher::common
{

namespace
{

cy_simd_caps_t g_simdCaps = {};
std::mutex g_simdMutex;
std::atomic_bool g_simdInitialized = false;

void SimdAddFeature( flags64_t &features, cy_simd_feature_flags_t feature, bool_t enabled )
{
    if ( enabled ) {
        features |= static_cast<flags64_t>( feature );
    }
}

flags64_t SimdConvertCpuFeatures( flags64_t cpuFeatures )
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

flags64_t SimdDetectCompiledFeatures()
{
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

void SimdSelectBestLevel( cy_simd_caps_t &caps )
{
    caps.bestLevel = CY_SIMD_LEVEL_SCALAR;
    caps.vectorRegisterBytes = CY_SIMD_SCALAR_REGISTER_BYTES;

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

} // namespace

bool_t Cy_SimdInit()
{
    if ( g_simdInitialized.load( std::memory_order_acquire ) ) {
        return CY_TRUE;
    }

    std::lock_guard<std::mutex> lock( g_simdMutex );

    if ( g_simdInitialized.load( std::memory_order_relaxed ) ) {
        return CY_TRUE;
    }

    const cy_cpu_detect_info_t *pCpu = Cy_CPUDetectGetInfo();

    g_simdCaps = {};
    g_simdCaps.cpuFeatures = SimdConvertCpuFeatures( pCpu->usableFeatures );
    g_simdCaps.compiledFeatures = SimdDetectCompiledFeatures();
    g_simdCaps.usableFeatures = g_simdCaps.cpuFeatures & g_simdCaps.compiledFeatures;

    SimdSelectBestLevel( g_simdCaps );

    g_simdInitialized.store( true, std::memory_order_release );
    return CY_TRUE;
}

void Cy_SimdShutdown()
{
    std::lock_guard<std::mutex> lock( g_simdMutex );
    g_simdCaps = {};
    g_simdInitialized.store( false, std::memory_order_release );
}

const cy_simd_caps_t *Cy_SimdGetCaps()
{
    if ( !g_simdInitialized.load( std::memory_order_acquire ) ) {
        Cy_SimdInit();
    }
    return &g_simdCaps;
}

bool_t Cy_SimdHasFeature( flags64_t features, cy_simd_feature_flags_t feature )
{
    return ( features & static_cast<flags64_t>( feature ) ) != 0u;
}

bool_t Cy_SimdCanUse( cy_simd_feature_flags_t feature )
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    return Cy_SimdHasFeature( pCaps->usableFeatures, feature );
}

cy_simd_level_t Cy_SimdGetBestLevel()
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    return pCaps->bestLevel;
}

u32 Cy_SimdGetVectorRegisterBytes()
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    return pCaps->vectorRegisterBytes;
}

const char *Cy_SimdFeatureName( cy_simd_feature_flags_t feature )
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

const char *Cy_SimdLevelName( cy_simd_level_t level )
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
