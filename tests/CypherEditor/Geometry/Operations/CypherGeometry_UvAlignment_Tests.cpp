//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_UvAlignment_Tests.cpp
//  Purpose: Contract tests for UV alignment and justification operations.
//  Details: Verifies Reset, FlipU, FlipV, FitToFace, JustifyU, JustifyV,
//           and AlignToAdjacentFace on known face polygons.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_UvAlignment.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Vec3d_Dot;
using cypher::math::Vec3d_LengthSquared;
using cypher::math::Uvd_TryProjectPlanarPoint;
using Catch::Approx;

namespace {

// A unit-square face on the XY plane at Z=0, normal = +Z.
constexpr math::vec3d_t kSquareXY[4] = {
    { 0.0, 0.0, 0.0 },
    { 1.0, 0.0, 0.0 },
    { 1.0, 1.0, 0.0 },
    { 0.0, 1.0, 0.0 }
};
constexpr math::vec3d_t kNormalPosZ = { 0.0, 0.0, 1.0 };

// A 2×3 rectangle on the XY plane.
constexpr math::vec3d_t kRectXY[4] = {
    { 0.0, 0.0, 0.0 },
    { 2.0, 0.0, 0.0 },
    { 2.0, 3.0, 0.0 },
    { 0.0, 3.0, 0.0 }
};

constexpr common::f64 kMinUvScale = 1.0e-6;

// Helper: projects a world point through a UV mapping and returns the UV.
math::vec2d_t ProjectUv(
    const math::planar_uv_mappingd_t &m,
    math::vec3d_t worldPt )
{
    math::vec2d_t uv{};
    REQUIRE( Uvd_TryProjectPlanarPoint( m, worldPt, kMinUvScale, &uv ) );
    return uv;
}

} // namespace

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE( "UvAlign: Reset produces axis-aligned basis for +Z normal",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    const auto &m = attrs.uvProjection;

    // Normal should be +Z.
    REQUIRE( m.normal.z == Approx( 1.0 ) );

    // U and V axes should be perpendicular to normal and to each other.
    REQUIRE( std::fabs( Vec3d_Dot( m.uAxis, m.normal ) ) ==
             Approx( 0.0 ).margin( 1.0e-12 ) );
    REQUIRE( std::fabs( Vec3d_Dot( m.vAxis, m.normal ) ) ==
             Approx( 0.0 ).margin( 1.0e-12 ) );
    REQUIRE( std::fabs( Vec3d_Dot( m.uAxis, m.vAxis ) ) ==
             Approx( 0.0 ).margin( 1.0e-12 ) );

    // Unit scale, zero rotation, zero offset.
    REQUIRE( m.worldUnitsPerUv.x == Approx( 1.0 ) );
    REQUIRE( m.worldUnitsPerUv.y == Approx( 1.0 ) );
    REQUIRE( m.rotationRadians == Approx( 0.0 ) );
    REQUIRE( m.offset.x == Approx( 0.0 ) );
    REQUIRE( m.offset.y == Approx( 0.0 ) );
}

TEST_CASE( "UvAlign: Reset works for +Y normal",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    const math::vec3d_t normalY = Vec3d_Make( 0.0, 1.0, 0.0 );
    REQUIRE( UvAlign_Reset( &attrs, normalY ) == geometry_status_t::OK );

    const auto &m = attrs.uvProjection;
    REQUIRE( std::fabs( Vec3d_Dot( m.uAxis, m.normal ) ) <
             1.0e-12 );
    REQUIRE( std::fabs( Vec3d_Dot( m.vAxis, m.normal ) ) <
             1.0e-12 );
}

TEST_CASE( "UvAlign: Reset null attribs rejected",
           "[Gate10][UvAlign]" )
{
    REQUIRE( UvAlign_Reset( nullptr, kNormalPosZ ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "UvAlign: Reset degenerate normal rejected",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, Vec3d_Make( 0.0, 0.0, 0.0 ) ) ==
             geometry_status_t::DEGENERATE );
}

// ---------------------------------------------------------------------------
// FlipU / FlipV
// ---------------------------------------------------------------------------

