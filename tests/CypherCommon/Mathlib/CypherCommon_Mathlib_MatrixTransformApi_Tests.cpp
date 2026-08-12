//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_MatrixTransformApi_Tests.cpp
//  Purpose: Verifies matrix, affine, quaternion, and TRS public APIs.
//  Details: Contracts preserve column-major storage, transform order, checked
//           conversions, normal transforms, and rotation interpolation policy.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
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

void RequireVec3(
    vec3_t value, f32 x, f32 y, f32 z, f32 margin = 0.00002f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
}

void RequireVec4(
    vec4_t value, f32 x, f32 y, f32 z, f32 w,
    f32 margin = 0.00002f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
    REQUIRE( value.w == Approx( w ).margin( margin ) );
}

void RequireQuat(
    quat_t value, f32 x, f32 y, f32 z, f32 w,
    f32 margin = 0.00002f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
    REQUIRE( value.w == Approx( w ).margin( margin ) );
}

} // namespace

TEST_CASE( "Matrix3 storage access and arithmetic preserve column-major policy",
           "[CypherCommon][Mathlib][Matrix3][API]" )
{
    mat3_t matrix = Mat3_FromRows(
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( 4.0f, 5.0f, 6.0f ),
        Vec3_Make( 7.0f, 8.0f, 9.0f ) );
    REQUIRE( Mat3_Index( 1u, 2u ) == 7u );
    RequireVec3( Mat3_Row( matrix, 1u ), 4.0f, 5.0f, 6.0f );
    RequireVec3( Mat3_Column( matrix, 2u ), 3.0f, 6.0f, 9.0f );
    REQUIRE( Mat3_Component( matrix, 2u, 1u ) == 8.0f );
    Mat3_SetComponent( &matrix, 2u, 1u, 10.0f );
    REQUIRE( Mat3_Component( matrix, 2u, 1u ) == 10.0f );
    REQUIRE( Mat3_IsFinite( matrix ) );

    const mat3_t added = Mat3_Add( CY_MAT3_IDENTITY, CY_MAT3_IDENTITY );
    REQUIRE( Mat3_Component( added, 0u, 0u ) == 2.0f );
    const mat3_t subtracted = Mat3_Subtract( added, CY_MAT3_IDENTITY );
    REQUIRE( Mat3_NearlyEquals(
        subtracted, CY_MAT3_IDENTITY, 0.0f, 0.0f ) );
    const mat3_t scaled = Mat3_Scale( CY_MAT3_IDENTITY, 3.0f );
    REQUIRE( Mat3_Determinant( scaled ) == Approx( 27.0f ) );
}

TEST_CASE( "Matrix3 scale and vector transforms map each axis independently",
           "[CypherCommon][Mathlib][Matrix3][API]" )
{
    const mat3_t scale = Mat3_FromScale( Vec3_Make( 2.0f, 3.0f, 4.0f ) );
    RequireVec3( Mat3_TransformVector(
        scale, Vec3_Make( 1.0f, 2.0f, 3.0f ) ), 2.0f, 6.0f, 12.0f );
    const mat3_t columns = Mat3_FromColumns(
        CY_VEC3_FORWARD, CY_VEC3_LEFT, CY_VEC3_UP );
    REQUIRE( Mat3_NearlyEquals(
        columns, CY_MAT3_IDENTITY, 0.0f, 0.0f ) );
}

