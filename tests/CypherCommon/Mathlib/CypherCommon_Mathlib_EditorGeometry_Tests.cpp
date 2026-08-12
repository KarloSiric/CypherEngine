//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_EditorGeometry_Tests.cpp
//  Purpose: Tests geometry operations shared by editor authoring tools.
//  Details: Covers planar predicates, triangulation, convex brush recovery,
//           deterministic snapping, and spline arc-length lookup.
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

f64 TriangleArea2( vec2_t a, vec2_t b, vec2_t c )
{
    return std::abs( Geometry2D_Orientation( a, b, c ) ) * 0.5;
}

} // namespace

TEST_CASE( "2D segment intersections classify crossings and overlap",
           "[CypherCommon][Mathlib][Editor][Geometry2D]" )
{
    const segment2_intersection_t crossing = Geometry2D_IntersectSegments(
        { Vec2_Make( 0.0f, 0.0f ), Vec2_Make( 4.0f, 4.0f ) },
        { Vec2_Make( 0.0f, 4.0f ), Vec2_Make( 4.0f, 0.0f ) },
        0.000001f );
    REQUIRE( crossing.kind == segment2_intersection_kind_t::POINT );
    RequireVec2( crossing.point0, 2.0f, 2.0f );
    REQUIRE( crossing.parameterA0 == Approx( 0.5f ) );
    REQUIRE( crossing.parameterB0 == Approx( 0.5f ) );

    const segment2_intersection_t overlap = Geometry2D_IntersectSegments(
        { Vec2_Make( 0.0f, 0.0f ), Vec2_Make( 4.0f, 0.0f ) },
        { Vec2_Make( 2.0f, 0.0f ), Vec2_Make( 6.0f, 0.0f ) },
        0.000001f );
    REQUIRE( overlap.kind == segment2_intersection_kind_t::OVERLAP );
    RequireVec2( overlap.point0, 2.0f, 0.0f );
    RequireVec2( overlap.point1, 4.0f, 0.0f );
}

TEST_CASE( "concave polygons triangulate without changing area",
           "[CypherCommon][Mathlib][Editor][Polygon2]" )
{
    constexpr vec2_t polygon[]{
        { 0.0f, 0.0f },
        { 4.0f, 0.0f },
        { 4.0f, 4.0f },
        { 2.0f, 2.0f },
        { 0.0f, 4.0f }
    };
    u32 scratch[5]{};
    u32 indices[9]{};

    REQUIRE( Polygon2_IsSimple( polygon, 5u, 0.000001f ) );
    REQUIRE_FALSE( Polygon2_IsConvex( polygon, 5u, 0.000001 ) );
    REQUIRE( Polygon2_ContainsPoint(
        polygon, 5u, Vec2_Make( 1.0f, 1.0f ), 0.000001f, true ) );
    REQUIRE_FALSE( Polygon2_ContainsPoint(
        polygon, 5u, Vec2_Make( 2.0f, 3.5f ), 0.000001f, true ) );

    const polygon_triangulation_result_t result = Polygon2_Triangulate(
        polygon, 5u, 0.000001, scratch, 5u, indices, 9u );
    REQUIRE( result.status == polygon_triangulation_status_t::OK );
    REQUIRE( result.cTriangles == 3u );
    REQUIRE( result.cIndicesWritten == 9u );

    f64 triangleArea = 0.0;
    for ( usize i = 0u; i < result.cIndicesWritten; i += 3u ) {
        REQUIRE( indices[i] < 5u );
        REQUIRE( indices[i + 1u] < 5u );
        REQUIRE( indices[i + 2u] < 5u );
        triangleArea += TriangleArea2(
            polygon[indices[i]], polygon[indices[i + 1u]],
            polygon[indices[i + 2u]] );
    }
    REQUIRE( triangleArea == Approx( std::abs( Polygon2_SignedArea( polygon, 5u ) ) ) );
}

