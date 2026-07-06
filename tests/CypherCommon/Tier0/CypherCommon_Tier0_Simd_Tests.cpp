//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Simd_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 SIMD capability policy.
//  Details: These tests validate SIMD policy invariants without depending on exact
//           CI runner hardware. CPU feature detection belongs to CPUDetect; SIMD
//           chooses the safe engine-level capability set.
//
//  History:
//  - Created by Karlo Siric on 2026-07-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Simd.h"
#include "CypherCommon_Platform.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

bool_t SimdLevelHasExpectedWidth( cy_simd_level_t level, u32 nBytes )
{
    switch ( level ) {
        case CY_SIMD_LEVEL_SCALAR:
            return nBytes == CY_SIMD_SCALAR_REGISTER_BYTES;
        case CY_SIMD_LEVEL_SSE2:
        case CY_SIMD_LEVEL_SSE41:
        case CY_SIMD_LEVEL_NEON:
            return nBytes == CY_SIMD_128_REGISTER_BYTES;
        case CY_SIMD_LEVEL_AVX:
        case CY_SIMD_LEVEL_AVX2:
            return nBytes == CY_SIMD_256_REGISTER_BYTES;
        default:
            return CY_FALSE;
    }
}

} // namespace

TEST_CASE( "SIMD initializes and exposes cached capabilities", "[CypherCommon][Tier0][SIMD]" )
{
    REQUIRE( Cy_SimdInit() );

    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    REQUIRE( pCaps != nullptr );

    REQUIRE( ( pCaps->usableFeatures & ~pCaps->cpuFeatures ) == 0u );
    REQUIRE( ( pCaps->usableFeatures & ~pCaps->compiledFeatures ) == 0u );
    REQUIRE( pCaps->vectorRegisterBits == pCaps->vectorRegisterBytes * 8u );
}

TEST_CASE( "SIMD best level has a coherent vector width", "[CypherCommon][Tier0][SIMD]" )
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    REQUIRE( pCaps != nullptr );

    REQUIRE( SimdLevelHasExpectedWidth( pCaps->bestLevel, pCaps->vectorRegisterBytes ) );
    REQUIRE( Cy_SimdGetBestLevel() == pCaps->bestLevel );
    REQUIRE( Cy_SimdGetVectorRegisterBytes() == pCaps->vectorRegisterBytes );
    REQUIRE( Cy_SimdLevelName( pCaps->bestLevel )[0] != '\0' );
}

TEST_CASE( "SIMD CanUse mirrors the usable feature mask", "[CypherCommon][Tier0][SIMD]" )
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    REQUIRE( pCaps != nullptr );

    const cy_simd_feature_flags_t features[] = {
        CY_SIMD_FEATURE_SSE2,
        CY_SIMD_FEATURE_SSE3,
        CY_SIMD_FEATURE_SSSE3,
        CY_SIMD_FEATURE_SSE41,
        CY_SIMD_FEATURE_SSE42,
        CY_SIMD_FEATURE_AVX,
        CY_SIMD_FEATURE_AVX2,
        CY_SIMD_FEATURE_NEON
    };

    for ( cy_simd_feature_flags_t feature : features ) {
        REQUIRE( Cy_SimdCanUse( feature ) == Cy_SimdHasFeature( pCaps->usableFeatures, feature ) );
        REQUIRE( Cy_SimdFeatureName( feature )[0] != '\0' );
    }
}

TEST_CASE( "SIMD compiled feature baseline matches target architecture", "[CypherCommon][Tier0][SIMD]" )
{
    const cy_simd_caps_t *pCaps = Cy_SimdGetCaps();
    REQUIRE( pCaps != nullptr );

#if CYPHER_ARCH_X64
    REQUIRE( Cy_SimdHasFeature( pCaps->compiledFeatures, CY_SIMD_FEATURE_SSE2 ) );
#endif

#if CYPHER_ARCH_ARM64
    REQUIRE( Cy_SimdHasFeature( pCaps->compiledFeatures, CY_SIMD_FEATURE_NEON ) );
#endif
}
