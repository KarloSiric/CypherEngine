//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTessellation_Tests.cpp
//  Purpose: Verifies fan triangulation of brush boundary faces.
//  Details: Covers Gate 4 tessellation acceptance: 12 triangles from a
//           box boundary, source-side provenance, outward winding, vertex
//           index validity, rebuild after plane edit, and error paths.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushTessellation.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Vec3d_Dot;
using cypher::math::Vec3d_Cross;
using cypher::math::Vec3d_Subtract;
using cypher::math::Planed_Make;
using Catch::Approx;

namespace {

struct TessellationFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    brush_tessellation_t tessellation{};

    TessellationFixture()
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAlloc,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushTessellation_Init(
                     &tessellation, &allocator ) ==
                 geometry_status_t::OK );
    }

    ~TessellationFixture()
    {
        BrushTessellation_Shutdown( &tessellation );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Box tessellation
// ---------------------------------------------------------------------------

TEST_CASE( "Tessellation: box produces 12 triangles",
           "[Gate4][Tessellation]" )
{
    TessellationFixture f;
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );

    // 6 quad faces → 12 triangles (2 per face).
    REQUIRE( BrushTessellation_TriangleCount( &f.tessellation ) == 12u );
    REQUIRE( BrushTessellation_VertexCount( &f.tessellation ) == 8u );
}

TEST_CASE( "Tessellation: every triangle has valid vertex indices",
           "[Gate4][Tessellation]" )
{
    TessellationFixture f;
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );

    const common::usize cTris =
        BrushTessellation_TriangleCount( &f.tessellation );
    const common::usize cVerts =
        BrushTessellation_VertexCount( &f.tessellation );

    for ( common::usize i = 0u; i < cTris; ++i ) {
        brush_tessellation_triangle_t tri{};
        REQUIRE( BrushTessellation_TryGetTriangle(
                     &f.tessellation, i, &tri ) ==
                 geometry_status_t::OK );
        REQUIRE( tri.iVertex0 < cVerts );
        REQUIRE( tri.iVertex1 < cVerts );
        REQUIRE( tri.iVertex2 < cVerts );
    }
}

TEST_CASE( "Tessellation: every triangle maps to one source side",
           "[Gate4][Tessellation]" )
{
    TessellationFixture f;
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );

    const common::usize cTris =
        BrushTessellation_TriangleCount( &f.tessellation );
    const common::usize cSides = BrushSolid_SideCount( &f.brush );

    for ( common::usize i = 0u; i < cTris; ++i ) {
        brush_tessellation_triangle_t tri{};
        REQUIRE( BrushTessellation_TryGetTriangle(
                     &f.tessellation, i, &tri ) ==
                 geometry_status_t::OK );
        REQUIRE( tri.iSourceSide < cSides );
    }
}

TEST_CASE( "Tessellation: every triangle points outward",
           "[Gate4][Tessellation]" )
{
    TessellationFixture f;
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );

    const common::usize cTris =
        BrushTessellation_TriangleCount( &f.tessellation );

    for ( common::usize i = 0u; i < cTris; ++i ) {
        brush_tessellation_triangle_t tri{};
        REQUIRE( BrushTessellation_TryGetTriangle(
                     &f.tessellation, i, &tri ) ==
                 geometry_status_t::OK );

        // Get the side's outward normal.
        brush_solid_side_t side{};
        REQUIRE( BrushSolid_TryGetSide(
                     &f.brush, tri.iSourceSide, &side ) ==
                 geometry_status_t::OK );

        // Compute the triangle normal from CCW winding.
        const math::vec3d_t &v0 =
            f.boundary.vertices.pData[tri.iVertex0];
        const math::vec3d_t &v1 =
            f.boundary.vertices.pData[tri.iVertex1];
        const math::vec3d_t &v2 =
            f.boundary.vertices.pData[tri.iVertex2];

        const math::vec3d_t edge1 = Vec3d_Subtract( v1, v0 );
        const math::vec3d_t edge2 = Vec3d_Subtract( v2, v0 );
        const math::vec3d_t triNormal = Vec3d_Cross( edge1, edge2 );

        // The triangle's normal should align with the side's outward
        // normal (positive dot product).
        const common::f64 dot =
            Vec3d_Dot( triNormal, side.plane.normal );
        REQUIRE( dot > 0.0 );
    }
}

TEST_CASE( "Tessellation: each side produces exactly 2 triangles for a box",
           "[Gate4][Tessellation]" )
{
    TessellationFixture f;
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );

    const common::usize cSides = BrushSolid_SideCount( &f.brush );
    const common::usize cTris =
        BrushTessellation_TriangleCount( &f.tessellation );

    // Count triangles per side.
    for ( common::usize s = 0u; s < cSides; ++s ) {
        common::usize count = 0u;
        for ( common::usize i = 0u; i < cTris; ++i ) {
            brush_tessellation_triangle_t tri{};
            (void)BrushTessellation_TryGetTriangle(
                &f.tessellation, i, &tri );
            if ( tri.iSourceSide == static_cast<common::u32>( s ) ) {
                ++count;
            }
        }
        REQUIRE( count == 2u );
    }
}

