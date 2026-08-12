//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_TransformGeometry_Tests.cpp
//  Purpose: Tests affine, TRS, and geometry primitive contracts.
//  Details: Tests preserve exact affine composition, reject lossy shear
//           decomposition, and cover bounds, planes, spheres, and triangles.
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

TEST_CASE( "affine composition matches sequential point transformation",
           "[CypherCommon][Mathlib][Affine3][Transform]" )
{
    const transform_t parent = Transform_Make(
        Vec3_Make( 4.0f, 1.0f, -2.0f ),
        Quat_FromUnitAxisAngle( CY_VEC3_UP, Angle_FromDegrees( 35.0f ) ),
        Vec3_Make( 2.0f, 1.0f, 0.5f ) );
    const transform_t local = Transform_Make(
        Vec3_Make( -1.0f, 3.0f, 2.0f ),
        Quat_FromUnitAxisAngle( CY_VEC3_LEFT, Angle_FromDegrees( -20.0f ) ),
        Vec3_Make( 1.0f, 3.0f, 1.0f ) );
    const vec3_t point = Vec3_Make( 2.0f, -4.0f, 1.0f );

    const affine3_t composed = Transform_ComposeAffine( parent, local );
    const vec3_t sequential = Transform_TransformPoint(
        parent, Transform_TransformPoint( local, point ) );
    const vec3_t direct = Affine3_TransformPoint( composed, point );
    RequireVec3( direct, sequential.x, sequential.y, sequential.z, 0.0002f );
}

TEST_CASE( "affine inverse restores points and directions",
           "[CypherCommon][Mathlib][Affine3]" )
{
    const affine3_t transform = Affine3_FromTRS(
        Vec3_Make( 3.0f, 4.0f, 5.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.2f, 0.4f, -0.3f ) ),
        Vec3_Make( -2.0f, 3.0f, 4.0f ) );
    affine3_t inverse{};
    REQUIRE( Affine3_TryInverse( transform, 0.000001f, &inverse ) );
    REQUIRE( Affine3_NearlyEquals(
        Affine3_Multiply( inverse, transform ),
        CY_AFFINE3_IDENTITY, 0.0001f, 0.0001f ) );
}

TEST_CASE( "TRS decomposition round-trips reflection and rejects shear",
           "[CypherCommon][Mathlib][Transform]" )
{
    const affine3_t source = Affine3_FromTRS(
        Vec3_Make( 5.0f, -3.0f, 2.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.3f, -0.2f, 0.7f ) ),
        Vec3_Make( -2.0f, 3.0f, 4.0f ) );
    transform_t decomposed{};
    REQUIRE( Transform_TryFromAffine3(
        source, 0.000001f, 0.0001f, &decomposed ) );
    REQUIRE( Affine3_NearlyEquals(
        source, Transform_ToAffine3( decomposed ),
        0.0002f, 0.0001f ) );

    affine3_t shear = CY_AFFINE3_IDENTITY;
    Affine3_SetComponent( &shear, 0u, 1u, 0.5f );
    decomposed = CY_TRANSFORM_IDENTITY;
    REQUIRE_FALSE( Transform_TryFromAffine3(
        shear, 0.000001f, 0.0001f, &decomposed ) );
    REQUIRE( Transform_NearlyEquals(
        decomposed, CY_TRANSFORM_IDENTITY,
        0.0f, 0.0f, 0.0f ) );
}

TEST_CASE( "TRS decomposition preserves every scale-sign parity",
           "[CypherCommon][Mathlib][Transform][Property]" )
{
    constexpr vec3_t scales[]{
        { 2.0f, 3.0f, 4.0f },
        { -2.0f, 3.0f, 4.0f },
        { 2.0f, -3.0f, 4.0f },
        { 2.0f, 3.0f, -4.0f },
        { -2.0f, -3.0f, 4.0f },
        { -2.0f, 3.0f, -4.0f },
        { 2.0f, -3.0f, -4.0f },
        { -2.0f, -3.0f, -4.0f }
    };
    const quat_t rotation = Quat_FromEulerXYZ(
        Vec3_Make( 0.4f, -0.6f, 0.8f ) );
    for ( vec3_t scale : scales ) {
        const affine3_t source = Affine3_FromTRS(
            Vec3_Make( 7.0f, -2.0f, 9.0f ), rotation, scale );
        transform_t result{};
        REQUIRE( Transform_TryFromAffine3(
            source, 0.000001f, 0.0001f, &result ) );
        REQUIRE( Affine3_NearlyEquals(
            source, Transform_ToAffine3( result ),
            0.0002f, 0.0001f ) );
    }
}

