//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_ScalarVector_Tests.cpp
//  Purpose: Tests scalar, angle, and vector numerical contracts.
//  Details: Coverage includes wrapping boundaries, extreme magnitudes, checked
//           failure outputs, products, projections, and homogeneous division.
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

#include <cmath>
#include <limits>

using namespace cypher::math;
using Catch::Approx;

namespace
{

void RequireVec2( vec2_t value, f32 x, f32 y, f32 margin = 0.00001f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
}

void RequireVec3(
    vec3_t value,
    f32 x,
    f32 y,
    f32 z,
    f32 margin = 0.00001f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
}

void RequireVec2d( vec2d_t value, f64 x, f64 y, f64 epsilon = 1.0e-14 )
{
    REQUIRE( value.x == Approx( x ).epsilon( epsilon ) );
    REQUIRE( value.y == Approx( y ).epsilon( epsilon ) );
}

void RequireVec3d(
    vec3d_t value,
    f64 x,
    f64 y,
    f64 z,
    f64 epsilon = 1.0e-14 )
{
    REQUIRE( value.x == Approx( x ).epsilon( epsilon ) );
    REQUIRE( value.y == Approx( y ).epsilon( epsilon ) );
    REQUIRE( value.z == Approx( z ).epsilon( epsilon ) );
}

} // namespace

TEST_CASE( "scalar constants and wrapping preserve explicit angle policy",
           "[CypherCommon][Mathlib][Scalar][Angle]" )
{
    REQUIRE( Scalar_DegreesToRadians( 180.0f ) == Approx( CY_PI_F ) );
    REQUIRE( Scalar_RadiansToDegrees( CY_HALF_PI_F ) == Approx( 90.0f ) );
    REQUIRE( Scalar_WrapRadiansPositive( -CY_HALF_PI_F ) ==
             Approx( CY_TAU_F - CY_HALF_PI_F ) );
    REQUIRE( Scalar_WrapRadiansSigned( 3.0f * CY_PI_F ) ==
             Approx( -CY_PI_F ) );

    const angle_t from = Angle_FromDegrees( 350.0f );
    const angle_t to = Angle_FromDegrees( 10.0f );
    REQUIRE( Angle_Degrees( Angle_ShortestDelta( from, to ) ) ==
             Approx( 20.0f ).margin( 0.0001f ) );
    REQUIRE( Angle_NearlyEquivalent(
        Angle_LerpShortest( from, to, 0.5f ),
        CY_ANGLE_ZERO,
        Scalar_DegreesToRadians( 0.0001f ) ) );
}

TEST_CASE( "scalar comparisons handle scale, infinities, and NaN",
           "[CypherCommon][Mathlib][Scalar]" )
{
    REQUIRE( Scalar_NearlyEquals( 1.0f, 1.00001f, 0.00002f, 0.0f ) );
    REQUIRE( Scalar_NearlyEquals( 1000000.0f, 1000001.0f, 0.0f, 0.000002f ) );
    REQUIRE_FALSE( Scalar_NearlyEquals( 1.0f, 1.1f, 0.001f, 0.001f ) );

    const f32 infinity = std::numeric_limits<f32>::infinity();
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    REQUIRE_FALSE( Scalar_IsFinite( infinity ) );
    REQUIRE( Scalar_IsNan( nan ) );
    REQUIRE_FALSE( Scalar_NearlyEquals( nan, nan, 1.0f, 1.0f ) );
}