TEST_CASE( "Matrix4 storage arithmetic and homogeneous transforms are complete",
           "[CypherCommon][Mathlib][Matrix4][API]" )
{
    mat4_t matrix = Mat4_FromRows(
        Vec4_Make( 1.0f, 2.0f, 3.0f, 4.0f ),
        Vec4_Make( 5.0f, 6.0f, 7.0f, 8.0f ),
        Vec4_Make( 9.0f, 10.0f, 11.0f, 12.0f ),
        Vec4_Make( 13.0f, 14.0f, 15.0f, 16.0f ) );
    REQUIRE( Mat4_Index( 2u, 3u ) == 14u );
    RequireVec4( Mat4_Row( matrix, 1u ), 5.0f, 6.0f, 7.0f, 8.0f );
    RequireVec4( Mat4_Column( matrix, 2u ), 3.0f, 7.0f, 11.0f, 15.0f );
    REQUIRE( Mat4_Component( matrix, 2u, 1u ) == 10.0f );
    Mat4_SetComponent( &matrix, 2u, 1u, 20.0f );
    REQUIRE( Mat4_Component( matrix, 2u, 1u ) == 20.0f );
    REQUIRE( Mat4_IsFinite( matrix ) );

    const mat4_t added = Mat4_Add( CY_MAT4_IDENTITY, CY_MAT4_IDENTITY );
    REQUIRE( Mat4_Component( added, 3u, 3u ) == 2.0f );
    REQUIRE( Mat4_NearlyEquals(
        Mat4_Subtract( added, CY_MAT4_IDENTITY ),
        CY_MAT4_IDENTITY, 0.0f, 0.0f ) );
    REQUIRE( Mat4_Component(
        Mat4_Scale( CY_MAT4_IDENTITY, 2.0f ), 0u, 0u ) == 2.0f );

    const mat4_t transposed = Mat4_Transpose( matrix );
    REQUIRE( Mat4_Component( transposed, 1u, 2u ) ==
             Mat4_Component( matrix, 2u, 1u ) );
    RequireVec4( Mat4_TransformVector4(
        CY_MAT4_IDENTITY, Vec4_Make( 1.0f, 2.0f, 3.0f, 4.0f ) ),
        1.0f, 2.0f, 3.0f, 4.0f );
}

TEST_CASE( "Matrix4 constructors expose translation linear and rotation parts",
           "[CypherCommon][Mathlib][Matrix4][API]" )
{
    const mat4_t fromColumns = Mat4_FromColumns(
        CY_VEC4_X, CY_VEC4_Y, CY_VEC4_Z, CY_VEC4_W );
    REQUIRE( Mat4_NearlyEquals(
        fromColumns, CY_MAT4_IDENTITY, 0.0f, 0.0f ) );

    const vec3_t translation = Vec3_Make( 3.0f, -2.0f, 5.0f );
    const mat4_t translated = Mat4_FromTranslation( translation );
    RequireVec3( Mat4_Translation( translated ), 3.0f, -2.0f, 5.0f );
    RequireVec3( Mat4_TransformPointAffine( translated, CY_VEC3_ZERO ),
                 3.0f, -2.0f, 5.0f );
    REQUIRE( Mat3_NearlyEquals(
        Mat4_LinearPart( translated ), CY_MAT3_IDENTITY, 0.0f, 0.0f ) );

    const mat4_t fromQuaternion = Mat4_FromQuaternion( CY_QUAT_IDENTITY );
    REQUIRE( Mat4_NearlyEquals(
        fromQuaternion, CY_MAT4_IDENTITY, 0.0f, 0.0f ) );
}

TEST_CASE( "Affine3 storage constructors and transforms preserve affine meaning",
           "[CypherCommon][Mathlib][Affine3][API]" )
{
    affine3_t affine = Affine3_FromColumns(
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, 3.0f, 0.0f ),
        Vec3_Make( 0.0f, 0.0f, 4.0f ),
        Vec3_Make( 5.0f, 6.0f, 7.0f ) );
    REQUIRE( Affine3_Index( 2u, 3u ) == 11u );
    REQUIRE( Affine3_Component( affine, 1u, 1u ) == 3.0f );
    RequireVec3( Affine3_Column( affine, 2u ), 0.0f, 0.0f, 4.0f );
    RequireVec3( Affine3_Translation( affine ), 5.0f, 6.0f, 7.0f );
    REQUIRE( Mat3_NearlyEquals(
        Affine3_LinearPart( affine ),
        Mat3_FromScale( Vec3_Make( 2.0f, 3.0f, 4.0f ) ),
        0.0f, 0.0f ) );
    REQUIRE( Affine3_IsFinite( affine ) );
    RequireVec3( Affine3_TransformDirection( affine, CY_VEC3_ONE ),
                 2.0f, 3.0f, 4.0f );

    Affine3_SetComponent( &affine, 0u, 3u, 8.0f );
    REQUIRE( Affine3_Component( affine, 0u, 3u ) == 8.0f );
    REQUIRE( Mat4_NearlyEquals(
        Affine3_ToMat4( affine ),
        Mat4_FromRows(
            Vec4_Make( 2.0f, 0.0f, 0.0f, 8.0f ),
            Vec4_Make( 0.0f, 3.0f, 0.0f, 6.0f ),
            Vec4_Make( 0.0f, 0.0f, 4.0f, 7.0f ),
            Vec4_Make( 0.0f, 0.0f, 0.0f, 1.0f ) ),
        0.0f, 0.0f ) );
}

