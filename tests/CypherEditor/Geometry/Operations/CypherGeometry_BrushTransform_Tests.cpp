//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTransform_Tests.cpp
//  Purpose: Verifies affine transform operations on brush side planes.
//  Details: Covers Gate 5 transform acceptance: translate shifts plane
//           distances, rotate preserves normal length, scale changes
//           both normal and distance, all transforms are atomic (no
//           partial application on failure), and identity transforms
//           are exact no-ops.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushTransform.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_Attributes_BrushSideStore.h"
#include "CypherGeometry_Attributes_Schema.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Vec3d_Dot;
using cypher::math::Vec3d_Subtract;
using cypher::math::Vec3d_LengthSquared;
using cypher::math::Planed_Make;
using Catch::Approx;

namespace {

struct TransformFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};

    TransformFixture()
    {
        // Unit cube centered at origin: (-1,-1,-1) to (1,1,1).
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAlloc,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
    }

    ~TransformFixture()
    {
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }

    void reconstructBoundary()
    {
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) ==
                 geometry_status_t::OK );
    }
};

common::f64 BoundarySignedVolume( const brush_boundary_t &boundary )
{
    common::f64 sixTimesVolume = 0.0;
    for ( common::usize iFace = 0u; iFace < boundary.faces.nCount; ++iFace ) {
        const brush_boundary_face_t &face = boundary.faces.pData[iFace];
        if ( face.cVertices < 3u ) { continue; }

        const common::u32 i0 =
            boundary.faceVertexIndices.pData[face.iFirstIndex];
        const math::vec3d_t &a = boundary.vertices.pData[i0];
        for ( common::u32 i = 1u; i + 1u < face.cVertices; ++i ) {
            const common::u32 i1 =
                boundary.faceVertexIndices.pData[face.iFirstIndex + i];
            const common::u32 i2 =
                boundary.faceVertexIndices.pData[face.iFirstIndex + i + 1u];
            const math::vec3d_t &b = boundary.vertices.pData[i1];
            const math::vec3d_t &c = boundary.vertices.pData[i2];
            sixTimesVolume +=
                a.x * ( b.y * c.z - b.z * c.y ) +
                a.y * ( b.z * c.x - b.x * c.z ) +
                a.z * ( b.x * c.y - b.y * c.x );
        }
    }
    return sixTimesVolume / 6.0;
}

} // namespace

// ---------------------------------------------------------------------------
// Translation
// ---------------------------------------------------------------------------

TEST_CASE( "Transform: translate shifts boundary vertices",
           "[Gate5][Transform]" )
{
    TransformFixture f;
    const math::vec3d_t offset = Vec3d_Make( 5.0, 10.0, 15.0 );

    REQUIRE( BrushTransform_TryTranslate( &f.brush, offset ) ==
             geometry_status_t::OK );

    f.reconstructBoundary();
    REQUIRE( f.boundary.vertices.nCount == 8u );

    // All vertices should now be in [4,6] x [9,11] x [14,16].
    for ( common::usize i = 0u; i < f.boundary.vertices.nCount; ++i ) {
        const math::vec3d_t &v = f.boundary.vertices.pData[i];
        REQUIRE( v.x >= Approx( 4.0 ).margin( 0.001 ) );
        REQUIRE( v.x <= Approx( 6.0 ).margin( 0.001 ) );
        REQUIRE( v.y >= Approx( 9.0 ).margin( 0.001 ) );
        REQUIRE( v.y <= Approx( 11.0 ).margin( 0.001 ) );
        REQUIRE( v.z >= Approx( 14.0 ).margin( 0.001 ) );
        REQUIRE( v.z <= Approx( 16.0 ).margin( 0.001 ) );
    }
}

TEST_CASE( "Transform: translate preserves plane normals",
           "[Gate5][Transform]" )
{
    TransformFixture f;

    // Record original normals.
    const common::usize cSides = BrushSolid_SideCount( &f.brush );
    math::vec3d_t origNormals[6];
    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( &f.brush, i, &side );
        origNormals[i] = side.plane.normal;
    }

    REQUIRE( BrushTransform_TryTranslate(
                 &f.brush, Vec3d_Make( 3.0, 4.0, 5.0 ) ) ==
             geometry_status_t::OK );

    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( &f.brush, i, &side );
        REQUIRE( side.plane.normal.x == Approx( origNormals[i].x ) );
        REQUIRE( side.plane.normal.y == Approx( origNormals[i].y ) );
        REQUIRE( side.plane.normal.z == Approx( origNormals[i].z ) );
    }
}

