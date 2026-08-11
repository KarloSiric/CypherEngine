//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherMath_RotationMatrix_Tests.cpp
//  Purpose: Tests quaternion, matrix, camera, and projection conventions.
//  Details: Cross-module round trips pin handedness, composition order, storage,
//           inversion, view direction, and both supported clip-depth ranges.
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

void RequireVec3(
    vec3_t value,
    f32 x,
    f32 y,
    f32 z,
    f32 margin = 0.00005f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
}

} // namespace

TEST_CASE( "quaternion axis-angle rotation follows right-handed engine axes",
           "[CypherMath][Quaternion]" )
{
    const quat_t quarterTurn = Quat_FromUnitAxisAngle(
        CY_VEC3_UP, Angle_FromDegrees( 90.0f ) );
    RequireVec3(
        Quat_RotateVectorUnit( quarterTurn, CY_VEC3_FORWARD ),
        0.0f, 1.0f, 0.0f );
    RequireVec3(
        Quat_InverseRotateVectorUnit( quarterTurn, CY_VEC3_LEFT ),
        1.0f, 0.0f, 0.0f );
    REQUIRE( Quat_RotationEquivalent(
        quarterTurn, Quat_Negate( quarterTurn ), 0.00001f ) );
    REQUIRE_FALSE( Quat_RotationEquivalent(
        Quat_Make( 0.0f, 0.0f, 0.0f, 0.0f ),
        CY_QUAT_IDENTITY, CY_PI_F ) );
}

TEST_CASE( "quaternion multiplication applies the right operand first",
           "[CypherMath][Quaternion]" )
{
    const quat_t aroundZ = Quat_FromUnitAxisAngle(
        CY_VEC3_UP, Angle_FromDegrees( 90.0f ) );
    const quat_t aroundY = Quat_FromUnitAxisAngle(
        CY_VEC3_LEFT, Angle_FromDegrees( 90.0f ) );
    const quat_t composed = Quat_Multiply( aroundY, aroundZ );
    const vec3_t sequential = Quat_RotateVectorUnit(
        aroundY,
        Quat_RotateVectorUnit( aroundZ, CY_VEC3_FORWARD ) );
    const vec3_t direct = Quat_RotateVectorUnit( composed, CY_VEC3_FORWARD );
    RequireVec3( direct, sequential.x, sequential.y, sequential.z );
}

TEST_CASE( "quaternion and Matrix3 conversions preserve rotations",
           "[CypherMath][Quaternion][Matrix3]" )
{
    const quat_t source = Quat_FromEulerXYZ(
        Vec3_Make( 0.3f, -0.7f, 1.1f ) );
    const mat3_t matrix = Mat3_FromQuaternion( source );
    REQUIRE( Mat3_IsOrthonormal( matrix, 0.0001f ) );
    REQUIRE( Mat3_Determinant( matrix ) == Approx( 1.0f ).margin( 0.0001f ) );

    quat_t roundTrip{};
    REQUIRE( Mat3_TryToQuaternion( matrix, 0.000001f, &roundTrip ) );
    REQUIRE( Quat_RotationEquivalent( source, roundTrip, 0.0005f ) );
}

TEST_CASE( "Euler and matrix round trips hold over a representative angle grid",
           "[CypherMath][Quaternion][Matrix3][Property]" )
{
    constexpr f32 angles[]{ -1.2f, -0.5f, 0.0f, 0.5f, 1.2f };
    for ( f32 x : angles ) {
        for ( f32 y : angles ) {
            for ( f32 z : angles ) {
                const quat_t source = Quat_FromEulerXYZ( Vec3_Make( x, y, z ) );
                const quat_t eulerRoundTrip =
                    Quat_FromEulerXYZ( Quat_ToEulerXYZ( source ) );
                REQUIRE( Quat_RotationEquivalent(
                    source, eulerRoundTrip, 0.001f ) );

                quat_t matrixRoundTrip{};
                REQUIRE( Mat3_TryToQuaternion(
                    Mat3_FromQuaternion( source ),
                    0.000001f, &matrixRoundTrip ) );
                CAPTURE(
                    x, y, z,
                    source.x, source.y, source.z, source.w,
                    matrixRoundTrip.x, matrixRoundTrip.y,
                    matrixRoundTrip.z, matrixRoundTrip.w,
                    Quat_AngleBetween( source, matrixRoundTrip ).radians );
                REQUIRE( Quat_RotationEquivalent(
                    source, matrixRoundTrip, 0.001f ) );
            }
        }
    }
}

