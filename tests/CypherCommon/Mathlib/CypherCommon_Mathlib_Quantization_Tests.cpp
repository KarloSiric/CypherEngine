//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_Quantization_Tests.cpp
//  Purpose: Tests fixed-point and binary quantization contracts.
//  Details: Covers overflow, signed rounding, endpoint mappings, circular angles,
//           bounded vectors, and smallest-three quaternion reconstruction.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cypher::math;
using Catch::Approx;

TEST_CASE( "fixed 16.16 conversion and arithmetic report overflow",
           "[CypherCommon][Mathlib][FixedPoint]" )
{
    fixed16_16_t a{};
    fixed16_16_t b{};
    fixed16_16_t result{};
    REQUIRE( Fixed16_16_TryFromF64( 1.5, &a ) );
    REQUIRE( Fixed16_16_TryFromF64( -2.25, &b ) );
    REQUIRE( Fixed16_16_ToF64( a ) == Approx( 1.5 ) );
    REQUIRE( Fixed16_16_ToF64( b ) == Approx( -2.25 ) );

    REQUIRE( Fixed16_16_TryMultiply( a, b, &result ) );
    REQUIRE( Fixed16_16_ToF64( result ) == Approx( -3.375 ) );
    REQUIRE( Fixed16_16_TryDivide( a, b, &result ) );
    REQUIRE( Fixed16_16_ToF64( result ) == Approx( -2.0 / 3.0 ).margin( 0.00002 ) );

    REQUIRE( Fixed16_16_TryFromI32( 32767, &result ) );
    REQUIRE_FALSE( Fixed16_16_TryFromI32( 32768, &result ) );
    REQUIRE( Fixed16_16_TryFromI32( -32768, &result ) );
    REQUIRE_FALSE( Fixed16_16_TryAdd(
        Fixed16_16_FromRaw( cypher::common::CY_I32_MAX ),
        CY_FIXED16_16_ONE, &result ) );
    REQUIRE_FALSE( Fixed16_16_TryDivide( a, CY_FIXED16_16_ZERO, &result ) );
}

TEST_CASE( "fixed 16.16 integer rounding is correct for negative values",
           "[CypherCommon][Mathlib][FixedPoint]" )
{
    fixed16_16_t value{};
    REQUIRE( Fixed16_16_TryFromF64( -1.25, &value ) );
    REQUIRE( Fixed16_16_FloorToI32( value ) == -2 );
    REQUIRE( Fixed16_16_CeilToI32( value ) == -1 );
    REQUIRE( Fixed16_16_RoundToI32( value ) == -1 );

    REQUIRE( Fixed16_16_TryFromF64( -1.5, &value ) );
    REQUIRE( Fixed16_16_RoundToI32( value ) == -2 );
    REQUIRE( Fixed16_16_TryFromF64( 1.5, &value ) );
    REQUIRE( Fixed16_16_RoundToI32( value ) == 2 );
}

TEST_CASE( "UNORM and range quantization preserve endpoints and error bounds",
           "[CypherCommon][Mathlib][Quantization]" )
{
    for ( u32 cBits : { 1u, 8u, 16u, 32u } ) {
        u32 code = 0u;
        f32 decoded = 0.0f;
        REQUIRE( Quantization_TryEncodeUnorm( 0.0f, cBits, &code ) );
        REQUIRE( code == 0u );
        REQUIRE( Quantization_TryDecodeUnorm( code, cBits, &decoded ) );
        REQUIRE( decoded == Approx( 0.0f ) );

        REQUIRE( Quantization_TryEncodeUnorm( 1.0f, cBits, &code ) );
        REQUIRE( code == Quantization_MaxCode( cBits ) );
        REQUIRE( Quantization_TryDecodeUnorm( code, cBits, &decoded ) );
        REQUIRE( decoded == Approx( 1.0f ) );
    }

    u32 code = 0u;
    f32 decoded = 0.0f;
    REQUIRE( Quantization_TryEncodeRange(
        17.25f, -100.0f, 100.0f, 12u, &code ) );
    REQUIRE( Quantization_TryDecodeRange(
        code, -100.0f, 100.0f, 12u, &decoded ) );
    const f32 maximumError = 100.0f / 4095.0f;
    REQUIRE( decoded == Approx( 17.25f ).margin( maximumError ) );
    REQUIRE_FALSE( Quantization_TryDecodeUnorm( 256u, 8u, &decoded ) );
}