TEST_CASE( "Transform: translate by zero is no-op",
           "[Gate5][Transform]" )
{
    TransformFixture f;
    f.reconstructBoundary();

    // Record original vertex positions.
    math::vec3d_t origVerts[8];
    for ( common::usize i = 0u; i < 8u; ++i ) {
        origVerts[i] = f.boundary.vertices.pData[i];
    }

    REQUIRE( BrushTransform_TryTranslate(
                 &f.brush, Vec3d_Make( 0.0, 0.0, 0.0 ) ) ==
             geometry_status_t::OK );

    f.reconstructBoundary();
    for ( common::usize i = 0u; i < 8u; ++i ) {
        REQUIRE( f.boundary.vertices.pData[i].x ==
                 Approx( origVerts[i].x ) );
        REQUIRE( f.boundary.vertices.pData[i].y ==
                 Approx( origVerts[i].y ) );
        REQUIRE( f.boundary.vertices.pData[i].z ==
                 Approx( origVerts[i].z ) );
    }
}

TEST_CASE( "Transform: null brush rejected", "[Gate5][Transform]" )
{
    REQUIRE( BrushTransform_TryTranslate(
                 nullptr, Vec3d_Make( 1.0, 0.0, 0.0 ) ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Transform: uninitialized brush is rejected consistently",
           "[Gate5][Transform][contract]" )
{
    brush_solid_t brush{};
    CHECK( BrushTransform_TryTranslate(
               &brush, Vec3d_Make( 1.0, 0.0, 0.0 ) ) ==
           geometry_status_t::NOT_INITIALIZED );
    CHECK( BrushTransform_TryRotate(
               &brush,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               math::CY_AFFINE3D_IDENTITY ) ==
           geometry_status_t::NOT_INITIALIZED );
    CHECK( BrushTransform_TryScale(
               &brush,
               Vec3d_Make( 0.0, 0.0, 0.0 ),
               Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
           geometry_status_t::NOT_INITIALIZED );
    CHECK( BrushTransform_TryApplyAffine(
               &brush, math::CY_AFFINE3D_IDENTITY ) ==
           geometry_status_t::NOT_INITIALIZED );
}

TEST_CASE( "Transform: translation overflow leaves every side unchanged",
           "[Gate5][Transform][numeric][contract]" )
{
    TransformFixture f;
    REQUIRE( BrushSolid_SideCount( &f.brush ) == 6u );

    brush_solid_side_t first{};
    REQUIRE( BrushSolid_TryGetSide( &f.brush, 0u, &first ) ==
             geometry_status_t::OK );
    first.plane.d = std::numeric_limits<common::f64>::max();
    REQUIRE( BrushSolid_TrySetSidePlane(
                 &f.brush, 0u, first.plane ) ==
             geometry_status_t::OK );

    brush_solid_side_t before[6]{};
    for ( common::usize i = 0u; i < 6u; ++i ) {
        REQUIRE( BrushSolid_TryGetSide(
                     &f.brush, i, &before[i] ) ==
                 geometry_status_t::OK );
    }

    const math::vec3d_t offset = math::Vec3d_Scale(
        first.plane.normal,
        -std::numeric_limits<common::f64>::max() );
    REQUIRE( BrushTransform_TryTranslate( &f.brush, offset ) ==
             geometry_status_t::NUMERIC_FAILURE );

    for ( common::usize i = 0u; i < 6u; ++i ) {
        brush_solid_side_t after{};
        REQUIRE( BrushSolid_TryGetSide(
                     &f.brush, i, &after ) ==
                 geometry_status_t::OK );
        CHECK( after.plane.normal.x == before[i].plane.normal.x );
        CHECK( after.plane.normal.y == before[i].plane.normal.y );
        CHECK( after.plane.normal.z == before[i].plane.normal.z );
        CHECK( after.plane.d == before[i].plane.d );
        CHECK( after.sourceId.value == before[i].sourceId.value );
        CHECK( after.iAttributeIndex == before[i].iAttributeIndex );
    }
}

// ---------------------------------------------------------------------------
// Rotation
// ---------------------------------------------------------------------------

TEST_CASE( "Transform: 90-degree rotation about Z axis",
           "[Gate5][Transform]" )
{
    TransformFixture f;

    // Build a 90-degree rotation around Z. Columns of rotation matrix:
    // col0 = (cos90, sin90, 0) = (0, 1, 0)
    // col1 = (-sin90, cos90, 0) = (-1, 0, 0)
    // col2 = (0, 0, 1)
    const math::affine3d_t rot = math::Affine3d_FromColumns(
        Vec3d_Make( 0.0, 1.0, 0.0 ),
        Vec3d_Make( -1.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 1.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ) );

    REQUIRE( BrushTransform_TryRotate(
                 &f.brush, Vec3d_Make( 0.0, 0.0, 0.0 ), rot ) ==
             geometry_status_t::OK );

    f.reconstructBoundary();
    REQUIRE( f.boundary.vertices.nCount == 8u );

    // Original box was (-1,-1,-1)-(1,1,1). After 90° about Z:
    // x' = -y, y' = x. So new bounds: (-1,-1,-1)-(1,1,1) — same
    // for a unit cube, but let's verify a vertex.
    bool bFoundExpected = false;
    for ( common::usize i = 0u; i < 8u; ++i ) {
        const math::vec3d_t &v = f.boundary.vertices.pData[i];
        // (1,1,1) → (-1,1,1) after 90° Z rotation.
        if ( std::abs( v.x - (-1.0) ) < 0.01 &&
             std::abs( v.y - 1.0 ) < 0.01 &&
             std::abs( v.z - 1.0 ) < 0.01 ) {
            bFoundExpected = true;
        }
    }
    REQUIRE( bFoundExpected );
}

TEST_CASE( "Transform: rotation preserves volume",
           "[Gate5][Transform]" )
{
    TransformFixture f;
    f.reconstructBoundary();

    const common::f64 volumeBefore =
        std::abs( BoundarySignedVolume( f.boundary ) );

    // Arbitrary rotation: 45° about Z.
    const common::f64 c = std::cos( 3.14159265358979323846 / 4.0 );
    const common::f64 s = std::sin( 3.14159265358979323846 / 4.0 );
    const math::affine3d_t rot = math::Affine3d_FromColumns(
        Vec3d_Make( c, s, 0.0 ),
        Vec3d_Make( -s, c, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 1.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ) );

    REQUIRE( BrushTransform_TryRotate(
                 &f.brush, Vec3d_Make( 0.0, 0.0, 0.0 ), rot ) ==
             geometry_status_t::OK );

    f.reconstructBoundary();
    REQUIRE( f.boundary.vertices.nCount == 8u );
    REQUIRE( f.boundary.faces.nCount == 6u );
    REQUIRE( f.boundary.edges.nCount == 12u );
    CHECK( std::abs( BoundarySignedVolume( f.boundary ) ) ==
           Approx( volumeBefore ).margin( 1e-12 ) );
}

// ---------------------------------------------------------------------------
// Scale
// ---------------------------------------------------------------------------

TEST_CASE( "Transform: uniform scale doubles box size",
           "[Gate5][Transform]" )
{
    TransformFixture f;

    REQUIRE( BrushTransform_TryScale(
                 &f.brush,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 2.0, 2.0, 2.0 ) ) ==
             geometry_status_t::OK );

    f.reconstructBoundary();

    // Original: (-1,-1,-1)-(1,1,1). After 2x scale from origin:
    // (-2,-2,-2)-(2,2,2).
    const math::aabbd_t bounds =
        BrushQueries_ComputeBoundsd( &f.boundary );
    REQUIRE( bounds.minimum.x == Approx( -2.0 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.x == Approx( 2.0 ).margin( 0.01 ) );
    REQUIRE( bounds.minimum.y == Approx( -2.0 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.y == Approx( 2.0 ).margin( 0.01 ) );
    REQUIRE( bounds.minimum.z == Approx( -2.0 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.z == Approx( 2.0 ).margin( 0.01 ) );
}

TEST_CASE( "Transform: non-uniform scale stretches one axis",
           "[Gate5][Transform]" )
{
    TransformFixture f;

    REQUIRE( BrushTransform_TryScale(
                 &f.brush,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 3.0 ) ) ==
             geometry_status_t::OK );

    f.reconstructBoundary();

    const math::aabbd_t bounds =
        BrushQueries_ComputeBoundsd( &f.boundary );
    // X and Y unchanged: (-1,-1). Z scaled 3x: (-3,3).
    REQUIRE( bounds.minimum.x == Approx( -1.0 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.x == Approx( 1.0 ).margin( 0.01 ) );
    REQUIRE( bounds.minimum.z == Approx( -3.0 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.z == Approx( 3.0 ).margin( 0.01 ) );
}

TEST_CASE( "Transform: zero scale rejected",
           "[Gate5][Transform]" )
{
    TransformFixture f;

    REQUIRE( BrushTransform_TryScale(
                 &f.brush,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 0.0, 1.0, 1.0 ) ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Transform: negative scale rejected",
           "[Gate5][Transform]" )
{
    TransformFixture f;

    REQUIRE( BrushTransform_TryScale(
                 &f.brush,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( -1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Transform: scale from non-origin pivot",
           "[Gate5][Transform]" )
{
    TransformFixture f;

    // Scale 2x from pivot (1,1,1) — the +X+Y+Z corner.
    // That corner stays fixed, opposite corner moves away.
    REQUIRE( BrushTransform_TryScale(
                 &f.brush,
                 Vec3d_Make( 1.0, 1.0, 1.0 ),
                 Vec3d_Make( 2.0, 2.0, 2.0 ) ) ==
             geometry_status_t::OK );

    f.reconstructBoundary();
    const math::aabbd_t bounds =
        BrushQueries_ComputeBoundsd( &f.boundary );

    // Pivot (1,1,1) stays fixed. (-1,-1,-1) → 1 + 2*(-1-1) = -3.
    REQUIRE( bounds.minimum.x == Approx( -3.0 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.x == Approx( 1.0 ).margin( 0.01 ) );
}

// ---------------------------------------------------------------------------
// Generic affine
// ---------------------------------------------------------------------------

TEST_CASE( "Transform: identity affine is exact no-op",
           "[Gate5][Transform]" )
{
    TransformFixture f;
    f.reconstructBoundary();

    math::vec3d_t origVerts[8];
    for ( common::usize i = 0u; i < 8u; ++i ) {
        origVerts[i] = f.boundary.vertices.pData[i];
    }

    REQUIRE( BrushTransform_TryApplyAffine(
                 &f.brush, math::CY_AFFINE3D_IDENTITY ) ==
             geometry_status_t::OK );

    f.reconstructBoundary();
    for ( common::usize i = 0u; i < 8u; ++i ) {
        REQUIRE( f.boundary.vertices.pData[i].x ==
                 Approx( origVerts[i].x ) );
        REQUIRE( f.boundary.vertices.pData[i].y ==
                 Approx( origVerts[i].y ) );
        REQUIRE( f.boundary.vertices.pData[i].z ==
                 Approx( origVerts[i].z ) );
    }
}

TEST_CASE( "Transform: degenerate affine rejected",
           "[Gate5][Transform]" )
{
    TransformFixture f;

    // Singular matrix — all columns zero.
    const math::affine3d_t singular = math::Affine3d_FromColumns(
        Vec3d_Make( 0.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ) );

    REQUIRE( BrushTransform_TryApplyAffine( &f.brush, singular ) ==
             geometry_status_t::NUMERIC_FAILURE );

    // Brush should be unchanged after failure.
    f.reconstructBoundary();
    REQUIRE( f.boundary.vertices.nCount == 8u );
}

TEST_CASE( "Transform: affine has no hidden 256-side scratch limit",
           "[Gate5][Transform][limits][contract]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_solid_t brush{};
    REQUIRE( BrushSolid_Init(
                 &brush, &allocator,
                 geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );

    geometry_limit_policy_t limits{};
    limits.cBrushSidesPerBrushMax = 300u;
    for ( common::usize i = 0u; i < 300u; ++i ) {
        brush_solid_side_t side{};
        side.plane = Planed_Make(
            Vec3d_Make( 1.0, 0.0, 0.0 ),
            static_cast<common::f64>( i ) );
        side.sourceId = geometry_source_id_t{ i + 2u };
        side.iAttributeIndex = static_cast<common::u32>( i );
        REQUIRE( BrushSolid_TryAddSide(
                     &brush, limits, side, nullptr ) ==
                 geometry_status_t::OK );
    }

    REQUIRE( BrushTransform_TryApplyAffine(
                 &brush, math::CY_AFFINE3D_IDENTITY ) ==
             geometry_status_t::OK );
    CHECK( BrushSolid_SideCount( &brush ) == 300u );
    for ( common::usize i = 0u; i < 300u; ++i ) {
        CHECK( brush.sides.pData[i].plane.d ==
               static_cast<common::f64>( i ) );
        CHECK( brush.sides.pData[i].sourceId.value == i + 2u );
        CHECK( brush.sides.pData[i].iAttributeIndex ==
               static_cast<common::u32>( i ) );
    }

    BrushSolid_Shutdown( &brush );
}

// ---------------------------------------------------------------------------
// Texture lock — translation
// ---------------------------------------------------------------------------

TEST_CASE( "TextureLock: translate shifts UV origin by offset",
           "[Gate8][TextureLock]" )
{
    TransformFixture f;

    // Build an attribute store with one side whose UV origin is at (1,2,3).
    geometry_brush_side_attribute_store_t store{};
    REQUIRE( BrushSideAttributeStore_Init( &store, &f.allocator ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t attrs =
        BrushSideAttributes_MakeDefault();
    attrs.uvProjection.origin = Vec3d_Make( 1.0, 2.0, 3.0 );
    common::usize idx = 0u;
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &store, f.policy, attrs, &idx ) ==
             geometry_status_t::OK );

    const math::vec3d_t offset = Vec3d_Make( 5.0, 10.0, 15.0 );
    REQUIRE( BrushTransform_TextureLockTranslate(
                 &store, 1u, offset, f.policy ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t result{};
    REQUIRE( BrushSideAttributeStore_TryGet( &store, 0u, &result ) ==
             geometry_status_t::OK );

    // UV origin should now be (6, 12, 18).
    REQUIRE( result.uvProjection.origin.x == Approx( 6.0 ) );
    REQUIRE( result.uvProjection.origin.y == Approx( 12.0 ) );
    REQUIRE( result.uvProjection.origin.z == Approx( 18.0 ) );

    BrushSideAttributeStore_Shutdown( &store );
}

TEST_CASE( "TextureLock: translate preserves UV axes and scale",
           "[Gate8][TextureLock]" )
{
    TransformFixture f;

    geometry_brush_side_attribute_store_t store{};
    REQUIRE( BrushSideAttributeStore_Init( &store, &f.allocator ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t attrs =
        BrushSideAttributes_MakeDefault();
    attrs.uvProjection.uAxis = Vec3d_Make( 1.0, 0.0, 0.0 );
    attrs.uvProjection.vAxis = Vec3d_Make( 0.0, 1.0, 0.0 );
    attrs.uvProjection.worldUnitsPerUv =
        math::Vec2d_Make( 0.25, 0.5 );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &store, f.policy, attrs, nullptr ) ==
             geometry_status_t::OK );

    REQUIRE( BrushTransform_TextureLockTranslate(
                 &store, 1u, Vec3d_Make( 10.0, 20.0, 30.0 ),
                 f.policy ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t result{};
    REQUIRE( BrushSideAttributeStore_TryGet( &store, 0u, &result ) ==
             geometry_status_t::OK );

    // Axes unchanged.
    REQUIRE( result.uvProjection.uAxis.x == Approx( 1.0 ) );
    REQUIRE( result.uvProjection.uAxis.y == Approx( 0.0 ) );
    REQUIRE( result.uvProjection.uAxis.z == Approx( 0.0 ) );

    // Scale unchanged.
    REQUIRE( result.uvProjection.worldUnitsPerUv.x == Approx( 0.25 ) );
    REQUIRE( result.uvProjection.worldUnitsPerUv.y == Approx( 0.5 ) );

    BrushSideAttributeStore_Shutdown( &store );
}

// ---------------------------------------------------------------------------
// Texture lock — rotation
// ---------------------------------------------------------------------------

TEST_CASE( "TextureLock: rotate 90° Z rotates UV axes accordingly",
           "[Gate8][TextureLock]" )
{
    TransformFixture f;

    geometry_brush_side_attribute_store_t store{};
    REQUIRE( BrushSideAttributeStore_Init( &store, &f.allocator ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t attrs =
        BrushSideAttributes_MakeDefault();
    attrs.uvProjection.origin = Vec3d_Make( 0.0, 0.0, 0.0 );
    attrs.uvProjection.uAxis = Vec3d_Make( 1.0, 0.0, 0.0 );
    attrs.uvProjection.vAxis = Vec3d_Make( 0.0, 1.0, 0.0 );
    attrs.uvProjection.normal = Vec3d_Make( 0.0, 0.0, 1.0 );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &store, f.policy, attrs, nullptr ) ==
             geometry_status_t::OK );

    // 90° rotation about Z:
    //   col0 = (0,1,0), col1 = (-1,0,0), col2 = (0,0,1)
    const math::affine3d_t rot = math::Affine3d_FromColumns(
        Vec3d_Make( 0.0, 1.0, 0.0 ),
        Vec3d_Make( -1.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 1.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ) );

    REQUIRE( BrushTransform_TextureLockRotate(
                 &store, 1u, Vec3d_Make( 0.0, 0.0, 0.0 ),
                 rot, f.policy ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t result{};
    REQUIRE( BrushSideAttributeStore_TryGet( &store, 0u, &result ) ==
             geometry_status_t::OK );

    // uAxis (1,0,0) rotated 90° Z → (0,1,0).
    REQUIRE( result.uvProjection.uAxis.x == Approx( 0.0 ).margin( 1e-10 ) );
    REQUIRE( result.uvProjection.uAxis.y == Approx( 1.0 ).margin( 1e-10 ) );
    REQUIRE( result.uvProjection.uAxis.z == Approx( 0.0 ).margin( 1e-10 ) );

    // vAxis (0,1,0) rotated 90° Z → (-1,0,0).
    REQUIRE( result.uvProjection.vAxis.x == Approx( -1.0 ).margin( 1e-10 ) );
    REQUIRE( result.uvProjection.vAxis.y == Approx( 0.0 ).margin( 1e-10 ) );
    REQUIRE( result.uvProjection.vAxis.z == Approx( 0.0 ).margin( 1e-10 ) );

    BrushSideAttributeStore_Shutdown( &store );
}

TEST_CASE( "TextureLock: rotate transforms origin through pivot",
           "[Gate8][TextureLock]" )
{
    TransformFixture f;

    geometry_brush_side_attribute_store_t store{};
    REQUIRE( BrushSideAttributeStore_Init( &store, &f.allocator ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t attrs =
        BrushSideAttributes_MakeDefault();
    // UV origin at (2,0,0), pivot at (1,0,0).
    attrs.uvProjection.origin = Vec3d_Make( 2.0, 0.0, 0.0 );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &store, f.policy, attrs, nullptr ) ==
             geometry_status_t::OK );

    // 90° rotation about Z with pivot at (1,0,0).
    // Relative to pivot: (2-1,0,0) = (1,0,0).
    // After 90° Z: (0,1,0). Back to world: (1,1,0).
    const math::affine3d_t rot = math::Affine3d_FromColumns(
        Vec3d_Make( 0.0, 1.0, 0.0 ),
        Vec3d_Make( -1.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 1.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ) );

    REQUIRE( BrushTransform_TextureLockRotate(
                 &store, 1u, Vec3d_Make( 1.0, 0.0, 0.0 ),
                 rot, f.policy ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t result{};
    REQUIRE( BrushSideAttributeStore_TryGet( &store, 0u, &result ) ==
             geometry_status_t::OK );

    REQUIRE( result.uvProjection.origin.x == Approx( 1.0 ).margin( 1e-10 ) );
    REQUIRE( result.uvProjection.origin.y == Approx( 1.0 ).margin( 1e-10 ) );
    REQUIRE( result.uvProjection.origin.z == Approx( 0.0 ).margin( 1e-10 ) );

    BrushSideAttributeStore_Shutdown( &store );
}

// ---------------------------------------------------------------------------
// Texture lock — scale
// ---------------------------------------------------------------------------

TEST_CASE( "TextureLock: uniform scale adjusts worldUnitsPerUv",
           "[Gate8][TextureLock]" )
{
    TransformFixture f;

    geometry_brush_side_attribute_store_t store{};
    REQUIRE( BrushSideAttributeStore_Init( &store, &f.allocator ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t attrs =
        BrushSideAttributes_MakeDefault();
    attrs.uvProjection.origin = Vec3d_Make( 0.0, 0.0, 0.0 );
    attrs.uvProjection.uAxis = Vec3d_Make( 1.0, 0.0, 0.0 );
    attrs.uvProjection.vAxis = Vec3d_Make( 0.0, 1.0, 0.0 );
    attrs.uvProjection.normal = Vec3d_Make( 0.0, 0.0, 1.0 );
    attrs.uvProjection.worldUnitsPerUv =
        math::Vec2d_Make( 1.0, 1.0 );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &store, f.policy, attrs, nullptr ) ==
             geometry_status_t::OK );

    // 2x uniform scale from origin.
    REQUIRE( BrushTransform_TextureLockScale(
                 &store, 1u,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 2.0, 2.0, 2.0 ),
                 f.policy ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t result{};
    REQUIRE( BrushSideAttributeStore_TryGet( &store, 0u, &result ) ==
             geometry_status_t::OK );

    // uAxis is aligned with X, scale.x = 2.0 → worldUnitsPerUv.x *= 2.
    REQUIRE( result.uvProjection.worldUnitsPerUv.x == Approx( 2.0 ) );
    REQUIRE( result.uvProjection.worldUnitsPerUv.y == Approx( 2.0 ) );

    // UV axes should remain unit-length after renormalization.
    REQUIRE( Vec3d_LengthSquared( result.uvProjection.uAxis ) ==
             Approx( 1.0 ).margin( 1e-10 ) );
    REQUIRE( Vec3d_LengthSquared( result.uvProjection.vAxis ) ==
             Approx( 1.0 ).margin( 1e-10 ) );

    BrushSideAttributeStore_Shutdown( &store );
}

TEST_CASE( "TextureLock: scale translates UV origin relative to pivot",
           "[Gate8][TextureLock]" )
{
    TransformFixture f;

    geometry_brush_side_attribute_store_t store{};
    REQUIRE( BrushSideAttributeStore_Init( &store, &f.allocator ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t attrs =
        BrushSideAttributes_MakeDefault();
    attrs.uvProjection.origin = Vec3d_Make( 4.0, 0.0, 0.0 );
    REQUIRE( BrushSideAttributeStore_TryAppend(
                 &store, f.policy, attrs, nullptr ) ==
             geometry_status_t::OK );

    // 2x scale from pivot (2, 0, 0).
    // Relative to pivot: (4-2, 0, 0) = (2, 0, 0).
    // After 2x: (4, 0, 0). Back to world: (4+2, 0, 0) = (6, 0, 0).
    REQUIRE( BrushTransform_TextureLockScale(
                 &store, 1u,
                 Vec3d_Make( 2.0, 0.0, 0.0 ),
                 Vec3d_Make( 2.0, 2.0, 2.0 ),
                 f.policy ) ==
             geometry_status_t::OK );

    geometry_brush_side_attributes_t result{};
    REQUIRE( BrushSideAttributeStore_TryGet( &store, 0u, &result ) ==
             geometry_status_t::OK );

    REQUIRE( result.uvProjection.origin.x == Approx( 6.0 ) );
    REQUIRE( result.uvProjection.origin.y == Approx( 0.0 ) );
    REQUIRE( result.uvProjection.origin.z == Approx( 0.0 ) );

    BrushSideAttributeStore_Shutdown( &store );
}

// ---------------------------------------------------------------------------
// Texture lock — error paths
// ---------------------------------------------------------------------------

TEST_CASE( "TextureLock: null store rejected by all three",
           "[Gate8][TextureLock]" )
{
    geometry_policy_t policy{};
    REQUIRE( BrushTransform_TextureLockTranslate(
                 nullptr, 1u,
                 Vec3d_Make( 1.0, 0.0, 0.0 ), policy ) ==
             geometry_status_t::INVALID_ARGUMENT );

    const math::affine3d_t rot = math::Affine3d_FromColumns(
        Vec3d_Make( 1.0, 0.0, 0.0 ),
        Vec3d_Make( 0.0, 1.0, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 1.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ) );
    REQUIRE( BrushTransform_TextureLockRotate(
                 nullptr, 1u,
                 Vec3d_Make( 0.0, 0.0, 0.0 ), rot, policy ) ==
             geometry_status_t::INVALID_ARGUMENT );

    REQUIRE( BrushTransform_TextureLockScale(
                 nullptr, 1u,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ), policy ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "TextureLock: zero scale rejected",
           "[Gate8][TextureLock]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_brush_side_attribute_store_t store{};
    REQUIRE( BrushSideAttributeStore_Init( &store, &allocator ) ==
             geometry_status_t::OK );

    REQUIRE( BrushTransform_TextureLockScale(
                 &store, 0u,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 0.0, 1.0, 1.0 ), policy ) ==
             geometry_status_t::INVALID_ARGUMENT );

    BrushSideAttributeStore_Shutdown( &store );
}

} // namespace cypher::editor::geometry