TEST_CASE( "Affine3 conversion and normal transformation reject invalid inputs",
           "[CypherCommon][Mathlib][Affine3][API]" )
{
    const affine3_t scale = Affine3_FromScale( Vec3_Make( 2.0f, 3.0f, 4.0f ) );
    affine3_t roundTrip{};
    REQUIRE( Affine3_TryFromMat4(
        Affine3_ToMat4( scale ), 0.000001f, &roundTrip ) );
    REQUIRE( Affine3_NearlyEquals( scale, roundTrip, 0.0f, 0.0f ) );
    REQUIRE_FALSE( Affine3_TryFromMat4(
        Mat4_FromRows(
            CY_VEC4_X, CY_VEC4_Y, CY_VEC4_Z,
            Vec4_Make( 1.0f, 0.0f, 0.0f, 1.0f ) ),
        0.000001f, &roundTrip ) );

    vec3_t normal{};
    REQUIRE( Affine3_TryTransformNormal(
        scale, CY_VEC3_UP, 0.000001f, &normal ) );
    RequireVec3( normal, 0.0f, 0.0f, 0.25f );
    REQUIRE_FALSE( Affine3_TryTransformNormal(
        Affine3_FromScale( Vec3_Make( 0.0f, 1.0f, 1.0f ) ),
        CY_VEC3_UP, 0.000001f, &normal ) );
    REQUIRE( Vec3_EqualsExact( normal, CY_VEC3_ZERO ) );

    REQUIRE( Affine3_NearlyEquals(
        Affine3_FromQuaternion( CY_QUAT_IDENTITY ),
        CY_AFFINE3_IDENTITY, 0.0f, 0.0f ) );
}

TEST_CASE( "quaternion value arithmetic and normalization preserve components",
           "[CypherCommon][Mathlib][Quaternion][API]" )
{
    const quat_t a = Quat_FromVectorScalar(
        Vec3_Make( 1.0f, 2.0f, 3.0f ), 4.0f );
    RequireVec3( Quat_VectorPart( a ), 1.0f, 2.0f, 3.0f );
    REQUIRE( Quat_EqualsExact( a, Quat_Make( 1.0f, 2.0f, 3.0f, 4.0f ) ) );
    REQUIRE( Quat_IsFinite( a ) );
    REQUIRE_FALSE( Quat_IsFinite( Quat_Make(
        0.0f, 0.0f, 0.0f, std::numeric_limits<f32>::infinity() ) ) );
    RequireQuat( Quat_Add( a, CY_QUAT_IDENTITY ), 1.0f, 2.0f, 3.0f, 5.0f );
    RequireQuat( Quat_Subtract( a, CY_QUAT_IDENTITY ), 1.0f, 2.0f, 3.0f, 3.0f );
    RequireQuat( Quat_Conjugate( a ), -1.0f, -2.0f, -3.0f, 4.0f );
    REQUIRE( Quat_Dot( a, a ) == 30.0f );
    REQUIRE( Quat_LengthSquared( a ) == 30.0f );
    REQUIRE( Quat_Length( a ) == Approx( std::sqrt( 30.0f ) ) );

    const quat_t normalized = Quat_NormalizeUnchecked(
        Quat_Make( 0.0f, 0.0f, 0.0f, 2.0f ) );
    RequireQuat( normalized, 0.0f, 0.0f, 0.0f, 1.0f );
    quat_t checked{};
    f32 originalLength = 0.0f;
    REQUIRE( Quat_TryNormalize(
        Quat_Make( 0.0f, 0.0f, 0.0f, 2.0f ),
        0.000001f, &checked, &originalLength ) );
    REQUIRE( Quat_EqualsExact( checked, CY_QUAT_IDENTITY ) );
    REQUIRE( originalLength == Approx( 2.0f ) );
}

