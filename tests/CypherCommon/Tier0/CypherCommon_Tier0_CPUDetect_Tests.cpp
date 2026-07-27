//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_CPUDetect_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 CPU detection behavior.
//  Details: These tests validate CPUDetect invariants without depending on exact
//           machine-specific model names or CI runner topology.
//
//  History:
//  - Created by Karlo Siric on 2026-07-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CPUDetect.h"
#include "CypherCommon_Platform.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <thread>

using namespace cypher::common;

namespace
{

bool IsPowerOfTwoSize( usize nValue )
{
    return nValue != 0u && ( nValue & ( nValue - 1u ) ) == 0u;
}

} // namespace

TEST_CASE( "CPUDetect initializes and exposes a cached CPU snapshot", "[CypherCommon][Tier0][CPUDetect]" )
{
    REQUIRE( Cy_CPUDetectInit() );

    const cy_cpu_detect_info_t *pInfo = Cy_CPUDetectGetInfo();
    REQUIRE( pInfo != nullptr );

    REQUIRE( pInfo->szVendor[0] != '\0' );
    REQUIRE( pInfo->szBrand[0] != '\0' );
    REQUIRE( pInfo->logicalThreadCount >= 1u );
    REQUIRE( pInfo->physicalCoreCount >= 1u );
    REQUIRE( pInfo->cacheLineSize >= 1u );
    REQUIRE( IsPowerOfTwoSize( pInfo->cacheLineSize ) );
}

TEST_CASE( "CPUDetect usable features are a subset of hardware features", "[CypherCommon][Tier0][CPUDetect]" )
{
    const cy_cpu_detect_info_t *pInfo = Cy_CPUDetectGetInfo();
    REQUIRE( pInfo != nullptr );

    REQUIRE( ( pInfo->usableFeatures & ~pInfo->hardwareFeatures ) == 0u );
}

TEST_CASE( "CPUDetect exposes stable names for vendors and features", "[CypherCommon][Tier0][CPUDetect]" )
{
    REQUIRE( Cy_CPUDetectVendorName( CY_CPU_VENDOR_UNKNOWN )[0] != '\0' );
    REQUIRE( Cy_CPUDetectFeatureName( CY_CPU_FEATURE_SSE2 )[0] != '\0' );
    REQUIRE( Cy_CPUDetectFeatureName( static_cast<cy_cpu_feature_flags_t>( CYPHER_BIT64( 63 ) ) )[0] != '\0' );
}

TEST_CASE( "CPUDetect exposes expected baseline architecture features", "[CypherCommon][Tier0][CPUDetect]" )
{
    const cy_cpu_detect_info_t *pInfo = Cy_CPUDetectGetInfo();
    REQUIRE( pInfo != nullptr );

#if CYPHER_ARCH_X64
    REQUIRE( Cy_CPUDetectHasFeature( pInfo->hardwareFeatures, CY_CPU_FEATURE_SSE2 ) );
    REQUIRE( Cy_CPUDetectHasFeature( pInfo->usableFeatures, CY_CPU_FEATURE_SSE2 ) );
#endif

#if CYPHER_ARCH_ARM_FAMILY
    REQUIRE( Cy_CPUDetectHasFeature( pInfo->hardwareFeatures, CY_CPU_FEATURE_NEON ) );
    REQUIRE( Cy_CPUDetectHasFeature( pInfo->usableFeatures, CY_CPU_FEATURE_NEON ) );
#endif
}

TEST_CASE( "CPUDetect publishes one immutable snapshot across threads", "[CypherCommon][Tier0][CPUDetect]" )
{
    constexpr usize THREAD_COUNT = 16u;
    std::array<const cy_cpu_detect_info_t *, THREAD_COUNT> results = {};
    std::array<std::thread, THREAD_COUNT> threads;

    for ( usize i = 0u; i < THREAD_COUNT; ++i ) {
        threads[i] = std::thread( [&results, i]() {
            results[i] = Cy_CPUDetectGetInfo();
        } );
    }

    for ( std::thread &thread : threads ) {
        thread.join();
    }

    const cy_cpu_detect_info_t *pExpected = Cy_CPUDetectGetInfo();
    for ( const cy_cpu_detect_info_t *pResult : results ) {
        REQUIRE( pResult == pExpected );
    }
}

TEST_CASE( "CPUDetect feature queries require every requested bit", "[CypherCommon][Tier0][CPUDetect]" )
{
    const flags64_t features =
        static_cast<flags64_t>( CY_CPU_FEATURE_SSE2 ) |
        static_cast<flags64_t>( CY_CPU_FEATURE_SSE3 );
    const auto combined = static_cast<cy_cpu_feature_flags_t>(
        static_cast<flags64_t>( CY_CPU_FEATURE_SSE2 ) |
        static_cast<flags64_t>( CY_CPU_FEATURE_AVX ) );

    REQUIRE_FALSE( Cy_CPUDetectHasFeature( features, CY_CPU_FEATURE_NONE ) );
    REQUIRE( Cy_CPUDetectHasFeature( features, CY_CPU_FEATURE_SSE2 ) );
    REQUIRE_FALSE( Cy_CPUDetectHasFeature( features, combined ) );
}
