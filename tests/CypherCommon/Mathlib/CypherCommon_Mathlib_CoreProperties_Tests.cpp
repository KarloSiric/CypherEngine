//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_CoreProperties_Tests.cpp
//  Purpose: Tests core scalar, vector, quaternion, and matrix invariants.
//  Details: Deterministic property grids protect periodicity, decomposition,
//           inverse identities, interpolation endpoints, and failure outputs.
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

namespace
{

void RequireVec3Near( vec3_t actual, vec3_t expected, f32 margin = 0.00005f )
{
    REQUIRE( actual.x == Approx( expected.x ).margin( margin ) );
    REQUIRE( actual.y == Approx( expected.y ).margin( margin ) );
    REQUIRE( actual.z == Approx( expected.z ).margin( margin ) );
}

} // namespace

TEST_CASE( "Scalar periodic and interpolation helpers preserve their contracts",
           "[CypherCommon][Mathlib][Scalar][Property]" )
{
    constexpr f32 values[]{ -123.75f, -14.5f, -0.25f, 0.0f, 7.0f, 91.125f };
    for ( f32 value : values ) {
        const f32 repeated = Scalar_Repeat( value, 7.25f );
        CAPTURE( value, repeated );
        REQUIRE( repeated >= 0.0f );
        REQUIRE( repeated < 7.25f );
        REQUIRE( Scalar_Repeat( value + 29.0f, 7.25f ) ==
                 Approx( repeated ).margin( 0.00002f ) );
    }

    REQUIRE( Scalar_InverseLerp( 10.0f, 20.0f, 15.0f ) == Approx( 0.5f ) );
    REQUIRE( Scalar_Remap( 15.0f, 10.0f, 20.0f, -4.0f, 8.0f ) ==
             Approx( 2.0f ) );
    REQUIRE( Scalar_SmoothStep( 0.0f, 1.0f, -1.0f ) == 0.0f );
    REQUIRE( Scalar_SmoothStep( 0.0f, 1.0f, 2.0f ) == 1.0f );
    REQUIRE( Scalar_SmootherStep( 0.0f, 1.0f, 0.5f ) == Approx( 0.5f ) );
    REQUIRE( Scalar_MoveTowards( 1.0f, 2.0f, 5.0f ) == 2.0f );
    REQUIRE( Scalar_MoveTowards( 2.0f, -2.0f, 1.25f ) == Approx( 0.75f ) );
}

TEST_CASE( "Angle normalization is invariant under complete turns",
           "[CypherCommon][Mathlib][Angle][Property]" )
{
    for ( int nTurns = -8; nTurns <= 8; ++nTurns ) {
        const angle_t source = Angle_FromDegrees(
            30.0f + 360.0f * static_cast<f32>( nTurns ) );
        CAPTURE( nTurns );
        REQUIRE( Angle_Degrees( Angle_NormalizePositive( source ) ) ==
                 Approx( 30.0f ).margin( 0.001f ) );
        REQUIRE( Angle_Degrees( Angle_NormalizeSigned( source ) ) ==
                 Approx( 30.0f ).margin( 0.001f ) );
    }

    f32 sine = 0.0f;
    f32 cosine = 0.0f;
    Angle_SinCos( Angle_FromDegrees( 135.0f ), &sine, &cosine );
    REQUIRE( sine == Approx( 0.70710678f ).margin( 0.000001f ) );
    REQUIRE( cosine == Approx( -0.70710678f ).margin( 0.000001f ) );
}

TEST_CASE( "Vector projection and rejection reconstruct the source",
           "[CypherCommon][Mathlib][Vector3][Property]" )
{
    constexpr vec3_t vectors[]{
        { 1.0f, 2.0f, 3.0f },
        { -4.0f, 0.5f, 9.0f },
        { 0.0001f, -0.25f, 100.0f },
        { -7.0f, -8.0f, -9.0f }
    };
    const vec3_t axis = Vec3_NormalizeUnchecked( Vec3_Make( 2.0f, -1.0f, 3.0f ) );

    for ( vec3_t value : vectors ) {
        const vec3_t projected = Vec3_ProjectOntoUnit( value, axis );
        const vec3_t rejected = Vec3_RejectFromUnit( value, axis );
        RequireVec3Near( Vec3_Add( projected, rejected ), value );
        REQUIRE( Vec3_Dot( rejected, axis ) ==
                 Approx( 0.0f ).margin( 0.00002f ) );

        vec3_t resized{};
        REQUIRE( Vec3_TrySetLength( value, 5.0f, 0.0f, &resized ) );
        REQUIRE( Vec3_Length( resized ) == Approx( 5.0f ).margin( 0.00001f ) );
    }
}

TEST_CASE( "Vector products obey orthogonality and antisymmetry",
           "[CypherCommon][Mathlib][Vector3][Property]" )
{
    constexpr vec3_t values[]{
        { 1.0f, 2.0f, 3.0f },
        { -2.0f, 5.0f, 0.5f },
        { 7.0f, -3.0f, 4.0f }
    };
    for ( vec3_t a : values ) {
        for ( vec3_t b : values ) {
            const vec3_t cross = Vec3_Cross( a, b );
            CAPTURE( a.x, a.y, a.z, b.x, b.y, b.z );
            REQUIRE( Vec3_Dot( cross, a ) == Approx( 0.0f ).margin( 0.0001f ) );
            REQUIRE( Vec3_Dot( cross, b ) == Approx( 0.0f ).margin( 0.0001f ) );
            RequireVec3Near( Vec3_Cross( b, a ), Vec3_Negate( cross ) );
        }
    }
}

