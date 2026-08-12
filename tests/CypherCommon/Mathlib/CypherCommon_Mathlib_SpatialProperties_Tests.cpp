//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_SpatialProperties_Tests.cpp
//  Purpose: Tests spatial primitive and intersection invariants.
//  Details: Coverage includes empty and touching bounds, sphere tangency,
//           degenerate rays, plane orientation, and barycentric reconstruction.
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

TEST_CASE( "AABB set operations handle empty disjoint and touching bounds",
           "[CypherCommon][Mathlib][Bounds][Property]" )
{
    const aabb_t a = Aabb_Make(
        Vec3_Make( -2.0f, -1.0f, -3.0f ),
        Vec3_Make( 2.0f, 4.0f, 1.0f ) );
    const aabb_t b = Aabb_Make(
        Vec3_Make( 1.0f, 2.0f, -4.0f ),
        Vec3_Make( 5.0f, 6.0f, 0.0f ) );
    const aabb_t intersection = Aabb_Intersection( a, b );
    REQUIRE( Aabb_IsValid( intersection ) );
    RequireVec3Near( intersection.minimum, Vec3_Make( 1.0f, 2.0f, -3.0f ) );
    RequireVec3Near( intersection.maximum, Vec3_Make( 2.0f, 4.0f, 0.0f ) );

    const aabb_t combined = Aabb_Union( a, b );
    REQUIRE( Aabb_ContainsAabb( combined, a ) );
    REQUIRE( Aabb_ContainsAabb( combined, b ) );
    RequireVec3Near( combined.minimum, Vec3_Make( -2.0f, -1.0f, -4.0f ) );
    RequireVec3Near( combined.maximum, Vec3_Make( 5.0f, 6.0f, 1.0f ) );

    const aabb_t disjoint = Aabb_Make(
        Vec3_Make( 6.0f, -1.0f, -1.0f ),
        Vec3_Make( 7.0f, 1.0f, 1.0f ) );
    REQUIRE( Aabb_IsEmpty( Aabb_Intersection( a, disjoint ) ) );
    REQUIRE_FALSE( Aabb_Overlaps( a, disjoint ) );

    const aabb_t touching = Aabb_Make(
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 3.0f, 1.0f, 1.0f ) );
    REQUIRE( Aabb_Overlaps( a, touching ) );
    REQUIRE( Aabb_Volume( Aabb_Intersection( a, touching ) ) == 0.0f );
    REQUIRE( Aabb_DistanceSquaredToPoint(
        a, Vec3_Make( 5.0f, 8.0f, 1.0f ) ) == Approx( 25.0f ) );

    const aabb_t unchanged = Aabb_Union( a, CY_AABB_EMPTY );
    RequireVec3Near( unchanged.minimum, a.minimum );
    RequireVec3Near( unchanged.maximum, a.maximum );
}

TEST_CASE( "Sphere metrics and overlap include exact boundary contact",
           "[CypherCommon][Mathlib][Sphere][Property]" )
{
    const sphere_t sphere = Sphere_Make( CY_VEC3_ZERO, 2.0f );
    REQUIRE( Sphere_IsValid( sphere ) );
    REQUIRE_FALSE( Sphere_IsValid( Sphere_Make( CY_VEC3_ZERO, -0.01f ) ) );
    REQUIRE( Sphere_ContainsPoint( sphere, Vec3_Make( 2.0f, 0.0f, 0.0f ) ) );
    REQUIRE( Sphere_Overlaps(
        sphere, Sphere_Make( Vec3_Make( 4.0f, 0.0f, 0.0f ), 2.0f ) ) );
    REQUIRE_FALSE( Sphere_Overlaps(
        sphere, Sphere_Make( Vec3_Make( 4.01f, 0.0f, 0.0f ), 2.0f ) ) );
    RequireVec3Near(
        Sphere_ClosestPoint( sphere, Vec3_Make( 5.0f, 0.0f, 0.0f ), 0.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ) );
    REQUIRE( Sphere_DistanceToPoint(
        sphere, Vec3_Make( 5.0f, 0.0f, 0.0f ) ) == Approx( 3.0f ) );
    REQUIRE( Sphere_Volume( sphere ) ==
             Approx( ( 32.0f / 3.0f ) * CY_PI_F ) );
    REQUIRE( Sphere_SurfaceArea( sphere ) == Approx( 16.0f * CY_PI_F ) );
}

TEST_CASE( "Ray normalization preserves parameterization and failure outputs",
           "[CypherCommon][Mathlib][Ray][Property]" )
{
    const ray_t source = Ray_Make(
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( 3.0f, 4.0f, 0.0f ) );
    ray_t normalized{};
    f32 originalLength = 0.0f;
    REQUIRE( Ray_TryNormalizeDirection(
        source, 0.000001f, &normalized, &originalLength ) );
    REQUIRE( originalLength == Approx( 5.0f ) );
    REQUIRE( Vec3_IsUnitLength( normalized.direction, 0.00001f ) );
    RequireVec3Near( normalized.origin, source.origin );
    RequireVec3Near(
        Ray_PointAt( source, 2.0f ),
        Ray_PointAt( normalized, 10.0f ) );

    f32 parameter = -1.0f;
    REQUIRE( Ray_TryClosestParameterToPoint(
        Ray_Make( CY_VEC3_ZERO, Vec3_Make( 2.0f, 0.0f, 0.0f ) ),
        Vec3_Make( 6.0f, 4.0f, 0.0f ), 0.000001f, &parameter ) );
    REQUIRE( parameter == Approx( 3.0f ) );
    REQUIRE( Ray_TryClosestParameterToPoint(
        Ray_Make( CY_VEC3_ZERO, CY_VEC3_FORWARD ),
        Vec3_Make( -5.0f, 0.0f, 0.0f ), 0.000001f, &parameter ) );
    REQUIRE( parameter == 0.0f );

    normalized = source;
    originalLength = 9.0f;
    REQUIRE_FALSE( Ray_TryNormalizeDirection(
        Ray_Make( source.origin, CY_VEC3_ZERO ),
        0.0f, &normalized, &originalLength ) );
    RequireVec3Near( normalized.origin, source.origin );
    REQUIRE( Vec3_EqualsExact( normalized.direction, CY_VEC3_ZERO ) );
    REQUIRE( originalLength == 0.0f );
}

