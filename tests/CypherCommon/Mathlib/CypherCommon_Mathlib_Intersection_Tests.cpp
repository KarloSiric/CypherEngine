//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_Intersection_Tests.cpp
//  Purpose: Tests ray intersections and frustum classification.
//  Details: Coverage includes arbitrary ray magnitudes, parallel slabs, triangle
//           culling, both clip-depth ranges, and reconstructed frustum corners.
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

TEST_CASE( "ray-plane intersection honors the ray parameter interval",
           "[CypherCommon][Mathlib][Intersection][Plane]" )
{
    const ray_t ray = Ray_Make( CY_VEC3_ZERO, Vec3_Make( 2.0f, 0.0f, 0.0f ) );
    const plane_t plane = Plane_Make( CY_VEC3_FORWARD, -5.0f );
    f32 t = 0.0f;
    vec3_t point{};
    REQUIRE( Intersection_RayPlane(
        ray, plane, 0.000001f, 0.0f, 10.0f, &t, &point ) );
    REQUIRE( t == Approx( 2.5f ) );
    RequireVec3( point, 5.0f, 0.0f, 0.0f );
    REQUIRE_FALSE( Intersection_RayPlane(
        ray, plane, 0.000001f, 0.0f, 2.0f, &t, nullptr ) );
}

TEST_CASE( "ray-sphere returns the clipped occupancy interval",
           "[CypherCommon][Mathlib][Intersection][Sphere]" )
{
    const ray_t ray = Ray_Make( CY_VEC3_ZERO, CY_VEC3_FORWARD );
    const sphere_t sphere = Sphere_Make( Vec3_Make( 5.0f, 0.0f, 0.0f ), 1.0f );
    ray_interval_t interval{};
    REQUIRE( Intersection_RaySphere(
        ray, sphere, 0.000001f, 0.0f, 100.0f, &interval ) );
    REQUIRE( interval.tEnter == Approx( 4.0f ) );
    REQUIRE( interval.tExit == Approx( 6.0f ) );

    const ray_t inside = Ray_Make(
        Vec3_Make( 5.0f, 0.0f, 0.0f ), CY_VEC3_FORWARD );
    REQUIRE( Intersection_RaySphere(
        inside, sphere, 0.000001f, 0.0f, 100.0f, &interval ) );
    REQUIRE( interval.tEnter == 0.0f );
    REQUIRE( interval.tExit == Approx( 1.0f ) );
}

TEST_CASE( "ray-AABB slab query handles parallel axes",
           "[CypherCommon][Mathlib][Intersection][Bounds]" )
{
    const aabb_t bounds = Aabb_Make(
        Vec3_Make( 4.0f, -1.0f, -1.0f ),
        Vec3_Make( 6.0f, 1.0f, 1.0f ) );
    ray_interval_t interval{};
    REQUIRE( Intersection_RayAabb(
        Ray_Make( CY_VEC3_ZERO, CY_VEC3_FORWARD ),
        bounds, 0.000001f, 0.0f, 100.0f, &interval ) );
    REQUIRE( interval.tEnter == Approx( 4.0f ) );
    REQUIRE( interval.tExit == Approx( 6.0f ) );

    REQUIRE_FALSE( Intersection_RayAabb(
        Ray_Make( Vec3_Make( 0.0f, 2.0f, 0.0f ), CY_VEC3_FORWARD ),
        bounds, 0.000001f, 0.0f, 100.0f, &interval ) );
}

TEST_CASE( "ray-triangle reports barycentrics and honors face culling",
           "[CypherCommon][Mathlib][Intersection][Triangle]" )
{
    const triangle3_t triangle = Triangle3_Make(
        Vec3_Make( 0.0f, 0.0f, 0.0f ),
        Vec3_Make( 1.0f, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, 1.0f, 0.0f ) );
    const ray_t frontRay = Ray_Make(
        Vec3_Make( 0.25f, 0.25f, 1.0f ),
        Vec3_Make( 0.0f, 0.0f, -1.0f ) );
    ray_triangle_hit_t hit{};
    REQUIRE( Intersection_RayTriangle(
        frontRay, triangle, triangle_cull_mode_t::BACK_FACE,
        0.000001f, 0.000001f, 0.0f, 100.0f, &hit ) );
    REQUIRE( hit.t == Approx( 1.0f ) );
    REQUIRE( hit.weightB == Approx( 0.25f ) );
    REQUIRE( hit.weightC == Approx( 0.25f ) );

    REQUIRE_FALSE( Intersection_RayTriangle(
        frontRay, triangle, triangle_cull_mode_t::FRONT_FACE,
        0.000001f, 0.000001f, 0.0f, 100.0f, &hit ) );
}

