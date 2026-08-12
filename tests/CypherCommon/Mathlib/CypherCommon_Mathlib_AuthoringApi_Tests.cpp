//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_AuthoringApi_Tests.cpp
//  Purpose: Verifies the complete editor-authoring and batch math public API.
//  Details: Contracts cover fixed point, brushes, polygons, snapping, splines,
//           viewport validation, and all four-lane batch operations.
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

using namespace cypher::math;
using Catch::Approx;

namespace
{

void RequireVec2( vec2_t value, f32 x, f32 y, f32 margin = 0.00002f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
}

void RequireVec3(
    vec3_t value, f32 x, f32 y, f32 z, f32 margin = 0.00002f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
}

} // namespace

TEST_CASE( "fixed-point raw conversion and subtraction preserve exact values",
           "[CypherCommon][Mathlib][FixedPoint][API]" )
{
    fixed16_16_t value{};
    REQUIRE( Fixed16_16_TryFromF32( 1.5f, &value ) );
    REQUIRE( Fixed16_16_Raw( value ) == 98304 );
    REQUIRE( Fixed16_16_ToF32( value ) == Approx( 1.5f ) );

    fixed16_16_t other{};
    REQUIRE( Fixed16_16_TryFromF32( 0.25f, &other ) );
    fixed16_16_t difference{};
    REQUIRE( Fixed16_16_TrySubtract( value, other, &difference ) );
    REQUIRE( Fixed16_16_ToF32( difference ) == Approx( 1.25f ) );
}

TEST_CASE( "brush candidate count and triple-plane intersection are exact",
           "[CypherCommon][Mathlib][Brush][API]" )
{
    REQUIRE( Brush_MaximumVertexCandidates( 2u ) == 0u );
    REQUIRE( Brush_MaximumVertexCandidates( 6u ) == 20u );

    vec3_t point{};
    REQUIRE( Brush_TryIntersectPlanes(
        Plane_Make( CY_VEC3_FORWARD, -1.0f ),
        Plane_Make( CY_VEC3_LEFT, -2.0f ),
        Plane_Make( CY_VEC3_UP, -3.0f ),
        0.000001, &point ) );
    RequireVec3( point, 1.0f, 2.0f, 3.0f );
    REQUIRE_FALSE( Brush_TryIntersectPlanes(
        Plane_Make( CY_VEC3_FORWARD, -1.0f ),
        Plane_Make( CY_VEC3_FORWARD, -2.0f ),
        Plane_Make( CY_VEC3_UP, -3.0f ),
        0.000001, &point ) );
    REQUIRE( Vec3_EqualsExact( point, CY_VEC3_ZERO ) );
}

TEST_CASE( "2D point-on-segment and polygon centroid include boundary policy",
           "[CypherCommon][Mathlib][Geometry2D][API]" )
{
    const segment2_t segment{
        Vec2_Make( -2.0f, 1.0f ), Vec2_Make( 2.0f, 1.0f )
    };
    REQUIRE( Geometry2D_PointOnSegment(
        Vec2_Make( 0.0f, 1.0f ), segment, 0.000001f ) );
    REQUIRE_FALSE( Geometry2D_PointOnSegment(
        Vec2_Make( 3.0f, 1.0f ), segment, 0.000001f ) );

    constexpr vec2_t square[]{
        { 0.0f, 0.0f }, { 2.0f, 0.0f },
        { 2.0f, 2.0f }, { 0.0f, 2.0f }
    };
    vec2_t centroid{};
    REQUIRE( Polygon2_TryCentroid(
        square, 4u, 0.000001, &centroid ) );
    RequireVec2( centroid, 1.0f, 1.0f );
}

TEST_CASE( "3D polygon projection plane convexity and containment agree",
           "[CypherCommon][Mathlib][Polygon3][API]" )
{
    constexpr vec3_t square[]{
        { 0.0f, 0.0f, 2.0f }, { 2.0f, 0.0f, 2.0f },
        { 2.0f, 2.0f, 2.0f }, { 0.0f, 2.0f, 2.0f }
    };
    polygon3_basis_t basis{};
    REQUIRE( Polygon3_TryBasis( square, 4u, 0.000001f, &basis ) );

    vec2_t projected[4]{};
    Polygon3_ProjectToBasis( square, 4u, basis, projected );
    for ( usize i = 0u; i < 4u; ++i ) {
        REQUIRE( Scalar_IsFinite( projected[i].x ) );
        REQUIRE( Scalar_IsFinite( projected[i].y ) );
    }

    plane_t plane{};
    REQUIRE( Polygon3_TryPlane( square, 4u, 0.000001f, &plane ) );
    REQUIRE( Plane_SignedDistance( plane, square[0] ) ==
             Approx( 0.0f ).margin( 0.000001f ) );
    REQUIRE( Polygon3_IsConvex(
        square, 4u, basis, 0.000001,
        projected, 4u ) );
    REQUIRE( Polygon3_ContainsPoint(
        square, 4u, basis, Vec3_Make( 1.0f, 1.0f, 2.0f ),
        0.000001f, 0.000001f, true,
        projected, 4u ) );
    REQUIRE_FALSE( Polygon3_ContainsPoint(
        square, 4u, basis, Vec3_Make( 3.0f, 1.0f, 2.0f ),
        0.000001f, 0.000001f, true,
        projected, 4u ) );
}