TEST_CASE( "Tessellation: rebuild after plane edit updates triangles",
           "[Gate4][Tessellation]" )
{
    TessellationFixture f;
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );
    REQUIRE( BrushTessellation_TriangleCount( &f.tessellation ) == 12u );

    // Read side 0's current normal and push it outward slightly.
    brush_solid_side_t side0{};
    REQUIRE( BrushSolid_TryGetSide( &f.brush, 0u, &side0 ) ==
             geometry_status_t::OK );
    (void)BrushSolid_TrySetSidePlane(
        &f.brush, 0u,
        Planed_Make( side0.plane.normal, side0.plane.d + 0.5 ) );

    // Reconstruct boundary and retessellate.
    REQUIRE( BrushBoundary_TryReconstruct(
                 &f.boundary, &f.brush, f.policy ) ==
             geometry_status_t::OK );
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );

    // Still 12 triangles — topology unchanged, only positions differ.
    REQUIRE( BrushTessellation_TriangleCount( &f.tessellation ) == 12u );
}

// ---------------------------------------------------------------------------
// Error paths
// ---------------------------------------------------------------------------

TEST_CASE( "Tessellation: double init rejected",
           "[Gate4][Tessellation]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_tessellation_t tess{};
    REQUIRE( BrushTessellation_Init( &tess, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushTessellation_Init( &tess, &allocator ) ==
             geometry_status_t::ALREADY_INITIALIZED );
    BrushTessellation_Shutdown( &tess );
}

TEST_CASE( "Tessellation: null arguments rejected",
           "[Gate4][Tessellation]" )
{
    REQUIRE( BrushTessellation_Init( nullptr, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );

    brush_tessellation_t tess{};
    REQUIRE( BrushTessellation_TryBuild( &tess, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Tessellation: build without init fails",
           "[Gate4][Tessellation]" )
{
    TessellationFixture f;
    brush_tessellation_t uninit{};
    REQUIRE( BrushTessellation_TryBuild( &uninit, &f.boundary ) ==
             geometry_status_t::NOT_INITIALIZED );
}

TEST_CASE( "Tessellation: uninitialized boundary is rejected",
           "[Gate4][Tessellation][contract]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_tessellation_t tess{};
    REQUIRE( BrushTessellation_Init( &tess, &allocator ) ==
             geometry_status_t::OK );

    brush_boundary_t boundary{};
    REQUIRE( BrushTessellation_TryBuild( &tess, &boundary ) ==
             geometry_status_t::NOT_INITIALIZED );
    CHECK( BrushTessellation_TriangleCount( &tess ) == 0u );
    CHECK( BrushTessellation_VertexCount( &tess ) == 0u );

    BrushTessellation_Shutdown( &tess );
}

TEST_CASE( "Tessellation: corrupt packed face range clears prior output",
           "[Gate4][Tessellation][contract]" )
{
    TessellationFixture f;
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );
    REQUIRE( BrushTessellation_TriangleCount( &f.tessellation ) == 12u );

    f.boundary.faces.pData[0].iFirstIndex =
        static_cast<common::u32>(
            f.boundary.faceVertexIndices.nCount + 1u );
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::CORRUPT_STATE );
    CHECK( BrushTessellation_TriangleCount( &f.tessellation ) == 0u );
    CHECK( BrushTessellation_VertexCount( &f.tessellation ) == 0u );
}

TEST_CASE( "Tessellation: out-of-range boundary vertex is rejected",
           "[Gate4][Tessellation][contract]" )
{
    TessellationFixture f;
    f.boundary.faceVertexIndices.pData[0] =
        static_cast<common::u32>( f.boundary.vertices.nCount );

    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::CORRUPT_STATE );
    CHECK( BrushTessellation_TriangleCount( &f.tessellation ) == 0u );
    CHECK( BrushTessellation_VertexCount( &f.tessellation ) == 0u );
}

TEST_CASE( "Tessellation: get triangle out of range",
           "[Gate4][Tessellation]" )
{
    TessellationFixture f;
    REQUIRE( BrushTessellation_TryBuild(
                 &f.tessellation, &f.boundary ) ==
             geometry_status_t::OK );

    brush_tessellation_triangle_t tri{};
    REQUIRE( BrushTessellation_TryGetTriangle(
                 &f.tessellation, 999u, &tri ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Tessellation: shutdown safe on null",
           "[Gate4][Tessellation]" )
{
    BrushTessellation_Shutdown( nullptr );
}

} // namespace cypher::editor::geometry