TEST_CASE( "quaternion basis directions and axis-angle paths preserve rotations",
           "[CypherCommon][Mathlib][Quaternion][API]" )
{
    RequireVec3( Quat_Forward( CY_QUAT_IDENTITY ), 1.0f, 0.0f, 0.0f );
    RequireVec3( Quat_Left( CY_QUAT_IDENTITY ), 0.0f, 1.0f, 0.0f );
    RequireVec3( Quat_Up( CY_QUAT_IDENTITY ), 0.0f, 0.0f, 1.0f );

    quat_t rotation{};
    REQUIRE( Quat_TryFromAxisAngle(
        Vec3_Make( 0.0f, 0.0f, 2.0f ), Angle_FromDegrees( 90.0f ),
        0.000001f, &rotation ) );
    REQUIRE( Quat_IsUnit( rotation, 0.00001f ) );
    vec3_t axis{};
    angle_t angle{};
    REQUIRE( Quat_TryToAxisAngle( rotation, 0.000001f, &axis, &angle ) );
    RequireVec3( axis, 0.0f, 0.0f, 1.0f );
    REQUIRE( Angle_Degrees( angle ) == Approx( 90.0f ).margin( 0.0001f ) );

    quat_t look{};
    REQUIRE( Quat_TryLookRotation(
        CY_VEC3_FORWARD, CY_VEC3_UP, 0.000001f, &look ) );
    REQUIRE( Quat_RotationEquivalent(
        look, CY_QUAT_IDENTITY, 0.00001f ) );
}

TEST_CASE( "quaternion interpolation follows the shortest rotation arc",
           "[CypherCommon][Mathlib][Quaternion][API]" )
{
    const quat_t end = Quat_FromUnitAxisAngle(
        CY_VEC3_UP, Angle_FromDegrees( 90.0f ) );
    const quat_t nlerp = Quat_Nlerp( CY_QUAT_IDENTITY, end, 0.5f );
    const quat_t slerp = Quat_Slerp( CY_QUAT_IDENTITY, end, 0.5f );
    REQUIRE( Quat_IsUnit( nlerp, 0.00001f ) );
    REQUIRE( Quat_IsUnit( slerp, 0.00001f ) );
    REQUIRE( Angle_Degrees(
        Quat_AngleBetween( CY_QUAT_IDENTITY, slerp ) ) ==
        Approx( 45.0f ).margin( 0.0001f ) );
    REQUIRE( Quat_RotationEquivalent( nlerp, slerp, 0.00001f ) );
}

TEST_CASE( "TRS validation matrix conversion inverse and interpolation are complete",
           "[CypherCommon][Mathlib][Transform][API]" )
{
    const transform_t a = Transform_Make(
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        CY_QUAT_IDENTITY,
        Vec3_Splat( 2.0f ) );
    const transform_t b = Transform_Make(
        Vec3_Make( 5.0f, 6.0f, 7.0f ),
        Quat_FromUnitAxisAngle( CY_VEC3_UP, Angle_FromDegrees( 90.0f ) ),
        Vec3_Splat( 4.0f ) );
    REQUIRE( Transform_IsFinite( a ) );
    REQUIRE( Transform_HasUniformScale( a, 0.00001f ) );
    REQUIRE_FALSE( Transform_HasUniformScale(
        Transform_Make( CY_VEC3_ZERO, CY_QUAT_IDENTITY,
                        Vec3_Make( 1.0f, 2.0f, 1.0f ) ),
        0.00001f ) );

    REQUIRE( Mat4_NearlyEquals(
        Transform_ToMat4( a ), Affine3_ToMat4( Transform_ToAffine3( a ) ),
        0.00001f, 0.00001f ) );
    affine3_t inverse{};
    REQUIRE( Transform_TryInverseAffine( a, 0.000001f, &inverse ) );
    REQUIRE( Affine3_NearlyEquals(
        Affine3_Multiply( inverse, Transform_ToAffine3( a ) ),
        CY_AFFINE3_IDENTITY, 0.00001f, 0.00001f ) );

    const transform_t halfway = Transform_Interpolate( a, b, 0.5f );
    RequireVec3( halfway.position, 3.0f, 4.0f, 5.0f );
    RequireVec3( halfway.scale, 3.0f, 3.0f, 3.0f );
    REQUIRE( Angle_Degrees(
        Quat_AngleBetween( CY_QUAT_IDENTITY, halfway.rotation ) ) ==
        Approx( 45.0f ).margin( 0.0002f ) );
}
