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

void RequireVec3d(
    vec3d_t value, f64 x, f64 y, f64 z, f64 margin = 1e-9 )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
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

TEST_CASE( "polygon binary64 clipping retains the requested plane half-space",
           "[CypherCommon][Mathlib][Editor][Clip][Binary64]" )
{
    constexpr vec3d_t polygon[]{
        { -2.0, -1.0, 0.0 },
        { 2.0, -1.0, 0.0 },
        { 2.0, 1.0, 0.0 },
        { -2.0, 1.0, 0.0 }
    };
    vec3d_t output[5]{};
    const polygon_clip_result_t result = Clip_PolygonAgainstPlaneD(
        polygon, 4u, Planed_Make( CY_VEC3D_FORWARD, 0.0 ),
        0.0, output, 5u );
    REQUIRE( result.status == polygon_clip_status_t::OK );
    REQUIRE( result.cVerticesWritten == 4u );
    for ( usize i = 0u; i < result.cVerticesWritten; ++i ) {
        REQUIRE( output[i].x <= 1e-9 );
    }
}

TEST_CASE( "segments binary64 clip against a convex brush interval",
           "[CypherCommon][Mathlib][Editor][Clip][Binary64]" )
{
    constexpr planed_t planes[]{
        { { 1.0, 0.0, 0.0 }, -1.0 },
        { { -1.0, 0.0, 0.0 }, -1.0 },
        { { 0.0, 1.0, 0.0 }, -1.0 },
        { { 0.0, -1.0, 0.0 }, -1.0 },
        { { 0.0, 0.0, 1.0 }, -1.0 },
        { { 0.0, 0.0, -1.0 }, -1.0 }
    };
    segmentd_clip_result_t result{};
    REQUIRE( Clip_TrySegmentAgainstConvexPlanesD(
        Segmentd_Make( Vec3d_Make( -3.0, 0.0, 0.0 ),
                       Vec3d_Make( 3.0, 0.0, 0.0 ) ),
        planes, 6u, 0.0, 1e-9, &result ) );
    RequireVec3d( result.segment.start, -1.0, 0.0, 0.0 );
    RequireVec3d( result.segment.end, 1.0, 0.0, 0.0 );
    REQUIRE( result.parameterEnter == Approx( 1.0 / 3.0 ) );
    REQUIRE( result.parameterExit == Approx( 2.0 / 3.0 ) );

    REQUIRE_FALSE( Clip_TrySegmentAgainstConvexPlanesD(
        Segmentd_Make( Vec3d_Make( -3.0, 2.0, 0.0 ),
                       Vec3d_Make( 3.0, 2.0, 0.0 ) ),
        planes, 6u, 0.0, 1e-9, &result ) );
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

TEST_CASE( "binary64 planar UV mapping round-trips through projection",
           "[CypherCommon][Mathlib][UV][Binary64]" )
{
    planar_uv_mappingd_t mapping{};
    REQUIRE( Uvd_TryBuildPlanarMapping(
        Vec3d_Make( 1.0, 2.0, 3.0 ), CY_VEC3D_UP, CY_VEC3D_FORWARD,
        Vec2d_Make( 2.0, 4.0 ), 0.0, Vec2d_Make( 0.5, -0.25 ), 1e-12,
        &mapping ) );

    // The basis must be orthonormal, or projection and unprojection would not
    // be inverses of one another.
    REQUIRE( Vec3d_Dot( mapping.uAxis, mapping.vAxis ) ==
             Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( Vec3d_Dot( mapping.uAxis, mapping.normal ) ==
             Approx( 0.0 ).margin( 1e-12 ) );

    const vec3d_t world = Vec3d_Make( 5.0, 2.0, -7.0 );
    vec2d_t uv{};
    REQUIRE( Uvd_TryProjectPlanarPoint( mapping, world, 1e-12, &uv ) );

    vec3d_t recovered{};
    REQUIRE( Uvd_TryUnprojectPlanarPoint( mapping, uv, 0.0, 1e-12, &recovered ) );

    // Unprojection lands on the mapping plane, so only the in-plane components
    // are expected to survive the round trip.
    const vec3d_t delta = Vec3d_Subtract( recovered, world );
    REQUIRE( Vec3d_Dot( delta, mapping.uAxis ) == Approx( 0.0 ).margin( 1e-9 ) );
    REQUIRE( Vec3d_Dot( delta, mapping.vAxis ) == Approx( 0.0 ).margin( 1e-9 ) );
}

TEST_CASE( "binary64 UV projection holds precision far from the world origin",
           "[CypherCommon][Mathlib][UV][Binary64]" )
{
    // This is the reason the authoring mapping is binary64 at all. World-locked
    // texturing must not drift on geometry placed far out in the level; an f32
    // origin has a ULP near 0.008 at this magnitude, which is visible swimming.
    const f64 farAway = 100000.0;
    planar_uv_mappingd_t mapping{};
    REQUIRE( Uvd_TryBuildPlanarMapping(
        Vec3d_Make( farAway, farAway, 0.0 ), CY_VEC3D_UP, CY_VEC3D_FORWARD,
        Vec2d_Make( 1.0, 1.0 ), 0.0, CY_VEC2D_ZERO, 1e-12, &mapping ) );

    // A one-millimetre step must still register as a one-millimetre UV step.
    const vec3d_t base = Vec3d_Make( farAway, farAway, 0.0 );
    const vec3d_t stepped = Vec3d_Add( base, Vec3d_Scale( mapping.uAxis, 0.001 ) );
    vec2d_t baseUv{};
    vec2d_t steppedUv{};
    REQUIRE( Uvd_TryProjectPlanarPoint( mapping, base, 1e-12, &baseUv ) );
    REQUIRE( Uvd_TryProjectPlanarPoint( mapping, stepped, 1e-12, &steppedUv ) );
    REQUIRE( ( steppedUv.x - baseUv.x ) == Approx( 0.001 ).margin( 1e-12 ) );
}

TEST_CASE( "binary64 UV mapping rejects degenerate and non-finite input",
           "[CypherCommon][Mathlib][UV][Binary64]" )
{
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();
    planar_uv_mappingd_t mapping{};

    // A zero-length normal has no plane to map onto.
    REQUIRE_FALSE( Uvd_TryBuildPlanarMapping(
        CY_VEC3D_ZERO, CY_VEC3D_ZERO, CY_VEC3D_FORWARD, Vec2d_Make( 1.0, 1.0 ),
        0.0, CY_VEC2D_ZERO, 1e-12, &mapping ) );

    // A zero UV scale would divide the projection by zero.
    REQUIRE_FALSE( Uvd_TryBuildPlanarMapping(
        CY_VEC3D_ZERO, CY_VEC3D_UP, CY_VEC3D_FORWARD, Vec2d_Make( 0.0, 1.0 ),
        0.0, CY_VEC2D_ZERO, 1e-12, &mapping ) );

    REQUIRE_FALSE( Uvd_TryBuildPlanarMapping(
        Vec3d_Make( nan, 0.0, 0.0 ), CY_VEC3D_UP, CY_VEC3D_FORWARD,
        Vec2d_Make( 1.0, 1.0 ), 0.0, CY_VEC2D_ZERO, 1e-12, &mapping ) );
}

TEST_CASE( "a hint parallel to the normal still yields a deterministic basis",
           "[CypherCommon][Mathlib][UV][Binary64]" )
{
    // Nothing survives projecting the hint onto the plane here, so the fallback
    // basis decides the result. It must be identical every time, or a face's
    // texture would rotate between rebuilds of the same brush.
    planar_uv_mappingd_t first{};
    planar_uv_mappingd_t second{};
    REQUIRE( Uvd_TryBuildPlanarMapping(
        CY_VEC3D_ZERO, CY_VEC3D_UP, CY_VEC3D_UP, Vec2d_Make( 1.0, 1.0 ),
        0.0, CY_VEC2D_ZERO, 1e-12, &first ) );
    REQUIRE( Uvd_TryBuildPlanarMapping(
        CY_VEC3D_ZERO, CY_VEC3D_UP, CY_VEC3D_UP, Vec2d_Make( 1.0, 1.0 ),
        0.0, CY_VEC2D_ZERO, 1e-12, &second ) );

    REQUIRE( first.uAxis.x == second.uAxis.x );
    REQUIRE( first.uAxis.y == second.uAxis.y );
    REQUIRE( first.uAxis.z == second.uAxis.z );
    REQUIRE( first.vAxis.x == second.vAxis.x );
    REQUIRE( first.vAxis.y == second.vAxis.y );
    REQUIRE( first.vAxis.z == second.vAxis.z );
}