TEST_CASE( "Checked vector operations reset outputs on degenerate input",
           "[CypherCommon][Mathlib][Vector][Failure]" )
{
    vec2_t normalized2 = CY_VEC2_ONE;
    f32 length2 = 17.0f;
    REQUIRE_FALSE( Vec2_TryNormalize(
        CY_VEC2_ZERO, 0.0f, &normalized2, &length2 ) );
    REQUIRE( Vec2_EqualsExact( normalized2, CY_VEC2_ZERO ) );
    REQUIRE( length2 == 0.0f );

    vec3_t resized = CY_VEC3_ONE;
    REQUIRE_FALSE( Vec3_TrySetLength(
        CY_VEC3_ZERO, 4.0f, 0.0f, &resized ) );
    REQUIRE( Vec3_EqualsExact( resized, CY_VEC3_ZERO ) );

    vec4_t normalized4 = CY_VEC4_ONE;
    f32 length4 = 17.0f;
    REQUIRE_FALSE( Vec4_TryNormalize(
        CY_VEC4_ZERO, 0.0f, &normalized4, &length4 ) );
    REQUIRE( Vec4_EqualsExact( normalized4, CY_VEC4_ZERO ) );
    REQUIRE( length4 == 0.0f );
}

TEST_CASE( "Quaternion inverses and from-to rotations preserve directions",
           "[CypherCommon][Mathlib][Quaternion][Property]" )
{
    const quat_t unit = Quat_FromEulerXYZ( Vec3_Make( 0.4f, -1.0f, 0.7f ) );
    const quat_t scaled = Quat_Scale( unit, 3.5f );
    quat_t inverse{};
    REQUIRE( Quat_TryInverse( scaled, 0.000001f, &inverse ) );
    REQUIRE( Quat_NearlyEquals(
        Quat_Multiply( scaled, inverse ), CY_QUAT_IDENTITY,
        0.00001f, 0.00001f ) );

    constexpr vec3_t fromValues[]{
        { 1.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        { 1.0f, 2.0f, 3.0f }
    };
    constexpr vec3_t toValues[]{
        { 1.0f, 0.0f, 0.0f },
        { -1.0f, 0.0f, 0.0f },
        { -3.0f, 4.0f, 1.0f }
    };
    for ( u32 i = 0u; i < 3u; ++i ) {
        quat_t rotation{};
        REQUIRE( Quat_TryFromToRotation(
            fromValues[i], toValues[i], 0.000001f, &rotation ) );
        const vec3_t from = Vec3_NormalizeUnchecked( fromValues[i] );
        const vec3_t to = Vec3_NormalizeUnchecked( toValues[i] );
        RequireVec3Near( Quat_RotateVectorUnit( rotation, from ), to, 0.0001f );
    }
}

TEST_CASE( "Matrix3 inversion and basis repair satisfy identity contracts",
           "[CypherCommon][Mathlib][Matrix3][Property]" )
{
    const mat3_t matrix = Mat3_FromRows(
        Vec3_Make( 2.0f, 0.0f, 1.0f ),
        Vec3_Make( 1.0f, 3.0f, 0.0f ),
        Vec3_Make( 0.0f, 2.0f, 4.0f ) );
    mat3_t inverse{};
    REQUIRE( Mat3_TryInverse( matrix, 0.000001f, &inverse ) );
    REQUIRE( Mat3_NearlyEquals(
        Mat3_Multiply( matrix, inverse ), CY_MAT3_IDENTITY,
        0.00002f, 0.00002f ) );
    REQUIRE( Mat3_NearlyEquals(
        Mat3_Transpose( Mat3_Transpose( matrix ) ), matrix, 0.0f, 0.0f ) );

    const mat3_t skewed = Mat3_FromColumns(
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 0.2f, 3.0f, 0.0f ),
        Vec3_Make( 0.5f, 0.3f, 4.0f ) );
    mat3_t repaired{};
    REQUIRE( Mat3_TryOrthonormalize( skewed, 0.000001f, &repaired ) );
    REQUIRE( Mat3_IsOrthonormal( repaired, 0.00001f ) );

    inverse = CY_MAT3_ZERO;
    REQUIRE_FALSE( Mat3_TryInverse( CY_MAT3_ZERO, 0.000001f, &inverse ) );
    REQUIRE( Mat3_NearlyEquals( inverse, CY_MAT3_IDENTITY, 0.0f, 0.0f ) );
}

TEST_CASE( "Infinite perspective projections preserve near and asymptotic depth",
           "[CypherCommon][Mathlib][Matrix4][Projection]" )
{
    for ( clip_depth_range_t depthRange : {
              clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
              clip_depth_range_t::ZERO_TO_ONE } ) {
        mat4_t projection{};
        REQUIRE( Mat4_TryPerspectiveInfiniteRH(
            Angle_FromDegrees( 70.0f ), 16.0f / 9.0f, 0.25f,
            depthRange, &projection ) );

        vec3_t nearPoint{};
        vec3_t distantPoint{};
        REQUIRE( Mat4_TryProjectPoint(
            projection, Vec3_Make( 0.0f, 0.0f, -0.25f ),
            0.000001f, &nearPoint ) );
        REQUIRE( Mat4_TryProjectPoint(
            projection, Vec3_Make( 0.0f, 0.0f, -1000000.0f ),
            0.000001f, &distantPoint ) );
        const f32 expectedNear =
            depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE
                ? -1.0f
                : 0.0f;
        REQUIRE( nearPoint.z == Approx( expectedNear ).margin( 0.00001f ) );
        REQUIRE( distantPoint.z == Approx( 1.0f ).margin( 0.00001f ) );
    }
}
