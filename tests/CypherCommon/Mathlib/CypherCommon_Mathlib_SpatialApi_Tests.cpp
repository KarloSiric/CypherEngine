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

void RequireVec3d(
    vec3d_t value, f64 x, f64 y, f64 z, f64 margin = 1e-9 )
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

TEST_CASE( "AABBd point construction expansion and metrics are complete",
           "[CypherCommon][Mathlib][Bounds][Binary64][API]" )
{
    const aabbd_t pointBounds = Aabbd_FromPoint( Vec3d_Make( 1.0, 2.0, 3.0 ) );
    REQUIRE( Aabbd_IsFinite( pointBounds ) );
    REQUIRE( Aabbd_IsValid( pointBounds ) );
    REQUIRE( Aabbd_ContainsPoint( pointBounds, Vec3d_Make( 1.0, 2.0, 3.0 ) ) );

    const aabbd_t other = Aabbd_Make(
        Vec3d_Make( -2.0, 1.0, -4.0 ), Vec3d_Make( 4.0, 6.0, 8.0 ) );
    const aabbd_t expanded = Aabbd_ExpandAabb( pointBounds, other );
    RequireVec3d( expanded.minimum, -2.0, 1.0, -4.0 );
    RequireVec3d( expanded.maximum, 4.0, 6.0, 8.0 );
    RequireVec3d( Aabbd_Extents( expanded ), 3.0, 2.5, 6.0 );
    REQUIRE( Aabbd_SurfaceArea( expanded ) == Approx( 324.0 ) );
    RequireVec3d(
        Aabbd_ClosestPoint( expanded, Vec3d_Make( 10.0, -5.0, 2.0 ) ),
        4.0, 1.0, 2.0 );

    const f64 nanD = std::numeric_limits<f64>::quiet_NaN();
    REQUIRE_FALSE( Aabbd_IsFinite( Aabbd_Make(
        Vec3d_Make( nanD, 0.0, 0.0 ), CY_VEC3D_ONE ) ) );

    // Precision conversion round trip, plus a deliberate out-of-range failure.
    aabb_t narrowed{};
    REQUIRE( Aabbd_TryToAabb( expanded, &narrowed ) );
    RequireVec3( narrowed.minimum, -2.0f, 1.0f, -4.0f );
    RequireVec3( narrowed.maximum, 4.0f, 6.0f, 8.0f );

    aabb_t failedNarrow{};
    REQUIRE_FALSE( Aabbd_TryToAabb(
        Aabbd_Make( Vec3d_Make( -1.0e300, 0.0, 0.0 ), CY_VEC3D_ONE ), &failedNarrow ) );

    // Conservative AABB re-fit after an affine transform.
    const aabbd_t unitBox = Aabbd_Make(
        Vec3d_Make( -1.0, -1.0, -1.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );
    const affine3d_t scale = Affine3d_FromScale( Vec3d_Make( 2.0, 3.0, 4.0 ) );
    const aabbd_t transformed = Aabbd_TransformAffine( unitBox, scale );
    RequireVec3d( transformed.minimum, -2.0, -3.0, -4.0 );
    RequireVec3d( transformed.maximum, 2.0, 3.0, 4.0 );
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

TEST_CASE( "planed finite normalization triangle construction and transform preserve winding",
           "[CypherCommon][Mathlib][Plane][Binary64][API]" )
{
    const planed_t unnormalized = Planed_Make(
        Vec3d_Make( 0.0, 0.0, 2.0 ), -6.0 );
    REQUIRE( Planed_IsFinite( unnormalized ) );
    planed_t normalized{};
    REQUIRE( Planed_TryNormalize( unnormalized, 0.000001, &normalized ) );
    RequireVec3d( normalized.normal, 0.0, 0.0, 1.0 );
    REQUIRE( normalized.d == Approx( -3.0 ) );

    planed_t trianglePlane{};
    REQUIRE( Planed_TryFromTriangle(
        CY_VEC3D_ZERO,
        Vec3d_Make( 1.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 1.0, 0.0 ),
        0.000001, &trianglePlane ) );
    RequireVec3d( trianglePlane.normal, 0.0, 0.0, 1.0 );
    REQUIRE( Planed_SignedDistance( trianglePlane, CY_VEC3D_ZERO ) == 0.0 );

    REQUIRE( Planed_ClassifyPoint( trianglePlane, Vec3d_Make( 0.0, 0.0, 5.0 ), 0.0001 ) ==
             plane_side_t::POSITIVE );
    REQUIRE( Planed_ClassifyPoint( trianglePlane, Vec3d_Make( 0.0, 0.0, -5.0 ), 0.0001 ) ==
             plane_side_t::NEGATIVE );
    REQUIRE( Planed_ClassifyPoint( trianglePlane, CY_VEC3D_ZERO, 0.0001 ) ==
             plane_side_t::ON_PLANE );

    RequireVec3d( Planed_Flip( trianglePlane ).normal, 0.0, 0.0, -1.0 );

    // Precision conversion round trip.
    const plane_t narrowSource = Plane_Make( Vec3_Make( 0.0f, 0.0f, 1.0f ), -3.0f );
    const planed_t widened = Planed_FromPlane( narrowSource );
    plane_t roundTrip{};
    REQUIRE( Planed_TryToPlane( widened, &roundTrip ) );
    RequireVec3( roundTrip.normal, 0.0f, 0.0f, 1.0f );
    REQUIRE( roundTrip.d == Approx( -3.0f ) );

    plane_t failedNarrow{};
    REQUIRE_FALSE( Planed_TryToPlane(
        Planed_Make( CY_VEC3D_UP, 1.0e300 ), &failedNarrow ) );

    // A uniform scale must move the plane's point and renormalize its normal.
    const affine3d_t scale = Affine3d_FromScale( Vec3d_Make( 2.0, 2.0, 2.0 ) );
    planed_t transformed{};
    REQUIRE( Planed_TryTransform(
        Planed_Make( CY_VEC3D_UP, -3.0 ), scale, 1e-9, 1e-9, &transformed ) );
    RequireVec3d( transformed.normal, 0.0, 0.0, 1.0 );
    REQUIRE( transformed.d == Approx( -6.0 ) );
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

TEST_CASE( "triangle3d centroid area winding barycentric and affine mapping are complete",
           "[CypherCommon][Mathlib][Triangle][Binary64][API]" )
{
    const triangle3d_t triangle = Triangle3d_Make(
        CY_VEC3D_ZERO,
        Vec3d_Make( 2.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 2.0, 0.0 ) );
    REQUIRE( Triangle3d_IsFinite( triangle ) );
    RequireVec3d( Triangle3d_Centroid( triangle ), 2.0 / 3.0, 2.0 / 3.0, 0.0 );
    RequireVec3d( Triangle3d_NormalUnnormalized( triangle ), 0.0, 0.0, 4.0 );
    REQUIRE( Triangle3d_TwiceArea( triangle ) == Approx( 4.0 ) );
    REQUIRE( Triangle3d_Area( triangle ) == Approx( 2.0 ) );

    const triangle3d_t transformed = Triangle3d_TransformAffine(
        triangle, Affine3d_FromTranslation( Vec3d_Make( 3.0, 4.0, 5.0 ) ) );
    RequireVec3d( transformed.a, 3.0, 4.0, 5.0 );
    RequireVec3d( transformed.b, 5.0, 4.0, 5.0 );
    RequireVec3d( transformed.c, 3.0, 6.0, 5.0 );

    vec3d_t unitNormal{};
    REQUIRE( Triangle3d_TryNormal( triangle, 1e-9, &unitNormal ) );
    RequireVec3d( unitNormal, 0.0, 0.0, 1.0 );

    planed_t plane{};
    REQUIRE( Triangle3d_TryPlane( triangle, 1e-9, &plane ) );
    RequireVec3d( plane.normal, 0.0, 0.0, 1.0 );
    REQUIRE( Planed_SignedDistance( plane, CY_VEC3D_ZERO ) ==
             Approx( 0.0 ).margin( 1e-9 ) );

    vec3d_t barycentric{};
    REQUIRE( Triangle3d_TryBarycentric(
        triangle, Vec3d_Make( 0.5, 0.5, 0.0 ), 1e-12, &barycentric ) );
    RequireVec3d( barycentric, 0.5, 0.25, 0.25 );

    REQUIRE( Triangle3d_ContainsPoint(
        triangle, Vec3d_Make( 0.5, 0.5, 0.0 ), 1e-9, 1e-9, 1e-9 ) );
    REQUIRE_FALSE( Triangle3d_ContainsPoint(
        triangle, Vec3d_Make( 5.0, 5.0, 0.0 ), 1e-9, 1e-9, 1e-9 ) );

    RequireVec3d(
        Triangle3d_ClosestPoint( triangle, Vec3d_Make( -5.0, -5.0, 0.0 ) ),
        0.0, 0.0, 0.0 );

    // Precision conversion round trip.
    triangle3_t narrowed{};
    REQUIRE( Triangle3d_TryToTriangle3( triangle, &narrowed ) );
    RequireVec3( narrowed.a, 0.0f, 0.0f, 0.0f );
    RequireVec3( narrowed.b, 2.0f, 0.0f, 0.0f );
    RequireVec3( narrowed.c, 0.0f, 2.0f, 0.0f );
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