TEST_CASE( "frustum extraction classifies points, spheres, and boxes",
           "[CypherCommon][Mathlib][Frustum][Intersection]" )
{
    mat4_t projection{};
    REQUIRE( Mat4_TryPerspectiveRH(
        Angle_FromDegrees( 90.0f ), 1.0f, 1.0f, 10.0f,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE, &projection ) );
    frustum_t frustum{};
    REQUIRE( Frustum_TryFromViewProjection(
        projection, clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
        0.000001f, &frustum ) );

    REQUIRE( Intersection_FrustumPoint(
        frustum, Vec3_Make( 0.0f, 0.0f, -5.0f ), 0.00001f ) ==
        volume_relation_t::INSIDE );
    REQUIRE( Intersection_FrustumPoint(
        frustum, Vec3_Make( 20.0f, 0.0f, -5.0f ), 0.00001f ) ==
        volume_relation_t::OUTSIDE );
    REQUIRE( Intersection_FrustumSphere(
        frustum, Sphere_Make( Vec3_Make( 0.0f, 0.0f, -1.0f ), 0.5f ),
        0.00001f ) == volume_relation_t::INTERSECTING );
    REQUIRE( Intersection_FrustumAabb(
        frustum,
        Aabb_FromCenterExtents(
            Vec3_Make( 0.0f, 0.0f, -5.0f ), Vec3_Splat( 0.5f ) ),
        0.00001f ) == volume_relation_t::INSIDE );
}

TEST_CASE( "frustum corners invert both depth conventions",
           "[CypherCommon][Mathlib][Frustum][Projection]" )
{
    for ( const clip_depth_range_t depthRange : {
              clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
              clip_depth_range_t::ZERO_TO_ONE } ) {
        mat4_t projection{};
        REQUIRE( Mat4_TryPerspectiveRH(
            Angle_FromDegrees( 90.0f ), 1.0f, 1.0f, 10.0f,
            depthRange, &projection ) );
        vec3_t corners[CY_FRUSTUM_CORNER_COUNT]{};
        REQUIRE( Frustum_TryCorners(
            projection, depthRange, 0.0000001f, 0.0000001f, corners ) );
        for ( u32 i = 0u; i < CY_FRUSTUM_CORNER_COUNT; ++i ) {
            const bool_t bFar = ( i & 4u ) != 0u;
            const f32 extent = bFar ? 10.0f : 1.0f;
            REQUIRE( Scalar_Abs( corners[i].x ) ==
                     Approx( extent ).margin( 0.0002f ) );
            REQUIRE( Scalar_Abs( corners[i].y ) ==
                     Approx( extent ).margin( 0.0002f ) );
            REQUIRE( corners[i].z ==
                     Approx( -extent ).margin( 0.0002f ) );
        }
    }
}

TEST_CASE( "orthographic projection maps the declared depth interval",
           "[CypherCommon][Mathlib][Matrix4][Projection]" )
{
    mat4_t projection{};
    REQUIRE( Mat4_TryOrthographicRH(
        -2.0f, 2.0f, -1.0f, 1.0f, 1.0f, 11.0f,
        clip_depth_range_t::ZERO_TO_ONE, &projection ) );
    vec3_t nearPoint{};
    vec3_t farPoint{};
    REQUIRE( Mat4_TryProjectPoint(
        projection, Vec3_Make( -2.0f, -1.0f, -1.0f ),
        0.000001f, &nearPoint ) );
    REQUIRE( Mat4_TryProjectPoint(
        projection, Vec3_Make( 2.0f, 1.0f, -11.0f ),
        0.000001f, &farPoint ) );
    RequireVec3( nearPoint, -1.0f, -1.0f, 0.0f );
    RequireVec3( farPoint, 1.0f, 1.0f, 1.0f );
}