TEST_CASE( "self-intersecting polygons are rejected",
           "[CypherCommon][Mathlib][Editor][Polygon2]" )
{
    constexpr vec2_t bowTie[]{
        { 0.0f, 0.0f },
        { 3.0f, 3.0f },
        { 0.0f, 3.0f },
        { 3.0f, 0.0f }
    };
    u32 scratch[4]{};
    u32 indices[6]{};
    REQUIRE_FALSE( Polygon2_IsSimple( bowTie, 4u, 0.000001f ) );
    REQUIRE( Polygon2_Triangulate(
        bowTie, 4u, 0.000001, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::NOT_SIMPLE );
}

TEST_CASE( "planar 3D polygons derive a basis, area, and triangulation",
           "[CypherCommon][Mathlib][Editor][Polygon3]" )
{
    constexpr vec3_t polygon[]{
        { 1.0f, 2.0f, 3.0f },
        { 5.0f, 2.0f, 3.0f },
        { 5.0f, 6.0f, 3.0f },
        { 1.0f, 6.0f, 3.0f }
    };
    polygon3_basis_t basis{};
    REQUIRE( Polygon3_TryBasis( polygon, 4u, 0.000001f, &basis ) );
    REQUIRE( Polygon3_IsPlanar( polygon, 4u, basis, 0.000001f ) );

    f32 area = 0.0f;
    vec3_t centroid{};
    REQUIRE( Polygon3_TryAreaCentroid(
        polygon, 4u, basis, 0.000001, &area, &centroid ) );
    REQUIRE( area == Approx( 16.0f ) );
    RequireVec3( centroid, 3.0f, 4.0f, 3.0f );

    vec2_t projected[4]{};
    u32 scratch[4]{};
    u32 indices[6]{};
    const polygon_triangulation_result_t result = Polygon3_Triangulate(
        polygon, 4u, basis, 0.000001,
        projected, 4u, scratch, 4u, indices, 6u );
    REQUIRE( result.status == polygon_triangulation_status_t::OK );
    REQUIRE( result.cTriangles == 2u );
}

TEST_CASE( "convex brush planes recover a cube and each face",
           "[CypherCommon][Mathlib][Editor][Brush]" )
{
    constexpr plane_t planes[]{
        { { 1.0f, 0.0f, 0.0f }, -1.0f },
        { { -1.0f, 0.0f, 0.0f }, -1.0f },
        { { 0.0f, 1.0f, 0.0f }, -1.0f },
        { { 0.0f, -1.0f, 0.0f }, -1.0f },
        { { 0.0f, 0.0f, 1.0f }, -1.0f },
        { { 0.0f, 0.0f, -1.0f }, -1.0f }
    };
    vec3_t vertices[20]{};
    const brush_vertex_result_t result = Brush_BuildVertices(
        planes, 6u, 0.000001, 0.00001f, 0.00001f, vertices, 20u );
    REQUIRE( result.status == brush_build_status_t::OK );
    REQUIRE( result.cVerticesWritten == 8u );
    REQUIRE( Brush_ContainsPoint( planes, 6u, CY_VEC3_ZERO, 0.0f ) );
    REQUIRE_FALSE( Brush_ContainsPoint(
        planes, 6u, Vec3_Make( 1.1f, 0.0f, 0.0f ), 0.0f ) );

    aabb_t bounds{};
    REQUIRE( Brush_TryBounds( vertices, result.cVerticesWritten, &bounds ) );
    RequireVec3( bounds.minimum, -1.0f, -1.0f, -1.0f );
    RequireVec3( bounds.maximum, 1.0f, 1.0f, 1.0f );

    for ( plane_t facePlane : planes ) {
        vec3_t face[8]{};
        const brush_vertex_result_t faceResult = Brush_BuildFacePolygon(
            facePlane, vertices, result.cVerticesWritten,
            0.00001f, 0.000001f, face, 8u );
        REQUIRE( faceResult.status == brush_build_status_t::OK );
        REQUIRE( faceResult.cVerticesWritten == 4u );
    }
}

TEST_CASE( "grid snapping handles negative coordinates and custom origins",
           "[CypherCommon][Mathlib][Editor][Snap]" )
{
    f32 snapped = 0.0f;
    REQUIRE( Snap_TryScalar(
        -1.25f, 0.5f, 0.0f, snap_mode_t::NEAREST, &snapped ) );
    REQUIRE( snapped == Approx( -1.5f ) );
    REQUIRE( Snap_TryScalar(
        -1.25f, 0.5f, 0.0f, snap_mode_t::CEIL, &snapped ) );
    REQUIRE( snapped == Approx( -1.0f ) );

    const vec3_t step = Vec3_Make( 0.5f, 2.0f, 4.0f );
    const vec3_t origin = Vec3_Make( 10.0f, -4.0f, 1.0f );
    grid_coord3_t grid{};
    REQUIRE( Snap_TryWorldToGrid(
        Vec3_Make( 11.0f, 2.0f, -7.0f ), step, origin,
        snap_mode_t::NEAREST, &grid ) );
    REQUIRE( grid.x == 2 );
    REQUIRE( grid.y == 3 );
    REQUIRE( grid.z == -2 );
    vec3_t restored{};
    REQUIRE( Snap_TryGridToWorld( grid, step, origin, &restored ) );
    RequireVec3( restored, 11.0f, 2.0f, -7.0f );
}

TEST_CASE( "Bezier splitting and arc tables preserve curve endpoints",
           "[CypherCommon][Mathlib][Editor][Spline]" )
{
    const cubic_bezier3_t curve{
        Vec3_Make( 0.0f, 0.0f, 0.0f ),
        Vec3_Make( 1.0f, 2.0f, 0.0f ),
        Vec3_Make( 3.0f, 2.0f, 0.0f ),
        Vec3_Make( 4.0f, 0.0f, 0.0f )
    };
    RequireVec3( Spline_BezierPoint( curve, 0.0f ), 0.0f, 0.0f, 0.0f );
    RequireVec3( Spline_BezierPoint( curve, 1.0f ), 4.0f, 0.0f, 0.0f );
    RequireVec3( Spline_BezierDerivative( curve, 0.0f ), 3.0f, 6.0f, 0.0f );

    cubic_bezier3_t left{};
    cubic_bezier3_t right{};
    Spline_BezierSplit( curve, 0.35f, &left, &right );
    const vec3_t split = Spline_BezierPoint( curve, 0.35f );
    REQUIRE( Vec3_NearlyEquals( left.p3, split, 0.00001f, 0.00001f ) );
    REQUIRE( Vec3_NearlyEquals( right.p0, split, 0.00001f, 0.00001f ) );

    spline_arc_sample_t samples[33]{};
    spline_arc_table_result_t table{};
    REQUIRE( Spline_TryBuildBezierArcTable(
        curve, 33u, samples, 33u, &table ) );
    REQUIRE( table.cSamplesWritten == 33u );
    REQUIRE( table.totalLength > Vec3_Distance( curve.p0, curve.p3 ) );
    for ( usize i = 1u; i < table.cSamplesWritten; ++i ) {
        REQUIRE( samples[i].distance >= samples[i - 1u].distance );
    }

    f32 parameter = 0.0f;
    REQUIRE( Spline_TryArcParameterAtDistance(
        samples, table.cSamplesWritten, table.totalLength * 0.5f, &parameter ) );
    REQUIRE( parameter == Approx( 0.5f ).margin( 0.02f ) );
}
