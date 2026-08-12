//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_SpatialApi_Tests.cpp
//  Purpose: Verifies the complete spatial primitive public API.
//  Details: Contracts cover bounds, planes, rays, segments, triangles, spheres,
//           frusta, finite-value rejection, and affine transformation behavior.
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

} // namespace

TEST_CASE( "AABB point construction expansion and metrics are complete",
           "[CypherCommon][Mathlib][Bounds][API]" )
{
    const aabb_t pointBounds = Aabb_FromPoint(
        Vec3_Make( 1.0f, 2.0f, 3.0f ) );
    REQUIRE( Aabb_IsFinite( pointBounds ) );
    REQUIRE( Aabb_IsValid( pointBounds ) );
    REQUIRE( Aabb_ContainsPoint(
        pointBounds, Vec3_Make( 1.0f, 2.0f, 3.0f ) ) );

    const aabb_t other = Aabb_Make(
        Vec3_Make( -2.0f, 1.0f, -4.0f ),
        Vec3_Make( 4.0f, 6.0f, 8.0f ) );
    const aabb_t expanded = Aabb_ExpandAabb( pointBounds, other );
    RequireVec3( expanded.minimum, -2.0f, 1.0f, -4.0f );
    RequireVec3( expanded.maximum, 4.0f, 6.0f, 8.0f );
    RequireVec3( Aabb_Extents( expanded ), 3.0f, 2.5f, 6.0f );
    REQUIRE( Aabb_SurfaceArea( expanded ) == Approx( 324.0f ) );
    RequireVec3( Aabb_ClosestPoint(
        expanded, Vec3_Make( 10.0f, -5.0f, 2.0f ) ),
        4.0f, 1.0f, 2.0f );

    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    REQUIRE_FALSE( Aabb_IsFinite( Aabb_Make(
        Vec3_Make( nan, 0.0f, 0.0f ), CY_VEC3_ONE ) ) );
}

TEST_CASE( "plane finite normalization and triangle construction preserve winding",
           "[CypherCommon][Mathlib][Plane][API]" )
{
    const plane_t unnormalized = Plane_Make(
        Vec3_Make( 0.0f, 0.0f, 2.0f ), -6.0f );
    REQUIRE( Plane_IsFinite( unnormalized ) );
    plane_t normalized{};
    REQUIRE( Plane_TryNormalize(
        unnormalized, 0.000001f, &normalized ) );
    RequireVec3( normalized.normal, 0.0f, 0.0f, 1.0f );
    REQUIRE( normalized.d == Approx( -3.0f ) );

    plane_t trianglePlane{};
    REQUIRE( Plane_TryFromTriangle(
        CY_VEC3_ZERO,
        Vec3_Make( 1.0f, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, 1.0f, 0.0f ),
        0.000001f, &trianglePlane ) );
    RequireVec3( trianglePlane.normal, 0.0f, 0.0f, 1.0f );
    REQUIRE( Plane_SignedDistance( trianglePlane, CY_VEC3_ZERO ) == 0.0f );
}

TEST_CASE( "rays and segments expose finite metrics and affine mapping",
           "[CypherCommon][Mathlib][Ray][Segment][API]" )
{
    const ray_t ray = Ray_Make(
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ) );
    const segment_t segment = Segment_Make(
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( 4.0f, 6.0f, 3.0f ) );
    REQUIRE( Ray_IsFinite( ray ) );
    REQUIRE( Segment_IsFinite( segment ) );
    RequireVec3( Segment_Direction( segment ), 3.0f, 4.0f, 0.0f );
    REQUIRE( Segment_LengthSquared( segment ) == 25.0f );
    REQUIRE( Segment_Length( segment ) == Approx( 5.0f ) );
    REQUIRE( Segment_DistanceSquaredToPoint(
        segment, Vec3_Make( 1.0f, 2.0f, 5.0f ) ) == Approx( 4.0f ) );

    const affine3_t transform = Affine3_FromTRS(
        Vec3_Make( 10.0f, 20.0f, 30.0f ),
        CY_QUAT_IDENTITY,
        Vec3_Make( 2.0f, 3.0f, 4.0f ) );
    const ray_t transformedRay = Ray_TransformAffine( ray, transform );
    RequireVec3( transformedRay.origin, 12.0f, 26.0f, 42.0f );
    RequireVec3( transformedRay.direction, 4.0f, 0.0f, 0.0f );
    const segment_t transformedSegment = Segment_TransformAffine(
        segment, transform );
    RequireVec3( transformedSegment.start, 12.0f, 26.0f, 42.0f );
    RequireVec3( transformedSegment.end, 18.0f, 38.0f, 42.0f );
}

