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

#include <cmath>
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

f64 TriangleArea2( vec2_t a, vec2_t b, vec2_t c )
{
    return std::abs( Geometry2D_Orientation( a, b, c ) ) * 0.5;
}

void RequireVec3d(
    vec3d_t value, f64 x, f64 y, f64 z, f64 margin = 1e-9 )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
}

void RequireVec2d( vec2d_t value, f64 x, f64 y, f64 margin = 1e-9 )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
}

f64 TriangleArea2D( vec2d_t a, vec2d_t b, vec2d_t c )
{
    return std::abs( Geometry2D_OrientationD( a, b, c ) ) * 0.5;
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

TEST_CASE( "2D binary64 segment intersections classify crossings and overlap",
           "[CypherCommon][Mathlib][Editor][Geometry2D][Binary64]" )
{
    const segment2d_intersection_t crossing = Geometry2D_IntersectSegmentsD(
        { Vec2d_Make( 0.0, 0.0 ), Vec2d_Make( 4.0, 4.0 ) },
        { Vec2d_Make( 0.0, 4.0 ), Vec2d_Make( 4.0, 0.0 ) },
        1e-9 );
    REQUIRE( crossing.kind == segment2_intersection_kind_t::POINT );
    RequireVec2d( crossing.point0, 2.0, 2.0 );
    REQUIRE( crossing.parameterA0 == Approx( 0.5 ) );
    REQUIRE( crossing.parameterB0 == Approx( 0.5 ) );

    const segment2d_intersection_t overlap = Geometry2D_IntersectSegmentsD(
        { Vec2d_Make( 0.0, 0.0 ), Vec2d_Make( 4.0, 0.0 ) },
        { Vec2d_Make( 2.0, 0.0 ), Vec2d_Make( 6.0, 0.0 ) },
        1e-9 );
    REQUIRE( overlap.kind == segment2_intersection_kind_t::OVERLAP );
    RequireVec2d( overlap.point0, 2.0, 0.0 );
    RequireVec2d( overlap.point1, 4.0, 0.0 );
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
        polygon, 5u, 0.000001, 0.000001, scratch, 5u, indices, 9u );
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

TEST_CASE( "concave polygons binary64 triangulate without changing area",
           "[CypherCommon][Mathlib][Editor][Polygon2][Binary64]" )
{
    constexpr vec2d_t polygon[]{
        { 0.0, 0.0 },
        { 4.0, 0.0 },
        { 4.0, 4.0 },
        { 2.0, 2.0 },
        { 0.0, 4.0 }
    };
    u32 scratch[5]{};
    u32 indices[9]{};

    REQUIRE( Polygon2d_IsSimple( polygon, 5u, 1e-9 ) );
    REQUIRE_FALSE( Polygon2d_IsConvex( polygon, 5u, 1e-9 ) );
    REQUIRE( Polygon2d_ContainsPoint(
        polygon, 5u, Vec2d_Make( 1.0, 1.0 ), 1e-9, true ) );
    REQUIRE_FALSE( Polygon2d_ContainsPoint(
        polygon, 5u, Vec2d_Make( 2.0, 3.5 ), 1e-9, true ) );

    const polygon_triangulation_result_t result = Polygon2d_Triangulate(
        polygon, 5u, 1e-9, 1e-9, scratch, 5u, indices, 9u );
    REQUIRE( result.status == polygon_triangulation_status_t::OK );
    REQUIRE( result.cTriangles == 3u );
    REQUIRE( result.cIndicesWritten == 9u );

    f64 triangleArea = 0.0;
    for ( usize i = 0u; i < result.cIndicesWritten; i += 3u ) {
        REQUIRE( indices[i] < 5u );
        REQUIRE( indices[i + 1u] < 5u );
        REQUIRE( indices[i + 2u] < 5u );
        triangleArea += TriangleArea2D(
            polygon[indices[i]], polygon[indices[i + 1u]],
            polygon[indices[i + 2u]] );
    }
    REQUIRE( triangleArea ==
             Approx( std::abs( Polygon2d_SignedArea( polygon, 5u ) ) ) );

    vec2d_t centroid{};
    REQUIRE( Polygon2d_TryCentroid( polygon, 5u, 1e-9, &centroid ) );
    REQUIRE( Vec2d_IsFinite( centroid ) );
}

TEST_CASE( "star polygons are not reported convex",
           "[CypherCommon][Mathlib][Editor][Polygon2]" )
{
    // A pentagram turns the same direction at every corner, so a convexity
    // test built only on consistent orientation sign accepts it. It winds
    // twice, which the total-turning check rejects.
    vec2_t star[5]{};
    vec2d_t starD[5]{};
    constexpr int order[5]{ 0, 2, 4, 1, 3 };
    for ( usize i = 0u; i < 5u; ++i ) {
        const f64 angle = 2.0 * 3.14159265358979323846 * order[i] / 5.0;
        starD[i] = Vec2d_Make( std::cos( angle ), std::sin( angle ) );
        star[i] = Vec2_Make(
            static_cast<f32>( starD[i].x ), static_cast<f32>( starD[i].y ) );
    }
    REQUIRE_FALSE( Polygon2_IsSimple( star, 5u, 0.000001f ) );
    REQUIRE_FALSE( Polygon2_IsConvex( star, 5u, 1e-12 ) );
    REQUIRE_FALSE( Polygon2d_IsSimple( starD, 5u, 1e-12 ) );
    REQUIRE_FALSE( Polygon2d_IsConvex( starD, 5u, 1e-12 ) );

    // A genuinely convex pentagon in ring order must still pass.
    vec2_t pentagon[5]{};
    vec2d_t pentagonD[5]{};
    for ( usize i = 0u; i < 5u; ++i ) {
        const f64 angle = 2.0 * 3.14159265358979323846 * static_cast<f64>( i ) / 5.0;
        pentagonD[i] = Vec2d_Make( std::cos( angle ), std::sin( angle ) );
        pentagon[i] = Vec2_Make(
            static_cast<f32>( pentagonD[i].x ), static_cast<f32>( pentagonD[i].y ) );
    }
    REQUIRE( Polygon2_IsConvex( pentagon, 5u, 1e-12 ) );
    REQUIRE( Polygon2d_IsConvex( pentagonD, 5u, 1e-12 ) );
}

TEST_CASE( "triangulation separates distance and area tolerances",
           "[CypherCommon][Mathlib][Editor][Polygon2][Binary64]" )
{
    // A unit square: edges are 1.0 apart, area is 1.0. Passing one scalar for
    // both roles used to make these two thresholds impossible to set
    // independently; they are now distinct parameters with distinct units.
    constexpr vec2d_t square[]{
        { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 }
    };
    u32 scratch[4]{};
    u32 indices[6]{};

    REQUIRE( Polygon2d_Triangulate(
        square, 4u, 1e-9, 1e-9, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::OK );

    // An area tolerance at or above the polygon area rejects it as degenerate
    // while the distance tolerance stays fine -- the two knobs act separately.
    REQUIRE( Polygon2d_Triangulate(
        square, 4u, 1e-9, 2.0, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::DEGENERATE );

    // A distance tolerance large enough to collapse the edges reports the
    // polygon as non-simple, independently of the area tolerance.
    REQUIRE( Polygon2d_Triangulate(
        square, 4u, 4.0, 1e-9, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::NOT_SIMPLE );

    // Negative values remain rejected on both parameters.
    REQUIRE( Polygon2d_Triangulate(
        square, 4u, -1.0, 1e-9, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::INVALID_ARGUMENT );
    REQUIRE( Polygon2d_Triangulate(
        square, 4u, 1e-9, -1.0, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::INVALID_ARGUMENT );
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
        bowTie, 4u, 0.000001, 0.000001, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::NOT_SIMPLE );
}

TEST_CASE( "self-intersecting polygons binary64 are rejected",
           "[CypherCommon][Mathlib][Editor][Polygon2][Binary64]" )
{
    constexpr vec2d_t bowTie[]{
        { 0.0, 0.0 },
        { 3.0, 3.0 },
        { 0.0, 3.0 },
        { 3.0, 0.0 }
    };
    u32 scratch[4]{};
    u32 indices[6]{};
    REQUIRE_FALSE( Polygon2d_IsSimple( bowTie, 4u, 1e-9 ) );
    REQUIRE( Polygon2d_Triangulate(
        bowTie, 4u, 1e-9, 1e-9, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::NOT_SIMPLE );
}

TEST_CASE( "planar geometry rejects nonfinite coordinates and tolerances",
           "[CypherCommon][Mathlib][Editor][Geometry2D][Validation]" )
{
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const f32 infinity = std::numeric_limits<f32>::infinity();
    const vec2_t triangle[]{
        Vec2_Make( 0.0f, 0.0f ),
        Vec2_Make( 2.0f, 0.0f ),
        Vec2_Make( 0.0f, 2.0f )
    };

    for ( f32 invalid : { nan, infinity, -1.0f } ) {
        CAPTURE( invalid );
        REQUIRE( Geometry2D_IntersectSegments(
            { triangle[0], triangle[1] }, { triangle[1], triangle[2] },
            invalid ).kind == segment2_intersection_kind_t::NONE );
        REQUIRE_FALSE( Polygon2_ContainsPoint(
            triangle, 3u, Vec2_Make( 0.25f, 0.25f ), invalid, true ) );
        REQUIRE_FALSE( Polygon2_IsSimple( triangle, 3u, invalid ) );
    }

    for ( f64 invalid : {
              std::numeric_limits<f64>::quiet_NaN(),
              std::numeric_limits<f64>::infinity(), -1.0 } ) {
        CAPTURE( invalid );
        vec2_t centroid = CY_VEC2_ONE;
        REQUIRE_FALSE( Polygon2_TryCentroid(
            triangle, 3u, invalid, &centroid ) );
        REQUIRE( Vec2_NearlyEquals(
            centroid, CY_VEC2_ZERO, 0.0f, 0.0f ) );
        REQUIRE_FALSE( Polygon2_IsConvex( triangle, 3u, invalid ) );

        u32 scratch[3]{};
        u32 indices[3]{};
        REQUIRE( Polygon2_Triangulate(
            triangle, 3u, invalid, invalid, scratch, 3u, indices, 3u ).status ==
            polygon_triangulation_status_t::INVALID_ARGUMENT );
    }

    const segment2_intersection_t invalidSegment = Geometry2D_IntersectSegments(
        { Vec2_Make( nan, 0.0f ), Vec2_Make( 1.0f, 0.0f ) },
        { Vec2_Make( 0.0f, -1.0f ), Vec2_Make( 0.0f, 1.0f ) },
        0.000001f );
    REQUIRE( invalidSegment.kind == segment2_intersection_kind_t::NONE );
    REQUIRE( Vec2_IsFinite( invalidSegment.point0 ) );

    const vec2_t invalidPolygon[]{
        Vec2_Make( 0.0f, 0.0f ),
        Vec2_Make( infinity, 0.0f ),
        Vec2_Make( 0.0f, 1.0f )
    };
    REQUIRE( Polygon2_SignedArea( invalidPolygon, 3u ) == 0.0 );
    REQUIRE_FALSE( Polygon2_IsSimple( invalidPolygon, 3u, 0.000001f ) );
    REQUIRE_FALSE( Polygon2_IsConvex( invalidPolygon, 3u, 0.000001 ) );
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
        polygon, 4u, basis, 0.000001, 0.000001,
        projected, 4u, scratch, 4u, indices, 6u );
    REQUIRE( result.status == polygon_triangulation_status_t::OK );
    REQUIRE( result.cTriangles == 2u );

    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const f64 infinity = std::numeric_limits<f64>::infinity();
    REQUIRE_FALSE( Polygon3_IsPlanar( polygon, 4u, basis, nan ) );
    REQUIRE_FALSE( Polygon3_TryAreaCentroid(
        polygon, 4u, basis, infinity, &area, &centroid ) );
    REQUIRE_FALSE( Polygon3_IsConvex(
        polygon, 4u, basis, infinity, projected, 4u ) );
    REQUIRE_FALSE( Polygon3_ContainsPoint(
        polygon, 4u, basis, centroid, 0.000001f, nan, true,
        projected, 4u ) );
    REQUIRE( Polygon3_Triangulate(
        polygon, 4u, basis, infinity, infinity,
        projected, 4u, scratch, 4u, indices, 6u ).status ==
        polygon_triangulation_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "planar 3D binary64 polygons derive a basis, area, and triangulation",
           "[CypherCommon][Mathlib][Editor][Polygon3][Binary64]" )
{
    constexpr vec3d_t polygon[]{
        { 1.0, 2.0, 3.0 },
        { 5.0, 2.0, 3.0 },
        { 5.0, 6.0, 3.0 },
        { 1.0, 6.0, 3.0 }
    };
    polygon3d_basis_t basis{};
    REQUIRE( Polygon3d_TryBasis( polygon, 4u, 1e-9, &basis ) );
    REQUIRE( Polygon3d_IsPlanar( polygon, 4u, basis, 1e-9 ) );

    f64 area = 0.0;
    vec3d_t centroid{};
    REQUIRE( Polygon3d_TryAreaCentroid(
        polygon, 4u, basis, 1e-9, &area, &centroid ) );
    REQUIRE( area == Approx( 16.0 ) );
    RequireVec3d( centroid, 3.0, 4.0, 3.0 );

    planed_t plane{};
    REQUIRE( Polygon3d_TryPlane( polygon, 4u, 1e-9, &plane ) );
    REQUIRE( Planed_SignedDistance( plane, polygon[0] ) ==
             Approx( 0.0 ).margin( 1e-9 ) );

    vec2d_t projected[4]{};
    u32 scratch[4]{};
    u32 indices[6]{};
    const polygon_triangulation_result_t result = Polygon3d_Triangulate(
        polygon, 4u, basis, 1e-9, 1e-9,
        projected, 4u, scratch, 4u, indices, 6u );
    REQUIRE( result.status == polygon_triangulation_status_t::OK );
    REQUIRE( result.cTriangles == 2u );

    REQUIRE( Polygon3d_IsConvex( polygon, 4u, basis, 1e-9, projected, 4u ) );
    REQUIRE( Polygon3d_ContainsPoint(
        polygon, 4u, basis, Vec3d_Make( 3.0, 4.0, 3.0 ),
        1e-9, 1e-9, true, projected, 4u ) );
    REQUIRE_FALSE( Polygon3d_ContainsPoint(
        polygon, 4u, basis, Vec3d_Make( 10.0, 4.0, 3.0 ),
        1e-9, 1e-9, true, projected, 4u ) );
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

    plane_t scaledPlanes[6]{};
    for ( usize i = 0u; i < 6u; ++i ) {
        scaledPlanes[i] = planes[i];
    }
    scaledPlanes[0].normal = Vec3_Scale( scaledPlanes[0].normal, 2.0f );
    scaledPlanes[0].d *= 2.0f;
    REQUIRE_FALSE( Brush_ContainsPoint(
        scaledPlanes, 6u, CY_VEC3_ZERO, 0.0f ) );
    REQUIRE( Brush_BuildVertices(
        scaledPlanes, 6u, 0.000001, 0.00001f, 0.00001f,
        vertices, 20u ).status == brush_build_status_t::INVALID_ARGUMENT );
    vec3_t scaledIntersection = CY_VEC3_ONE;
    REQUIRE_FALSE( Brush_TryIntersectPlanes(
        scaledPlanes[0], scaledPlanes[2], scaledPlanes[4],
        0.000001, &scaledIntersection ) );
    REQUIRE( Vec3_NearlyEquals(
        scaledIntersection, CY_VEC3_ZERO, 0.0f, 0.0f ) );

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

    const f32 invalidTolerances[]{
        std::numeric_limits<f32>::quiet_NaN(),
        std::numeric_limits<f32>::infinity(),
        -1.0f
    };
    for ( f32 invalid : invalidTolerances ) {
        CAPTURE( invalid );
        REQUIRE_FALSE( Brush_ContainsPoint(
            planes, 6u, Vec3_Make( 10.0f, 0.0f, 0.0f ), invalid ) );
        REQUIRE( Brush_BuildVertices(
            planes, 6u, 0.000001, invalid, 0.00001f,
            vertices, 20u ).status == brush_build_status_t::INVALID_ARGUMENT );
        REQUIRE( Brush_BuildVertices(
            planes, 6u, 0.000001, 0.00001f, invalid,
            vertices, 20u ).status == brush_build_status_t::INVALID_ARGUMENT );

        vec3_t face[8]{};
        REQUIRE( Brush_BuildFacePolygon(
            planes[0], vertices, result.cVerticesWritten,
            invalid, 0.000001f, face, 8u ).status ==
            brush_build_status_t::INVALID_ARGUMENT );
    }

    for ( f64 invalid : {
              std::numeric_limits<f64>::quiet_NaN(),
              std::numeric_limits<f64>::infinity(), -1.0 } ) {
        CAPTURE( invalid );
        vec3_t intersection = CY_VEC3_ONE;
        REQUIRE_FALSE( Brush_TryIntersectPlanes(
            planes[0], planes[2], planes[4], invalid, &intersection ) );
        REQUIRE( Vec3_NearlyEquals(
            intersection, CY_VEC3_ZERO, 0.0f, 0.0f ) );
        REQUIRE( Brush_BuildVertices(
            planes, 6u, invalid, 0.00001f, 0.00001f,
            vertices, 20u ).status == brush_build_status_t::INVALID_ARGUMENT );
    }
}

TEST_CASE( "convex brushd planes recover a cube and each face",
           "[CypherCommon][Mathlib][Editor][Brush][Binary64]" )
{
    constexpr planed_t planes[]{
        { { 1.0, 0.0, 0.0 }, -1.0 },
        { { -1.0, 0.0, 0.0 }, -1.0 },
        { { 0.0, 1.0, 0.0 }, -1.0 },
        { { 0.0, -1.0, 0.0 }, -1.0 },
        { { 0.0, 0.0, 1.0 }, -1.0 },
        { { 0.0, 0.0, -1.0 }, -1.0 }
    };
    vec3d_t vertices[20]{};
    const brush_vertex_result_t result = Brushd_BuildVertices(
        planes, 6u, 0.000001, 0.00001, 0.00001, vertices, 20u );
    REQUIRE( result.status == brush_build_status_t::OK );
    REQUIRE( result.cVerticesWritten == 8u );
    REQUIRE( Brushd_ContainsPoint( planes, 6u, CY_VEC3D_ZERO, 0.0 ) );
    REQUIRE_FALSE( Brushd_ContainsPoint(
        planes, 6u, Vec3d_Make( 1.1, 0.0, 0.0 ), 0.0 ) );

    aabbd_t bounds{};
    REQUIRE( Brushd_TryBounds( vertices, result.cVerticesWritten, &bounds ) );
    RequireVec3d( bounds.minimum, -1.0, -1.0, -1.0 );
    RequireVec3d( bounds.maximum, 1.0, 1.0, 1.0 );

    for ( planed_t facePlane : planes ) {
        vec3d_t face[8]{};
        const brush_vertex_result_t faceResult = Brushd_BuildFacePolygon(
            facePlane, vertices, result.cVerticesWritten,
            0.00001, 0.000001, face, 8u );
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
    REQUIRE( Spline_ArcTableIsValid( samples, table.cSamplesWritten ) );
    for ( usize i = 1u; i < table.cSamplesWritten; ++i ) {
        REQUIRE( samples[i].distance >= samples[i - 1u].distance );
    }

    f32 parameter = 0.0f;
    REQUIRE( Spline_TryArcParameterAtDistance(
        samples, table.cSamplesWritten, table.totalLength * 0.5f, &parameter ) );
    REQUIRE( parameter == Approx( 0.5f ).margin( 0.02f ) );
    REQUIRE( Spline_ArcParameterAtDistanceUnchecked(
        samples, table.cSamplesWritten, table.totalLength * 0.5f ) ==
        Approx( parameter ) );

    cubic_bezier3_t nonfiniteCurve = curve;
    nonfiniteCurve.p2.x = std::numeric_limits<f32>::infinity();
    spline_arc_table_result_t invalidResult{ 99u, 99.0f };
    REQUIRE_FALSE( Spline_TryBuildBezierArcTable(
        nonfiniteCurve, 33u, samples, 33u, &invalidResult ) );
    REQUIRE( invalidResult.cSamplesWritten == 0u );
    REQUIRE( invalidResult.totalLength == 0.0f );
}

TEST_CASE( "arc-table lookup rejects malformed serialized tables",
           "[CypherCommon][Mathlib][Editor][Spline][Validation]" )
{
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const spline_arc_sample_t valid[]{
        { 0.0f, 0.0f }, { 0.5f, 2.0f }, { 1.0f, 4.0f }
    };
    REQUIRE( Spline_ArcTableIsValid( valid, 3u ) );

    const spline_arc_sample_t malformed[][3]{
        { { 0.0f, 0.0f }, { 0.5f, nan }, { 1.0f, 4.0f } },
        { { 0.0f, 0.0f }, { 0.5f, 3.0f }, { 1.0f, 2.0f } },
        { { 0.0f, 0.0f }, { 0.75f, 2.0f }, { 0.5f, 4.0f } },
        { { 0.0f, 0.0f }, { 1.25f, 2.0f }, { 1.0f, 4.0f } },
        { { 0.0f, -1.0f }, { 0.5f, 2.0f }, { 1.0f, 4.0f } },
        { { 0.0f, 0.0f }, { 0.5f, 2.0f }, { 0.75f, 4.0f } }
    };
    for ( const auto &table : malformed ) {
        REQUIRE_FALSE( Spline_ArcTableIsValid( table, 3u ) );
        f32 parameter = 123.0f;
        REQUIRE_FALSE( Spline_TryArcParameterAtDistance(
            table, 3u, 1.0f, &parameter ) );
        REQUIRE( parameter == 0.0f );
    }
}