TEST_CASE( "vector arithmetic follows the engine axis convention",
           "[CypherCommon][Mathlib][Vector3]" )
{
    RequireVec3( Vec3_Cross( CY_VEC3_FORWARD, CY_VEC3_LEFT ), 0.0f, 0.0f, 1.0f );
    REQUIRE( Vec3_Dot( CY_VEC3_FORWARD, CY_VEC3_LEFT ) == 0.0f );
    RequireVec3(
        Vec3_ReflectUnitNormal( Vec3_Make( 1.0f, 0.0f, -1.0f ), CY_VEC3_UP ),
        1.0f, 0.0f, 1.0f );

    vec3_t projection{};
    REQUIRE( Vec3_TryProjectOnto(
        Vec3_Make( 2.0f, 3.0f, 4.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        0.000001f, &projection ) );
    RequireVec3( projection, 2.0f, 0.0f, 0.0f );
}

TEST_CASE( "checked normalization remains stable at extreme magnitudes",
           "[CypherCommon][Mathlib][Vector]" )
{
    vec3_t normalized{};
    f32 originalLength = 0.0f;
    REQUIRE( Vec3_TryNormalize(
        Vec3_Make( 3.0e30f, 4.0e30f, 0.0f ),
        0.0f, &normalized, &originalLength ) );
    RequireVec3( normalized, 0.6f, 0.8f, 0.0f );
    REQUIRE( originalLength == Approx( 5.0e30f ).epsilon( 0.00001f ) );

    normalized = CY_VEC3_ONE;
    originalLength = 12.0f;
    REQUIRE_FALSE( Vec3_TryNormalize(
        Vec3_Make( 3.0e-30f, 4.0e-30f, 0.0f ),
        1.0e-20f, &normalized, &originalLength ) );
    REQUIRE( Vec3_EqualsExact( normalized, CY_VEC3_ZERO ) );
    REQUIRE( originalLength == Approx( 5.0e-30f ).epsilon( 0.00001f ) );
}

TEST_CASE( "vector basis construction produces an orthonormal frame",
           "[CypherCommon][Mathlib][Vector3]" )
{
    vec3_t tangent{};
    vec3_t bitangent{};
    Vec3_BuildOrthonormalBasis( CY_VEC3_UP, &tangent, &bitangent );
    REQUIRE( Vec3_IsUnitLength( tangent, 0.00001f ) );
    REQUIRE( Vec3_IsUnitLength( bitangent, 0.00001f ) );
    REQUIRE( Vec3_Dot( tangent, CY_VEC3_UP ) == Approx( 0.0f ).margin( 0.00001f ) );
    REQUIRE( Vec3_Dot( bitangent, CY_VEC3_UP ) == Approx( 0.0f ).margin( 0.00001f ) );
    REQUIRE( Vec3_Dot( tangent, bitangent ) == Approx( 0.0f ).margin( 0.00001f ) );
}

TEST_CASE( "Vector2 planar products and perpendiculars use counter-clockwise sign",
           "[CypherCommon][Mathlib][Vector2]" )
{
    const vec2_t x = Vec2_Make( 1.0f, 0.0f );
    const vec2_t y = Vec2_Make( 0.0f, 1.0f );
    REQUIRE( Vec2_Cross( x, y ) == 1.0f );
    RequireVec2( Vec2_PerpendicularCCW( x ), 0.0f, 1.0f );
    RequireVec2( Vec2_PerpendicularCW( x ), 0.0f, -1.0f );
}

TEST_CASE( "binary64 vector finite checks and equality reject invalid data",
           "[CypherCommon][Mathlib][Vector][Binary64]" )
{
    const f64 infinity = std::numeric_limits<f64>::infinity();
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();
    const f64 maximum = std::numeric_limits<f64>::max();

    REQUIRE( Vec2d_IsFinite( Vec2d_Make( maximum, -maximum ) ) );
    REQUIRE_FALSE( Vec2d_IsFinite( Vec2d_Make( nan, 0.0 ) ) );
    REQUIRE_FALSE( Vec2d_IsFinite( Vec2d_Make( 0.0, infinity ) ) );
    REQUIRE_FALSE( Vec2d_IsFinite( Vec2d_Make( -infinity, 0.0 ) ) );

    REQUIRE( Vec3d_IsFinite( Vec3d_Make( maximum, 0.0, -maximum ) ) );
    REQUIRE_FALSE( Vec3d_IsFinite( Vec3d_Make( nan, 0.0, 0.0 ) ) );
    REQUIRE_FALSE( Vec3d_IsFinite( Vec3d_Make( 0.0, infinity, 0.0 ) ) );
    REQUIRE_FALSE( Vec3d_IsFinite( Vec3d_Make( 0.0, 0.0, -infinity ) ) );

    REQUIRE( Vec2d_EqualsExact( Vec2d_Make( 0.0, -0.0 ), CY_VEC2D_ZERO ) );
    REQUIRE( Vec3d_EqualsExact(
        Vec3d_Make( 1.0, 2.0, 3.0 ), Vec3d_Make( 1.0, 2.0, 3.0 ) ) );
    REQUIRE_FALSE( Vec3d_EqualsExact(
        Vec3d_Make( 1.0, 2.0, 3.0 ),
        Vec3d_Make( 1.0, 2.0, std::nextafter( 3.0, infinity ) ) ) );
    REQUIRE_FALSE( Vec3d_EqualsExact(
        Vec3d_Make( nan, 2.0, 3.0 ), Vec3d_Make( nan, 2.0, 3.0 ) ) );
}

TEST_CASE( "binary64 checked lengths survive extreme finite magnitudes",
           "[CypherCommon][Mathlib][Vector][Binary64][Length]" )
{
    f64 length = -1.0;
    REQUIRE( Vec2d_TryLength( CY_VEC2D_ZERO, &length ) );
    REQUIRE( length == 0.0 );

    REQUIRE( Vec2d_TryLength( Vec2d_Make( 3.0e300, 4.0e300 ), &length ) );
    REQUIRE( length / 1.0e300 == Approx( 5.0 ).epsilon( 1.0e-14 ) );
    REQUIRE( Vec2d_TryLength( Vec2d_Make( 3.0e-300, 4.0e-300 ), &length ) );
    REQUIRE( length / 1.0e-300 == Approx( 5.0 ).epsilon( 1.0e-14 ) );

    REQUIRE( Vec3d_TryLength( Vec3d_Make( 2.0e300, 3.0e300, 6.0e300 ), &length ) );
    REQUIRE( length / 1.0e300 == Approx( 7.0 ).epsilon( 1.0e-14 ) );
    REQUIRE( Vec3d_TryLength( Vec3d_Make( 2.0e-300, 3.0e-300, 6.0e-300 ), &length ) );
    REQUIRE( length / 1.0e-300 == Approx( 7.0 ).epsilon( 1.0e-14 ) );

    const f64 subnormal = std::numeric_limits<f64>::denorm_min();
    REQUIRE( Vec2d_TryLength(
        Vec2d_Make( 3.0 * subnormal, 4.0 * subnormal ), &length ) );
    REQUIRE( length == 5.0 * subnormal );
    REQUIRE( Vec3d_TryLength(
        Vec3d_Make( subnormal, 0.0, 0.0 ), &length ) );
    REQUIRE( length == subnormal );

    const f64 maximum = std::numeric_limits<f64>::max();
    REQUIRE( Vec3d_TryLength( Vec3d_Make( maximum, 0.0, 0.0 ), &length ) );
    REQUIRE( length == maximum );

    length = 12.0;
    REQUIRE_FALSE( Vec2d_TryLength(
        Vec2d_Make( maximum, maximum ),
        &length ) );
    REQUIRE( length == 0.0 );

    length = 12.0;
    REQUIRE_FALSE( Vec3d_TryLength(
        Vec3d_Make( std::numeric_limits<f64>::infinity(), 0.0, 0.0 ),
        &length ) );
    REQUIRE( length == 0.0 );
}

TEST_CASE( "binary64 checked normalization has deterministic failure outputs",
           "[CypherCommon][Mathlib][Vector][Binary64][Normalize]" )
{
    vec2d_t normalized2 = CY_VEC2D_ONE;
    f64 originalLength = -1.0;
    REQUIRE( Vec2d_TryNormalize(
        Vec2d_Make( -3.0e300, 4.0e300 ), 0.0,
        &normalized2, &originalLength ) );
    RequireVec2d( normalized2, -0.6, 0.8 );
    REQUIRE( originalLength / 1.0e300 == Approx( 5.0 ).epsilon( 1.0e-14 ) );

    REQUIRE( Vec2d_TryNormalize(
        Vec2d_Make( 3.0e-300, 4.0e-300 ), 0.0,
        &normalized2, nullptr ) );
    RequireVec2d( normalized2, 0.6, 0.8 );

    const f64 subnormal = std::numeric_limits<f64>::denorm_min();
    REQUIRE( Vec2d_TryNormalize(
        Vec2d_Make( 3.0 * subnormal, 4.0 * subnormal ), 0.0,
        &normalized2, &originalLength ) );
    RequireVec2d( normalized2, 0.6, 0.8 );
    REQUIRE( originalLength == 5.0 * subnormal );

    vec3d_t normalized3 = CY_VEC3D_ONE;
    REQUIRE( Vec3d_TryNormalize(
        Vec3d_Make( 2.0e300, 3.0e300, 6.0e300 ), 0.0,
        &normalized3, &originalLength ) );
    RequireVec3d( normalized3, 2.0 / 7.0, 3.0 / 7.0, 6.0 / 7.0 );
    REQUIRE( Vec3d_Dot( normalized3, normalized3 ) ==
             Approx( 1.0 ).epsilon( 1.0e-14 ) );
    REQUIRE( originalLength / 1.0e300 == Approx( 7.0 ).epsilon( 1.0e-14 ) );

    REQUIRE( Vec3d_TryNormalize(
        Vec3d_Make( 2.0e-300, -3.0e-300, 6.0e-300 ), 0.0,
        &normalized3, nullptr ) );
    RequireVec3d( normalized3, 2.0 / 7.0, -3.0 / 7.0, 6.0 / 7.0 );

    normalized3 = CY_VEC3D_ONE;
    originalLength = -1.0;
    REQUIRE_FALSE( Vec3d_TryNormalize(
        CY_VEC3D_ZERO, 0.0, &normalized3, &originalLength ) );
    REQUIRE( Vec3d_EqualsExact( normalized3, CY_VEC3D_ZERO ) );
    REQUIRE( originalLength == 0.0 );

    normalized3 = CY_VEC3D_ONE;
    originalLength = -1.0;
    REQUIRE_FALSE( Vec3d_TryNormalize(
        Vec3d_Make( 3.0, 4.0, 0.0 ), 5.0,
        &normalized3, &originalLength ) );
    REQUIRE( Vec3d_EqualsExact( normalized3, CY_VEC3D_ZERO ) );
    REQUIRE( originalLength == Approx( 5.0 ) );

    REQUIRE( Vec3d_TryNormalize(
        Vec3d_Make( 3.0, 4.0, 0.0 ), std::nextafter( 5.0, 0.0 ),
        &normalized3, &originalLength ) );
    RequireVec3d( normalized3, 0.6, 0.8, 0.0 );
    REQUIRE( originalLength == Approx( 5.0 ) );

    normalized3 = CY_VEC3D_ONE;
    REQUIRE_FALSE( Vec3d_TryNormalize(
        Vec3d_Make( 3.0, 4.0, 0.0 ),
        std::nextafter( 5.0, std::numeric_limits<f64>::infinity() ),
        &normalized3, &originalLength ) );
    REQUIRE( Vec3d_EqualsExact( normalized3, CY_VEC3D_ZERO ) );
    REQUIRE( originalLength == Approx( 5.0 ) );

    normalized3 = CY_VEC3D_ONE;
    originalLength = 12.0;
    REQUIRE_FALSE( Vec3d_TryNormalize(
        Vec3d_Make( std::numeric_limits<f64>::max(),
                    std::numeric_limits<f64>::max(), 0.0 ),
        0.0, &normalized3, &originalLength ) );
    REQUIRE( Vec3d_EqualsExact( normalized3, CY_VEC3D_ZERO ) );
    REQUIRE( originalLength == 0.0 );

    normalized2 = CY_VEC2D_ONE;
    originalLength = 12.0;
    REQUIRE_FALSE( Vec2d_TryNormalize(
        Vec2d_Make( std::numeric_limits<f64>::quiet_NaN(), 1.0 ), 0.0,
        &normalized2, &originalLength ) );
    REQUIRE( Vec2d_EqualsExact( normalized2, CY_VEC2D_ZERO ) );
    REQUIRE( originalLength == 0.0 );

    normalized3 = CY_VEC3D_ONE;
    originalLength = 12.0;
    REQUIRE_FALSE( Vec3d_TryNormalize(
        Vec3d_Make( 1.0, std::numeric_limits<f64>::infinity(), 0.0 ), 0.0,
        &normalized3, &originalLength ) );
    REQUIRE( Vec3d_EqualsExact( normalized3, CY_VEC3D_ZERO ) );
    REQUIRE( originalLength == 0.0 );
}

TEST_CASE( "Vector4 perspective divide succeeds and fails deterministically",
           "[CypherCommon][Mathlib][Vector4]" )
{
    vec3_t result = CY_VEC3_ONE;
    REQUIRE( Vec4_TryPerspectiveDivide(
        Vec4_Make( 4.0f, 6.0f, 8.0f, 2.0f ), 0.00001f, &result ) );
    RequireVec3( result, 2.0f, 3.0f, 4.0f );

    result = CY_VEC3_ONE;
    REQUIRE_FALSE( Vec4_TryPerspectiveDivide(
        Vec4_Make( 1.0f, 2.0f, 3.0f, 0.0f ), 0.00001f, &result ) );
    REQUIRE( Vec3_EqualsExact( result, CY_VEC3_ZERO ) );
}
