//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_ViewportAuthoring_Tests.cpp
//  Purpose: Tests viewport, clipping, and planar material-authoring math.
//  Details: Tests preserve Cypher world axes through camera picking and validate
//           allocation-free clipping and reversible face UV projection.
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

#include <limits>

using namespace cypher::math;
using Catch::Approx;

namespace
{

void RequireVec2( vec2_t value, f32 x, f32 y, f32 margin = 0.00005f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
}

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

TEST_CASE( "viewport projection and picking preserve Cypher world axes",
           "[CypherCommon][Mathlib][Editor][Viewport]" )
{
    mat4_t view{};
    mat4_t projection{};
    REQUIRE( Mat4_TryLookAtRH(
        CY_VEC3_ZERO, CY_VEC3_FORWARD, CY_VEC3_UP, 0.000001f, &view ) );
    REQUIRE( Mat4_TryPerspectiveRH(
        Angle_FromDegrees( 90.0f ), 1.0f, 1.0f, 100.0f,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE, &projection ) );
    const mat4_t worldToClip = Mat4_Multiply( projection, view );
    mat4_t clipToWorld{};
    REQUIRE( Mat4_TryInverse( worldToClip, 0.000001f, &clipToWorld ) );

    constexpr viewport_rect_t viewport{ 10.0f, 20.0f, 800.0f, 800.0f };
    viewport_projection_t projected{};
    REQUIRE( Viewport_TryProjectPoint(
        worldToClip, viewport, viewport_origin_t::TOP_LEFT,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
        Vec3_Make( 10.0f, 0.0f, 0.0f ), 0.000001f, &projected ) );
    REQUIRE( projected.bInsideClipVolume );
    RequireVec2(
        Vec2_Make( projected.screen.x, projected.screen.y ),
        410.0f, 420.0f );
    REQUIRE( projected.screen.z >= 0.0f );
    REQUIRE( projected.screen.z <= 1.0f );

    vec3_t unprojected{};
    REQUIRE( Viewport_TryUnprojectPoint(
        clipToWorld, viewport, viewport_origin_t::TOP_LEFT,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
        projected.screen, 0.000001f, &unprojected ) );
    RequireVec3( unprojected, 10.0f, 0.0f, 0.0f, 0.0002f );

    ray_t pickingRay{};
    REQUIRE( Viewport_TryBuildPickingRay(
        clipToWorld, viewport, viewport_origin_t::TOP_LEFT,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
        Vec2_Make( 410.0f, 420.0f ), 0.000001f, 0.000001f,
        &pickingRay ) );
    RequireVec3( pickingRay.origin, 1.0f, 0.0f, 0.0f, 0.0001f );
    RequireVec3( pickingRay.direction, 1.0f, 0.0f, 0.0f, 0.0001f );

    REQUIRE( Viewport_TryProjectPoint(
        worldToClip, viewport, viewport_origin_t::TOP_LEFT,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
        Vec3_Make( -10.0f, 0.0f, 0.0f ), 0.000001f, &projected ) );
    REQUIRE_FALSE( projected.bInsideClipVolume );
}

TEST_CASE( "polygon clipping retains the requested plane half-space",
           "[CypherCommon][Mathlib][Editor][Clip]" )
{
    constexpr vec3_t polygon[]{
        { -2.0f, -1.0f, 0.0f },
        { 2.0f, -1.0f, 0.0f },
        { 2.0f, 1.0f, 0.0f },
        { -2.0f, 1.0f, 0.0f }
    };
    vec3_t output[5]{};
    const polygon_clip_result_t result = Clip_PolygonAgainstPlane(
        polygon, 4u, Plane_Make( CY_VEC3_FORWARD, 0.0f ),
        0.0f, output, 5u );
    REQUIRE( result.status == polygon_clip_status_t::OK );
    REQUIRE( result.cVerticesWritten == 4u );
    for ( usize i = 0u; i < result.cVerticesWritten; ++i ) {
        REQUIRE( output[i].x <= 0.000001f );
    }
}

TEST_CASE( "segments clip against a convex brush interval",
           "[CypherCommon][Mathlib][Editor][Clip]" )
{
    constexpr plane_t planes[]{
        { { 1.0f, 0.0f, 0.0f }, -1.0f },
        { { -1.0f, 0.0f, 0.0f }, -1.0f },
        { { 0.0f, 1.0f, 0.0f }, -1.0f },
        { { 0.0f, -1.0f, 0.0f }, -1.0f },
        { { 0.0f, 0.0f, 1.0f }, -1.0f },
        { { 0.0f, 0.0f, -1.0f }, -1.0f }
    };
    segment_clip_result_t result{};
    REQUIRE( Clip_TrySegmentAgainstConvexPlanes(
        Segment_Make( Vec3_Make( -3.0f, 0.0f, 0.0f ),
                      Vec3_Make( 3.0f, 0.0f, 0.0f ) ),
        planes, 6u, 0.0f, 0.000001f, &result ) );
    RequireVec3( result.segment.start, -1.0f, 0.0f, 0.0f );
    RequireVec3( result.segment.end, 1.0f, 0.0f, 0.0f );
    REQUIRE( result.parameterEnter == Approx( 1.0f / 3.0f ) );
    REQUIRE( result.parameterExit == Approx( 2.0f / 3.0f ) );

    REQUIRE_FALSE( Clip_TrySegmentAgainstConvexPlanes(
        Segment_Make( Vec3_Make( -3.0f, 2.0f, 0.0f ),
                      Vec3_Make( 3.0f, 2.0f, 0.0f ) ),
        planes, 6u, 0.0f, 0.000001f, &result ) );
}

TEST_CASE( "planar UV projection is reversible with scale, rotation, and offset",
           "[CypherCommon][Mathlib][Editor][UV]" )
{
    planar_uv_mapping_t mapping{};
    REQUIRE( Uv_TryBuildPlanarMapping(
        CY_VEC3_ZERO,
        CY_VEC3_UP,
        CY_VEC3_LEFT,
        Vec2_Make( 2.0f, 4.0f ),
        Angle_FromDegrees( 90.0f ),
        Vec2_Make( 0.25f, -0.5f ),
        0.000001f,
        &mapping ) );

    vec2_t uv{};
    REQUIRE( Uv_TryProjectPlanarPoint(
        mapping, Vec3_Make( 4.0f, 8.0f, 0.0f ), 0.000001f, &uv ) );
    RequireVec2( uv, -1.75f, 1.5f, 0.0001f );

    vec3_t world{};
    REQUIRE( Uv_TryUnprojectPlanarPoint(
        mapping, uv, 3.0f, 0.000001f, &world ) );
    RequireVec3( world, 4.0f, 8.0f, 3.0f, 0.0002f );
}

TEST_CASE( "transform gizmo queries hit axis, plane, and rotation handles",
           "[CypherCommon][Mathlib][Editor][Gizmo]" )
{
    const ray_t ray = Ray_Make(
        Vec3_Make( 2.0f, 0.1f, 5.0f ),
        Vec3_Make( 0.0f, 0.0f, -1.0f ) );
    gizmo_axis_hit_t axisHit{};
    REQUIRE( Gizmo_TryHitAxis(
        ray, CY_VEC3_ZERO, CY_VEC3_FORWARD,
        0.000001f, 0.000001f, 0.2f, &axisHit ) );
    RequireVec3( axisHit.pointOnRay, 2.0f, 0.1f, 0.0f );
    RequireVec3( axisHit.pointOnAxis, 2.0f, 0.0f, 0.0f );
    REQUIRE( axisHit.rayDistance == Approx( 5.0f ) );
    REQUIRE( axisHit.axisDistance == Approx( 2.0f ) );
    REQUIRE( axisHit.separation == Approx( 0.1f ) );

    gizmo_plane_hit_t planeHit{};
    REQUIRE( Gizmo_TryHitPlane(
        Ray_Make( Vec3_Make( 0.25f, -0.5f, 5.0f ),
                  Vec3_Make( 0.0f, 0.0f, -1.0f ) ),
        CY_VEC3_ZERO, CY_VEC3_FORWARD, CY_VEC3_LEFT,
        Vec2_Make( 1.0f, 1.0f ), 0.000001f, 0.000001f, &planeHit ) );
    RequireVec3( planeHit.point, 0.25f, -0.5f, 0.0f );
    RequireVec2( planeHit.coordinates, 0.25f, -0.5f );

    gizmo_ring_hit_t ringHit{};
    REQUIRE( Gizmo_TryHitRing(
        Ray_Make( Vec3_Make( 2.0f, 0.0f, 5.0f ),
                  Vec3_Make( 0.0f, 0.0f, -1.0f ) ),
        CY_VEC3_ZERO, CY_VEC3_UP, 2.0f, 0.1f,
        0.000001f, 0.000001f, &ringHit ) );
    RequireVec3( ringHit.point, 2.0f, 0.0f, 0.0f );
    RequireVec3( ringHit.radialDirection, 1.0f, 0.0f, 0.0f );
    REQUIRE( ringHit.radialDistance == Approx( 2.0f ) );

    const f32 notFinite = std::numeric_limits<f32>::quiet_NaN();
    REQUIRE_FALSE( Gizmo_TryHitPlane(
        ray, CY_VEC3_ZERO, CY_VEC3_FORWARD, CY_VEC3_LEFT,
        Vec2_Make( 1.0f, 1.0f ), notFinite, 0.000001f, &planeHit ) );
    REQUIRE_FALSE( Gizmo_TryHitRing(
        ray, CY_VEC3_ZERO, CY_VEC3_UP, 2.0f, 0.1f,
        0.000001f, notFinite, &ringHit ) );
}