TEST_CASE( "matrix products and inverses obey column-vector composition",
           "[CypherMath][Matrix4]" )
{
    const quat_t rotation = Quat_FromUnitAxisAngle(
        CY_VEC3_UP, Angle_FromDegrees( 37.0f ) );
    const mat4_t matrix = Mat4_FromTRS(
        Vec3_Make( 3.0f, -2.0f, 5.0f ),
        rotation,
        Vec3_Make( 2.0f, 3.0f, 4.0f ) );
    REQUIRE( Mat4_IsAffine( matrix, 0.0f ) );
    REQUIRE( Mat4_Determinant( matrix ) == Approx( 24.0f ).margin( 0.0001f ) );

    mat4_t inverse{};
    REQUIRE( Mat4_TryInverse( matrix, 0.0000001f, &inverse ) );
    REQUIRE( Mat4_NearlyEquals(
        Mat4_Multiply( inverse, matrix ),
        CY_MAT4_IDENTITY, 0.0001f, 0.0001f ) );

    const vec3_t point = Vec3_Make( 7.0f, -4.0f, 2.0f );
    const vec3_t transformed = Mat4_TransformPointAffine( matrix, point );
    const vec3_t restored = Mat4_TransformPointAffine( inverse, transformed );
    RequireVec3( restored, point.x, point.y, point.z, 0.0002f );
}

TEST_CASE( "look-at maps the eye to origin and target down negative Z",
           "[CypherMath][Matrix4][Camera]" )
{
    const vec3_t eye = Vec3_Make( 5.0f, 2.0f, 3.0f );
    const vec3_t target = Vec3_Make( 1.0f, 2.0f, 3.0f );
    mat4_t view{};
    REQUIRE( Mat4_TryLookAtRH(
        eye, target, CY_VEC3_UP, 0.000001f, &view ) );
    RequireVec3( Mat4_TransformPointAffine( view, eye ), 0.0f, 0.0f, 0.0f );
    RequireVec3(
        Mat4_TransformPointAffine( view, target ), 0.0f, 0.0f, -4.0f );
}

TEST_CASE( "perspective matrices map near and far depth explicitly",
           "[CypherMath][Matrix4][Projection]" )
{
    for ( const clip_depth_range_t depthRange : {
              clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
              clip_depth_range_t::ZERO_TO_ONE } ) {
        mat4_t projection{};
        REQUIRE( Mat4_TryPerspectiveRH(
            Angle_FromDegrees( 90.0f ), 1.0f, 1.0f, 10.0f,
            depthRange, &projection ) );

        vec3_t nearPoint{};
        vec3_t farPoint{};
        REQUIRE( Mat4_TryProjectPoint(
            projection, Vec3_Make( 0.0f, 0.0f, -1.0f ),
            0.000001f, &nearPoint ) );
        REQUIRE( Mat4_TryProjectPoint(
            projection, Vec3_Make( 0.0f, 0.0f, -10.0f ),
            0.000001f, &farPoint ) );
        const f32 expectedNear =
            depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE
                ? -1.0f
                : 0.0f;
        REQUIRE( nearPoint.z == Approx( expectedNear ).margin( 0.00001f ) );
        REQUIRE( farPoint.z == Approx( 1.0f ).margin( 0.00001f ) );
    }
}

TEST_CASE( "singular matrices fail inversion with identity output",
           "[CypherMath][Matrix]" )
{
    mat4_t inverse = CY_MAT4_ZERO;
    REQUIRE_FALSE( Mat4_TryInverse(
        Mat4_FromScale( Vec3_Make( 1.0f, 0.0f, 1.0f ) ),
        0.000001f, &inverse ) );
    REQUIRE( Mat4_NearlyEquals(
        inverse, CY_MAT4_IDENTITY, 0.0f, 0.0f ) );
}

TEST_CASE( "TRS matrices invert across translation, rotation, and reflection cases",
           "[CypherMath][Matrix4][Property]" )
{
    constexpr vec3_t scales[]{
        { 1.0f, 1.0f, 1.0f },
        { 2.0f, 3.0f, 4.0f },
        { -2.0f, 3.0f, 4.0f },
        { -2.0f, -3.0f, 4.0f },
        { -2.0f, -3.0f, -4.0f }
    };
    for ( u32 i = 0u; i < 5u; ++i ) {
        const mat4_t matrix = Mat4_FromTRS(
            Vec3_Make(
                static_cast<f32>( i ) * 3.0f,
                -static_cast<f32>( i ),
                2.0f + static_cast<f32>( i ) ),
            Quat_FromEulerXYZ( Vec3_Make(
                0.1f * static_cast<f32>( i ),
                -0.2f * static_cast<f32>( i ),
                0.3f * static_cast<f32>( i ) ) ),
            scales[i] );
        mat4_t inverse{};
        REQUIRE( Mat4_TryInverse( matrix, 0.0000001f, &inverse ) );
        REQUIRE( Mat4_NearlyEquals(
            Mat4_Multiply( matrix, inverse ),
            CY_MAT4_IDENTITY, 0.0002f, 0.0001f ) );
    }
}