TEST_CASE( "UvAlign: FlipU negates U axis",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    const math::vec3d_t origU = attrs.uvProjection.uAxis;
    REQUIRE( UvAlign_FlipU( &attrs ) == geometry_status_t::OK );

    REQUIRE( attrs.uvProjection.uAxis.x == Approx( -origU.x ) );
    REQUIRE( attrs.uvProjection.uAxis.y == Approx( -origU.y ) );
    REQUIRE( attrs.uvProjection.uAxis.z == Approx( -origU.z ) );
}

TEST_CASE( "UvAlign: FlipU twice restores original",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    const math::vec3d_t origU = attrs.uvProjection.uAxis;
    const common::f64 origOffX = attrs.uvProjection.offset.x;

    REQUIRE( UvAlign_FlipU( &attrs ) == geometry_status_t::OK );
    REQUIRE( UvAlign_FlipU( &attrs ) == geometry_status_t::OK );

    REQUIRE( attrs.uvProjection.uAxis.x == Approx( origU.x ) );
    REQUIRE( attrs.uvProjection.offset.x == Approx( origOffX ) );
}

TEST_CASE( "UvAlign: FlipV negates V axis",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    const math::vec3d_t origV = attrs.uvProjection.vAxis;
    REQUIRE( UvAlign_FlipV( &attrs ) == geometry_status_t::OK );

    REQUIRE( attrs.uvProjection.vAxis.x == Approx( -origV.x ) );
    REQUIRE( attrs.uvProjection.vAxis.y == Approx( -origV.y ) );
    REQUIRE( attrs.uvProjection.vAxis.z == Approx( -origV.z ) );
}