TEST_CASE( "vector and circular angle quantization round-trip bounded values",
           "[CypherCommon][Mathlib][Quantization]" )
{
    const vec3_t minimum = Vec3_Make( -1024.0f, -128.0f, -32.0f );
    const vec3_t maximum = Vec3_Make( 1024.0f, 128.0f, 96.0f );
    const vec3_t source = Vec3_Make( 125.25f, -17.75f, 64.5f );
    quantized_vec3_t code{};
    vec3_t decoded{};
    REQUIRE( Quantization_TryEncodeVec3Range(
        source, minimum, maximum, 16u, &code ) );
    REQUIRE( Quantization_TryDecodeVec3Range(
        code, minimum, maximum, 16u, &decoded ) );
    REQUIRE( decoded.x == Approx( source.x ).margin( 0.04f ) );
    REQUIRE( decoded.y == Approx( source.y ).margin( 0.01f ) );
    REQUIRE( decoded.z == Approx( source.z ).margin( 0.01f ) );

    code = { 1u, 2u, 3u };
    REQUIRE_FALSE( Quantization_TryEncodeVec3Range(
        source, minimum, Vec3_Make( 1024.0f, -128.0f, 96.0f ),
        16u, &code ) );
    REQUIRE( code.x == 0u );
    REQUIRE( code.y == 0u );
    REQUIRE( code.z == 0u );

    decoded = CY_VEC3_ONE;
    REQUIRE_FALSE( Quantization_TryDecodeVec3Range(
        quantized_vec3_t{ 0u, 70000u, 0u }, minimum, maximum,
        16u, &decoded ) );
    REQUIRE( Vec3_EqualsExact( decoded, CY_VEC3_ZERO ) );

    u32 angleCode = 0u;
    angle_t angle{};
    REQUIRE( Quantization_TryEncodeAngle(
        Angle_FromDegrees( -90.0f ), 12u, &angleCode ) );
    REQUIRE( angleCode == 3072u );
    REQUIRE( Quantization_TryDecodeAngle( angleCode, 12u, &angle ) );
    REQUIRE( Angle_Degrees( angle ) == Approx( 270.0f ).margin( 0.001f ) );

    REQUIRE( Quantization_TryEncodeAngle(
        Angle_FromDegrees( 359.99f ), 8u, &angleCode ) );
    REQUIRE( angleCode == 0u );
}

TEST_CASE( "smallest-three quaternion quantization preserves rotations",
           "[CypherCommon][Mathlib][Quantization][Quaternion]" )
{
    const quat_t rotations[]{
        CY_QUAT_IDENTITY,
        Quat_FromEulerXYZ( Vec3_Make( 0.2f, -0.7f, 1.1f ) ),
        Quat_FromUnitAxisAngle(
            Vec3_NormalizeUnchecked( Vec3_Make( 1.0f, 2.0f, 3.0f ) ),
            Angle_FromDegrees( 179.0f ) ),
        Quat_Negate( Quat_FromEulerXYZ(
            Vec3_Make( -1.0f, 0.3f, 2.4f ) ) )
    };

    for ( quat_t rotation : rotations ) {
        quantized_quat_t code{};
        quat_t decoded{};
        REQUIRE( Quantization_TryEncodeQuatSmallestThree(
            rotation, 12u, 0.000001f, &code ) );
        REQUIRE( code.largestComponent < 4u );
        REQUIRE( Quantization_TryDecodeQuatSmallestThree(
            code, 12u, 0.000001f, &decoded ) );
        REQUIRE( Quat_IsUnit( decoded, 0.00001f ) );
        REQUIRE( Quat_RotationEquivalent( rotation, decoded, 0.002f ) );
    }
}