TEST_CASE( "Triangle planes preserve winding classification and projection",
           "[CypherCommon][Mathlib][Plane][Triangle]" )
{
    const triangle3_t triangle = Triangle3_Make(
        Vec3_Make( 0.0f, 0.0f, 2.0f ),
        Vec3_Make( 2.0f, 0.0f, 2.0f ),
        Vec3_Make( 0.0f, 2.0f, 2.0f ) );
    plane_t plane{};
    REQUIRE( Triangle3_TryPlane( triangle, 0.000001f, &plane ) );
    REQUIRE( Plane_IsNormalized( plane, 0.00001f ) );
    RequireVec3Near( plane.normal, CY_VEC3_UP );
    REQUIRE( Plane_ClassifyPoint(
        plane, Vec3_Make( 0.0f, 0.0f, 3.0f ), 0.00001f ) ==
        plane_side_t::POSITIVE );
    REQUIRE( Plane_ClassifyPoint(
        plane, Vec3_Make( 0.0f, 0.0f, 1.0f ), 0.00001f ) ==
        plane_side_t::NEGATIVE );
    RequireVec3Near(
        Plane_ProjectPointUnit( plane, Vec3_Make( 3.0f, -4.0f, 8.0f ) ),
        Vec3_Make( 3.0f, -4.0f, 2.0f ) );
    REQUIRE( Plane_ClassifyPoint(
        Plane_Flip( plane ), Vec3_Make( 0.0f, 0.0f, 3.0f ), 0.00001f ) ==
        plane_side_t::NEGATIVE );
}

TEST_CASE( "Triangle barycentric coordinates reconstruct an interior grid",
           "[CypherCommon][Mathlib][Triangle][Property]" )
{
    const triangle3_t triangle = Triangle3_Make(
        Vec3_Make( -2.0f, 1.0f, 0.5f ),
        Vec3_Make( 4.0f, -1.0f, 2.0f ),
        Vec3_Make( 1.0f, 5.0f, -3.0f ) );

    for ( u32 i = 0u; i <= 10u; ++i ) {
        for ( u32 j = 0u; j + i <= 10u; ++j ) {
            const f32 weightB = static_cast<f32>( i ) / 10.0f;
            const f32 weightC = static_cast<f32>( j ) / 10.0f;
            const vec3_t expectedWeights = Vec3_Make(
                1.0f - weightB - weightC, weightB, weightC );
            const vec3_t point = Triangle3_PointFromBarycentric(
                triangle, expectedWeights );
            vec3_t actualWeights{};
            CAPTURE( i, j );
            REQUIRE( Triangle3_TryBarycentric(
                triangle, point, 0.0000001f, &actualWeights ) );
            RequireVec3Near( actualWeights, expectedWeights, 0.00001f );
            RequireVec3Near(
                Triangle3_PointFromBarycentric( triangle, actualWeights ),
                point, 0.00002f );
        }
    }

    vec3_t normal = CY_VEC3_ONE;
    const triangle3_t degenerate = Triangle3_Make(
        CY_VEC3_ZERO, CY_VEC3_FORWARD, Vec3_Scale( CY_VEC3_FORWARD, 2.0f ) );
    REQUIRE_FALSE( Triangle3_TryNormal(
        degenerate, 0.000001f, &normal ) );
    REQUIRE( Vec3_EqualsExact( normal, CY_VEC3_ZERO ) );
}

TEST_CASE( "Primitive intersections include tangency and finite segments",
           "[CypherCommon][Mathlib][Intersection][Boundary]" )
{
    const aabb_t a = Aabb_Make( CY_VEC3_ZERO, CY_VEC3_ONE );
    const aabb_t b = Aabb_Make(
        Vec3_Make( 1.0f, 0.25f, 0.25f ),
        Vec3_Make( 2.0f, 0.75f, 0.75f ) );
    REQUIRE( Intersection_AabbAabb( a, b ) );

    const sphere_t sphereA = Sphere_Make( CY_VEC3_ZERO, 1.0f );
    const sphere_t sphereB = Sphere_Make(
        Vec3_Make( 2.0f, 0.0f, 0.0f ), 1.0f );
    REQUIRE( Intersection_SphereSphere( sphereA, sphereB ) );
    REQUIRE( Intersection_SphereAabb(
        Sphere_Make( Vec3_Make( 2.0f, 0.5f, 0.5f ), 1.0f ), a ) );

    const triangle3_t triangle = Triangle3_Make(
        Vec3_Make( 0.0f, 0.0f, 0.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, 2.0f, 0.0f ) );
    const segment_t segment = Segment_Make(
        Vec3_Make( 0.5f, 0.5f, 1.0f ),
        Vec3_Make( 0.5f, 0.5f, -1.0f ) );
    ray_triangle_hit_t hit{};
    REQUIRE( Intersection_SegmentTriangle(
        segment, triangle, triangle_cull_mode_t::NONE,
        0.000001f, 0.000001f, &hit ) );
    REQUIRE( hit.t == Approx( 0.5f ) );
    RequireVec3Near( Segment_PointAt( segment, hit.t ),
                     Vec3_Make( 0.5f, 0.5f, 0.0f ) );
}