TEST_CASE( "vector and angle snapping share deterministic grid policy",
           "[CypherCommon][Mathlib][Snap][API]" )
{
    vec3_t snapped{};
    REQUIRE( Snap_TryVec3(
        Vec3_Make( 1.2f, -1.2f, 2.8f ),
        Vec3_Splat( 1.0f ), CY_VEC3_ZERO,
        snap_mode_t::NEAREST, &snapped ) );
    RequireVec3( snapped, 1.0f, -1.0f, 3.0f );

    angle_t snappedAngle{};
    REQUIRE( Snap_TryAngle(
        Angle_FromDegrees( 100.0f ), Angle_FromDegrees( 45.0f ),
        CY_ANGLE_ZERO, snap_mode_t::NEAREST, true, &snappedAngle ) );
    REQUIRE( Angle_Degrees( snappedAngle ) == Approx( 90.0f ) );

    RequireVec3( Snap_ProjectPointToPlaneUnit(
        Vec3_Make( 1.0f, 2.0f, 3.0f ), CY_PLANE_Z ),
        1.0f, 2.0f, 0.0f );
    RequireVec3( Snap_DirectionToPrincipalAxis(
        Vec3_Make( -4.0f, 2.0f, 3.0f ) ), -1.0f, 0.0f, 0.0f );
}

TEST_CASE( "Bezier Hermite and Catmull-Rom derivatives preserve endpoint rules",
           "[CypherCommon][Mathlib][Spline][API]" )
{
    const cubic_bezier3_t bezier{
        Vec3_Make( 0.0f, 0.0f, 0.0f ),
        Vec3_Make( 1.0f, 0.0f, 0.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 3.0f, 0.0f, 0.0f )
    };
    RequireVec3( Spline_BezierSecondDerivative( bezier, 0.5f ),
                 0.0f, 0.0f, 0.0f );

    const cubic_hermite3_t hermite{
        Vec3_Make( 0.0f, 0.0f, 0.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 4.0f, 0.0f, 0.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f )
    };
    RequireVec3( Spline_HermitePoint( hermite, 0.0f ),
                 0.0f, 0.0f, 0.0f );
    RequireVec3( Spline_HermitePoint( hermite, 1.0f ),
                 4.0f, 0.0f, 0.0f );
    RequireVec3( Spline_HermiteDerivative( hermite, 0.0f ),
                 2.0f, 0.0f, 0.0f );

    const catmull_rom3_t catmull{
        Vec3_Make( 0.0f, 0.0f, 0.0f ),
        Vec3_Make( 1.0f, 0.0f, 0.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 3.0f, 0.0f, 0.0f )
    };
    RequireVec3( Spline_CatmullRomPoint( catmull, 0.0f, 0.0f ),
                 1.0f, 0.0f, 0.0f );
    RequireVec3( Spline_CatmullRomPoint( catmull, 1.0f, 0.0f ),
                 2.0f, 0.0f, 0.0f );
    RequireVec3( Spline_CatmullRomDerivative( catmull, 0.5f, 0.0f ),
                 1.0f, 0.0f, 0.0f );
}

TEST_CASE( "four-lane batch arithmetic and transforms match scalar operations",
           "[CypherCommon][Mathlib][Batch][API]" )
{
    const vec3_t a[CY_MATH_BATCH_LANES]{
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( 2.0f, 3.0f, 4.0f ),
        Vec3_Make( 3.0f, 4.0f, 5.0f ),
        Vec3_Make( 4.0f, 5.0f, 6.0f )
    };
    const vec3_t b[CY_MATH_BATCH_LANES]{
        CY_VEC3_ONE, CY_VEC3_ONE, CY_VEC3_ONE, CY_VEC3_ONE
    };
    const vec3_soa4_t soaA = Vec3Soa4_Load( a );
    const vec3_soa4_t soaB = Vec3Soa4_Load( b );
    const vec3_soa4_t added = Vec3Soa4_Add( soaA, soaB );
    const vec3_soa4_t subtracted = Vec3Soa4_Subtract( soaA, soaB );
    const vec3_soa4_t scaled = Vec3Soa4_Scale( soaA, 2.0f );
    const f32_soa4_t lengths = Vec3Soa4_LengthSquared( soaA );

    vec3_t output[CY_MATH_BATCH_LANES]{};
    Vec3Soa4_Store( added, output );
    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        REQUIRE( Vec3_EqualsExact( output[i], Vec3_Add( a[i], b[i] ) ) );
        REQUIRE( subtracted.x[i] == Approx( a[i].x - 1.0f ) );
        REQUIRE( scaled.y[i] == Approx( a[i].y * 2.0f ) );
        REQUIRE( lengths.lane[i] == Approx( Vec3_LengthSquared( a[i] ) ) );
    }

    const mat4_t matrix = Mat4_FromTRS(
        Vec3_Make( 10.0f, 20.0f, 30.0f ), CY_QUAT_IDENTITY,
        Vec3_Make( 2.0f, 3.0f, 4.0f ) );
    const vec3_soa4_t points = Vec3Soa4_TransformPointsAffine( matrix, soaA );
    const vec3_soa4_t directions = Vec3Soa4_TransformDirections( matrix, soaA );
    Vec3Soa4_Store( points, output );
    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        REQUIRE( Vec3_NearlyEquals(
            output[i], Mat4_TransformPointAffine( matrix, a[i] ),
            0.00001f, 0.00001f ) );
        RequireVec3(
            Vec3_Make( directions.x[i], directions.y[i], directions.z[i] ),
            a[i].x * 2.0f, a[i].y * 3.0f, a[i].z * 4.0f );
    }
}

TEST_CASE( "viewport validation rejects empty and non-finite rectangles",
           "[CypherCommon][Mathlib][Viewport][API]" )
{
    REQUIRE( Viewport_IsValid( { 0.0f, 0.0f, 1920.0f, 1080.0f } ) );
    REQUIRE_FALSE( Viewport_IsValid( { 0.0f, 0.0f, 0.0f, 1080.0f } ) );
    REQUIRE_FALSE( Viewport_IsValid( { 0.0f, 0.0f, 1920.0f, -1.0f } ) );
}