TEST_CASE( "AABB construction and affine mapping remain conservative",
           "[CypherCommon][Mathlib][Bounds]" )
{
    aabb_t bounds = CY_AABB_EMPTY;
    bounds = Aabb_ExpandPoint( bounds, Vec3_Make( -1.0f, -2.0f, -3.0f ) );
    bounds = Aabb_ExpandPoint( bounds, Vec3_Make( 3.0f, 2.0f, 1.0f ) );
    REQUIRE( Aabb_IsValid( bounds ) );
    RequireVec3( Aabb_Center( bounds ), 1.0f, 0.0f, -1.0f );
    RequireVec3( Aabb_Size( bounds ), 4.0f, 4.0f, 4.0f );
    REQUIRE( Aabb_Volume( bounds ) == Approx( 64.0f ) );
    REQUIRE( Aabb_ContainsPoint( bounds, bounds.maximum ) );

    const affine3_t transform = Affine3_FromTRS(
        Vec3_Make( 5.0f, 0.0f, 0.0f ),
        Quat_FromUnitAxisAngle( CY_VEC3_UP, Angle_FromDegrees( 90.0f ) ),
        Vec3_Make( 2.0f, 1.0f, 1.0f ) );
    const aabb_t transformed = Aabb_TransformAffine( bounds, transform );
    for ( u32 i = 0u; i < 8u; ++i ) {
        REQUIRE( Aabb_ContainsPoint(
            transformed,
            Affine3_TransformPoint( transform, Aabb_Corner( bounds, i ) ) ) );
    }
}

TEST_CASE( "sphere merge contains both inputs and identity transform is exact",
           "[CypherCommon][Mathlib][Sphere]" )
{
    const sphere_t a = Sphere_Make( Vec3_Make( 0.0f, 0.0f, 0.0f ), 1.0f );
    const sphere_t b = Sphere_Make( Vec3_Make( 4.0f, 0.0f, 0.0f ), 1.0f );
    const sphere_t merged = Sphere_Merge( a, b, 0.000001f );
    RequireVec3( merged.center, 2.0f, 0.0f, 0.0f );
    REQUIRE( merged.radius == Approx( 3.0f ) );
    REQUIRE( Sphere_ContainsSphere( merged, a ) );
    REQUIRE( Sphere_ContainsSphere( merged, b ) );

    const sphere_t unchanged = Sphere_TransformAffineConservative(
        merged, CY_AFFINE3_IDENTITY );
    RequireVec3( unchanged.center, 2.0f, 0.0f, 0.0f );
    REQUIRE( unchanged.radius == Approx( 3.0f ) );
}

TEST_CASE( "plane affine transformation preserves transformed points",
           "[CypherCommon][Mathlib][Plane]" )
{
    plane_t source{};
    REQUIRE( Plane_TryFromPointNormal(
        CY_VEC3_ZERO, CY_VEC3_FORWARD, 0.000001f, &source ) );
    const affine3_t translation =
        Affine3_FromTranslation( Vec3_Make( 5.0f, 0.0f, 0.0f ) );
    plane_t transformed{};
    REQUIRE( Plane_TryTransform(
        source, translation, 0.000001f, 0.000001f, &transformed ) );
    REQUIRE( Plane_SignedDistance(
        transformed, Vec3_Make( 5.0f, 8.0f, -2.0f ) ) ==
        Approx( 0.0f ).margin( 0.00001f ) );
    REQUIRE( transformed.d == Approx( -5.0f ).margin( 0.00001f ) );
}

TEST_CASE( "triangle barycentrics and closest point cover face and edge regions",
           "[CypherCommon][Mathlib][Triangle]" )
{
    const triangle3_t triangle = Triangle3_Make(
        Vec3_Make( 0.0f, 0.0f, 0.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, 2.0f, 0.0f ) );
    REQUIRE( Triangle3_Area( triangle ) == Approx( 2.0f ) );
    vec3_t barycentric{};
    REQUIRE( Triangle3_TryBarycentric(
        triangle, Vec3_Make( 0.5f, 0.5f, 0.0f ),
        0.000001f, &barycentric ) );
    RequireVec3( barycentric, 0.5f, 0.25f, 0.25f );
    REQUIRE( Triangle3_ContainsPoint(
        triangle, Vec3_Make( 0.5f, 0.5f, 0.0f ),
        0.000001f, 0.00001f, 0.00001f ) );
    RequireVec3(
        Triangle3_ClosestPoint( triangle, Vec3_Make( 2.0f, 2.0f, 1.0f ) ),
        1.0f, 1.0f, 0.0f );
}

TEST_CASE( "segment closest point clamps to finite endpoints",
           "[CypherCommon][Mathlib][Ray]" )
{
    const segment_t segment = Segment_Make(
        Vec3_Make( 0.0f, 0.0f, 0.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ) );
    RequireVec3(
        Segment_ClosestPoint( segment, Vec3_Make( 3.0f, 1.0f, 0.0f ) ),
        2.0f, 0.0f, 0.0f );
    RequireVec3(
        Segment_ClosestPoint( segment, Vec3_Make( 1.0f, 1.0f, 0.0f ) ),
        1.0f, 0.0f, 0.0f );
}