TEST_CASE( "UvAlign: FlipU null rejected",
           "[Gate10][UvAlign]" )
{
    REQUIRE( UvAlign_FlipU( nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// FitToFace
// ---------------------------------------------------------------------------

TEST_CASE( "UvAlign: FitToFace on unit square produces [0,1] UV range",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    REQUIRE( UvAlign_FitToFace( &attrs, kSquareXY, 4u ) ==
             geometry_status_t::OK );

    // All vertices should project into [0, 1].
    for ( int i = 0; i < 4; ++i ) {
        const math::vec2d_t uv = ProjectUv( attrs.uvProjection, kSquareXY[i] );
        REQUIRE( uv.x >= -0.001 );
        REQUIRE( uv.x <= 1.001 );
        REQUIRE( uv.y >= -0.001 );
        REQUIRE( uv.y <= 1.001 );
    }
}

TEST_CASE( "UvAlign: FitToFace on 2x3 rect scales non-uniformly",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    REQUIRE( UvAlign_FitToFace( &attrs, kRectXY, 4u ) ==
             geometry_status_t::OK );

    // Bottom-left should be ~(0,0), top-right should be ~(1,1).
    const math::vec2d_t bl = ProjectUv( attrs.uvProjection, kRectXY[0] );
    const math::vec2d_t tr = ProjectUv( attrs.uvProjection, kRectXY[2] );
    REQUIRE( bl.x == Approx( 0.0 ).margin( 0.001 ) );
    REQUIRE( bl.y == Approx( 0.0 ).margin( 0.001 ) );
    REQUIRE( tr.x == Approx( 1.0 ).margin( 0.001 ) );
    REQUIRE( tr.y == Approx( 1.0 ).margin( 0.001 ) );
}

TEST_CASE( "UvAlign: FitToFace null args rejected",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_FitToFace( nullptr, kSquareXY, 4u ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( UvAlign_FitToFace( &attrs, nullptr, 4u ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "UvAlign: FitToFace fewer than 3 vertices rejected",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );
    REQUIRE( UvAlign_FitToFace( &attrs, kSquareXY, 2u ) ==
             geometry_status_t::DEGENERATE );
}

// ---------------------------------------------------------------------------
// JustifyU / JustifyV
// ---------------------------------------------------------------------------

TEST_CASE( "UvAlign: JustifyU MIN shifts left edge to U=0",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    // Offset the mapping so the face starts at U != 0.
    attrs.uvProjection.offset.x = 5.0;

    REQUIRE( UvAlign_JustifyU( &attrs, kSquareXY, 4u,
                               uv_justify_mode_t::MIN ) ==
             geometry_status_t::OK );

    // Leftmost vertex should now project to U ≈ 0.
    const math::vec2d_t bl = ProjectUv( attrs.uvProjection, kSquareXY[0] );
    REQUIRE( bl.x == Approx( 0.0 ).margin( 0.001 ) );
}

TEST_CASE( "UvAlign: JustifyU MAX shifts right edge to U=1",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    REQUIRE( UvAlign_JustifyU( &attrs, kSquareXY, 4u,
                               uv_justify_mode_t::MAX ) ==
             geometry_status_t::OK );

    // Rightmost vertex should project to U ≈ 1.
    const math::vec2d_t tr = ProjectUv( attrs.uvProjection, kSquareXY[1] );
    REQUIRE( tr.x == Approx( 1.0 ).margin( 0.001 ) );
}

TEST_CASE( "UvAlign: JustifyV CENTER centers the face vertically",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t attrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &attrs, kNormalPosZ ) == geometry_status_t::OK );

    REQUIRE( UvAlign_JustifyV( &attrs, kSquareXY, 4u,
                               uv_justify_mode_t::CENTER ) ==
             geometry_status_t::OK );

    // Face center (0.5, 0.5, 0) should project to V ≈ 0.5.
    const math::vec2d_t center = ProjectUv(
        attrs.uvProjection, Vec3d_Make( 0.5, 0.5, 0.0 ) );
    REQUIRE( center.y == Approx( 0.5 ).margin( 0.001 ) );
}

// ---------------------------------------------------------------------------
// AlignToAdjacentFace
// ---------------------------------------------------------------------------

TEST_CASE( "UvAlign: AlignToAdjacentFace preserves UVs at shared edge",
           "[Gate10][UvAlign]" )
{
    // Reference face: XY plane at Z=0, normal = +Z.
    geometry_brush_side_attributes_t refAttrs = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &refAttrs, kNormalPosZ ) == geometry_status_t::OK );

    // Target face: XZ plane at Y=0, normal = -Y (perpendicular wall).
    geometry_brush_side_attributes_t tgtAttrs = BrushSideAttributes_MakeDefault();
    const math::vec3d_t tgtNormal = Vec3d_Make( 0.0, -1.0, 0.0 );

    // Shared edge runs along X from (0,0,0) to (1,0,0).
    const math::vec3d_t edgeA = Vec3d_Make( 0.0, 0.0, 0.0 );
    const math::vec3d_t edgeB = Vec3d_Make( 1.0, 0.0, 0.0 );

    REQUIRE( UvAlign_AlignToAdjacentFace(
                 &tgtAttrs, tgtNormal, refAttrs,
                 edgeA, edgeB ) == geometry_status_t::OK );

    // The shared edge endpoints should have the same UVs through both
    // mappings.
    const math::vec2d_t refA = ProjectUv( refAttrs.uvProjection, edgeA );
    const math::vec2d_t refB = ProjectUv( refAttrs.uvProjection, edgeB );
    const math::vec2d_t tgtA = ProjectUv( tgtAttrs.uvProjection, edgeA );
    const math::vec2d_t tgtB = ProjectUv( tgtAttrs.uvProjection, edgeB );

    REQUIRE( tgtA.x == Approx( refA.x ).margin( 0.001 ) );
    REQUIRE( tgtA.y == Approx( refA.y ).margin( 0.001 ) );
    REQUIRE( tgtB.x == Approx( refB.x ).margin( 0.001 ) );
    REQUIRE( tgtB.y == Approx( refB.y ).margin( 0.001 ) );
}

TEST_CASE( "UvAlign: AlignToAdjacentFace null target rejected",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t ref = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_AlignToAdjacentFace(
                 nullptr, kNormalPosZ, ref,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 0.0, 0.0 ) ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "UvAlign: AlignToAdjacentFace degenerate edge rejected",
           "[Gate10][UvAlign]" )
{
    geometry_brush_side_attributes_t ref = BrushSideAttributes_MakeDefault();
    REQUIRE( UvAlign_Reset( &ref, kNormalPosZ ) == geometry_status_t::OK );
    geometry_brush_side_attributes_t tgt = BrushSideAttributes_MakeDefault();
    const math::vec3d_t pt = Vec3d_Make( 1.0, 2.0, 3.0 );

    REQUIRE( UvAlign_AlignToAdjacentFace(
                 &tgt, kNormalPosZ, ref, pt, pt ) ==
             geometry_status_t::DEGENERATE );
}

} // namespace cypher::editor::geometry