TEST_CASE( "triangle centroid area winding and affine mapping are complete",
           "[CypherCommon][Mathlib][Triangle][API]" )
{
    const triangle3_t triangle = Triangle3_Make(
        CY_VEC3_ZERO,
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, 2.0f, 0.0f ) );
    REQUIRE( Triangle3_IsFinite( triangle ) );
    RequireVec3( Triangle3_Centroid( triangle ),
                 2.0f / 3.0f, 2.0f / 3.0f, 0.0f );
    RequireVec3( Triangle3_NormalUnnormalized( triangle ),
                 0.0f, 0.0f, 4.0f );
    REQUIRE( Triangle3_TwiceArea( triangle ) == Approx( 4.0f ) );

    const triangle3_t transformed = Triangle3_TransformAffine(
        triangle, Affine3_FromTranslation( Vec3_Make( 3.0f, 4.0f, 5.0f ) ) );
    RequireVec3( transformed.a, 3.0f, 4.0f, 5.0f );
    RequireVec3( transformed.b, 5.0f, 4.0f, 5.0f );
    RequireVec3( transformed.c, 3.0f, 6.0f, 5.0f );
}

TEST_CASE( "sphere construction from AABB is centered and conservative",
           "[CypherCommon][Mathlib][Sphere][API]" )
{
    const aabb_t bounds = Aabb_Make(
        Vec3_Make( -1.0f, -2.0f, -3.0f ),
        Vec3_Make( 3.0f, 4.0f, 5.0f ) );
    const sphere_t sphere = Sphere_FromAabb( bounds );
    RequireVec3( sphere.center, 1.0f, 1.0f, 1.0f );
    REQUIRE( sphere.radius == Approx( std::sqrt( 29.0f ) ) );
    for ( u32 i = 0u; i < 8u; ++i ) {
        REQUIRE( Sphere_ContainsPoint( sphere, Aabb_Corner( bounds, i ) ) );
    }
}

TEST_CASE( "frustum access and affine transformation preserve valid planes",
           "[CypherCommon][Mathlib][Frustum][API]" )
{
    mat4_t view{};
    mat4_t projection{};
    REQUIRE( Mat4_TryLookAtRH(
        CY_VEC3_ZERO, CY_VEC3_FORWARD, CY_VEC3_UP,
        0.000001f, &view ) );
    REQUIRE( Mat4_TryPerspectiveRH(
        Angle_FromDegrees( 90.0f ), 1.0f, 1.0f, 100.0f,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE, &projection ) );

    frustum_t frustum{};
    REQUIRE( Frustum_TryFromViewProjection(
        Mat4_Multiply( projection, view ),
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
        0.000001f, &frustum ) );
    REQUIRE( Frustum_IsFinite( frustum ) );
    const plane_t nearPlane = Frustum_Plane(
        frustum, frustum_plane_t::NEAR );
    REQUIRE( Plane_IsFinite( nearPlane ) );
    REQUIRE( Plane_IsNormalized( nearPlane, 0.0001f ) );

    frustum_t transformed{};
    REQUIRE( Frustum_TryTransform(
        frustum,
        Affine3_FromTranslation( Vec3_Make( 3.0f, 4.0f, 5.0f ) ),
        0.000001f, 0.000001f, &transformed ) );
    REQUIRE( Frustum_IsFinite( transformed ) );
    for ( u32 i = 0u; i < CY_FRUSTUM_PLANE_COUNT; ++i ) {
        REQUIRE( Plane_IsNormalized(
            Frustum_Plane( transformed, static_cast<frustum_plane_t>( i ) ),
            0.0001f ) );
    }
}
