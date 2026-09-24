//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PlanarExtrude_Tests.cpp
//  Purpose: Contract tests for EditableMesh tessellation and PlanarRegion
//           extrusion.
//  Details: Extrusion is checked by volume (area x height), outward
//           orientation (positive signed volume), structural validity, and
//           shell count. A polygon with a hole extrudes to a genus-1 solid,
//           so its Euler characteristic is 0, not 2 — the test asserts that
//           explicitly rather than using the genus-0 validity flag.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PlanarExtrude.h"
#include "CypherGeometry_MeshTessellation.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_MeshGeometricValidation.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <catch2/catch_approx.hpp>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;

namespace {

struct RegionMesh {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    planar_region_t region{};
    editable_mesh_t mesh{};
    common::u64 id{ 10u };

    explicit RegionMesh( math::planed_t plane = math::Planed_Make( math::Vec3d_Make( 0, 0, 1 ), 0.0 ) ) {
        planar_frame_t f{};
        REQUIRE( PlanarFrame_TryFromPlane( plane, &f ) == geometry_status_t::OK );
        REQUIRE( PlanarRegion_Init( &region, &allocator, f, geometry_source_id_t{ id++ } ) ==
                 geometry_status_t::OK );
    }
    ~RegionMesh() {
        EditableMesh_Shutdown( &mesh );
        PlanarRegion_Shutdown( &region );
    }
    template <common::usize N>
    void Poly( const vec2d_t ( &p )[N] ) {
        const geometry_source_id_t a{ id++ }, b{ id++ };
        REQUIRE( PlanarRegion_TryAddPolygon( &region, a, b, common::span_t<const vec2d_t>{ p, N },
                                             nullptr ) == geometry_status_t::OK );
    }
    template <common::usize N>
    void Hole( const vec2d_t ( &p )[N] ) {
        REQUIRE( PlanarRegion_TryAddHole( &region, geometry_source_id_t{ id++ },
                                          common::span_t<const vec2d_t>{ p, N } ) ==
                 geometry_status_t::OK );
    }
    geometry_status_t Extrude( double h ) {
        return PlanarExtrude_TryExtrude( &region, h, geometry_policy_t{}, &allocator, &mesh, nullptr );
    }
};

void CheckStructural( const editable_mesh_t *m )
{
    const mesh_validation_result_t v = MeshValidation_Validate( m );
    CHECK( v.bReciprocalTwins );
    CHECK( v.bClosedLoops );
    CHECK( v.bAllVerticesReferenced );
    CHECK( v.bEdgeLinks );
    CHECK( v.bConsistentWinding );
    CHECK( v.fSignedVolume > 0.0 );
}

} // namespace

// ===========================================================================
// Extrusion
// ===========================================================================

TEST_CASE( "PlanarExtrude: square becomes a box", "[Operations][PlanarExtrude]" )
{
    RegionMesh rm;
    const vec2d_t sq[] = { { 0, 0 }, { 2, 0 }, { 2, 3 }, { 0, 3 } };
    rm.Poly( sq );
    REQUIRE( rm.Extrude( 4.0 ) == geometry_status_t::OK );
    CHECK( EditableMesh_VertexCount( &rm.mesh ) == 8u );
    CHECK( EditableMesh_FaceCount( &rm.mesh ) == 4u + 4u ); // 2+2 cap triangles, 4 side quads
    CHECK( EditableMesh_SignedVolume( &rm.mesh ) == Approx( 24.0 ) );
    CheckStructural( &rm.mesh );
    CHECK( MeshValidation_Validate( &rm.mesh ).bEulerValid );
    CHECK( MeshValidation_ValidateGeometry( &rm.mesh, geometry_policy_t{}, {} ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "PlanarExtrude: negative height stays outward", "[Operations][PlanarExtrude]" )
{
    RegionMesh rm;
    const vec2d_t sq[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    rm.Poly( sq );
    REQUIRE( rm.Extrude( -2.0 ) == geometry_status_t::OK );
    CHECK( EditableMesh_SignedVolume( &rm.mesh ) == Approx( 2.0 ) );
    CheckStructural( &rm.mesh );
}

TEST_CASE( "PlanarExtrude: concave L profile", "[Operations][PlanarExtrude]" )
{
    RegionMesh rm;
    const vec2d_t L[] = { { 0, 0 }, { 4, 0 }, { 4, 1 }, { 1, 1 }, { 1, 4 }, { 0, 4 } };
    rm.Poly( L );
    REQUIRE( rm.Extrude( 1.5 ) == geometry_status_t::OK );
    CHECK( EditableMesh_SignedVolume( &rm.mesh ) == Approx( 7.0 * 1.5 ) );
    CheckStructural( &rm.mesh );
    CHECK( MeshValidation_ValidateGeometry( &rm.mesh, geometry_policy_t{}, {} ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "PlanarExtrude: polygon with hole is a genus-1 solid", "[Operations][PlanarExtrude]" )
{
    RegionMesh rm;
    const vec2d_t outer[] = { { 0, 0 }, { 6, 0 }, { 6, 6 }, { 0, 6 } };
    const vec2d_t hole[] = { { 2, 2 }, { 2, 4 }, { 4, 4 }, { 4, 2 } };
    rm.Poly( outer );
    rm.Hole( hole );
    REQUIRE( rm.Extrude( 3.0 ) == geometry_status_t::OK );
    CHECK( EditableMesh_SignedVolume( &rm.mesh ) == Approx( 32.0 * 3.0 ) );
    CheckStructural( &rm.mesh );
    CHECK( EditableMesh_EulerCharacteristic( &rm.mesh ) == 0 ); // torus: 2 - 2g, g = 1
    CHECK( MeshValidation_ValidateGeometry( &rm.mesh, geometry_policy_t{}, {} ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "PlanarExtrude: several polygons become several shells",
           "[Operations][PlanarExtrude]" )
{
    RegionMesh rm;
    const vec2d_t a[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    const vec2d_t b[] = { { 3, 0 }, { 5, 0 }, { 4, 2 } };
    rm.Poly( a );
    rm.Poly( b );
    REQUIRE( rm.Extrude( 1.0 ) == geometry_status_t::OK );
    CHECK( EditableMesh_ShellCount( &rm.mesh ) == 2u );
    CHECK( EditableMesh_SignedVolume( &rm.mesh ) == Approx( 1.0 + 2.0 ) );
    CheckStructural( &rm.mesh );
}

TEST_CASE( "PlanarExtrude: tilted plane", "[Operations][PlanarExtrude]" )
{
    RegionMesh rm( math::Planed_Make( math::Vec3d_Make( 1, 1, 1 ), -2.0 ) );
    const vec2d_t sq[] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    rm.Poly( sq );
    REQUIRE( rm.Extrude( 0.5 ) == geometry_status_t::OK );
    CHECK( EditableMesh_SignedVolume( &rm.mesh ) == Approx( 4.0 * 0.5 ) );
    CheckStructural( &rm.mesh );
}

TEST_CASE( "PlanarExtrude: provenance per face", "[Operations][PlanarExtrude]" )
{
    RegionMesh rm;
    const vec2d_t sq[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    rm.Poly( sq ); // polygon id 11, contour id 12
    common::vector_t<geometry_source_id_t> src{};
    REQUIRE( common::Vector_Init( &src, &rm.allocator ) );
    REQUIRE( PlanarExtrude_TryExtrude( &rm.region, 1.0, geometry_policy_t{}, &rm.allocator, &rm.mesh,
                                       &src ) == geometry_status_t::OK );
    REQUIRE( src.nCount == 8u );
    int caps = 0, sides = 0;
    for ( common::usize i = 0; i < src.nCount; ++i ) {
        if ( src.pData[i].value == 11u ) { ++caps; }
        if ( src.pData[i].value == 12u ) { ++sides; }
    }
    CHECK( caps == 4 );
    CHECK( sides == 4 );
    common::Vector_Shutdown( &src );
}

TEST_CASE( "PlanarExtrude: rejections leave the mesh uninitialized",
           "[Operations][PlanarExtrude]" )
{
    RegionMesh rm;
    CHECK( rm.Extrude( 1.0 ) == geometry_status_t::DEGENERATE ); // empty region
    const vec2d_t cw[] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
    rm.Poly( cw );
    CHECK( rm.Extrude( 1.0 ) == geometry_status_t::INVALID_TOPOLOGY ); // outer wound CW
    CHECK_FALSE( EditableMesh_IsInitialized( &rm.mesh ) );
    CHECK( rm.Extrude( 0.0 ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( rm.Extrude( NAN ) == geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// Mesh tessellation
// ===========================================================================

TEST_CASE( "MeshTessellation: extruded L tessellates with face mapping",
           "[Tessellation][MeshTessellation]" )
{
    RegionMesh rm;
    const vec2d_t L[] = { { 0, 0 }, { 4, 0 }, { 4, 1 }, { 1, 1 }, { 1, 4 }, { 0, 4 } };
    rm.Poly( L );
    REQUIRE( rm.Extrude( 1.0 ) == geometry_status_t::OK );

    mesh_tessellation_t tess{};
    REQUIRE( MeshTessellation_Init( &tess, &rm.allocator ) == geometry_status_t::OK );
    REQUIRE( MeshTessellation_TryBuild( &rm.mesh, &tess, nullptr ) == geometry_status_t::OK );
    // Caps: 4 + 4 triangles; sides: 6 quads -> 12 triangles.
    CHECK( MeshTessellation_TriangleCount( &tess ) == 20u );
    CHECK( tess.positions.nCount == EditableMesh_VertexCount( &rm.mesh ) );

    // Every triangle points the same way as its source face, and triangle
    // volumes sum to the mesh volume (no gaps, no flips).
    double vol = 0.0;
    for ( common::usize t = 0; t < MeshTessellation_TriangleCount( &tess ); ++t ) {
        const vec3d_t a = tess.positions.pData[tess.indices.pData[3 * t + 0]];
        const vec3d_t b = tess.positions.pData[tess.indices.pData[3 * t + 1]];
        const vec3d_t c = tess.positions.pData[tess.indices.pData[3 * t + 2]];
        const vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, a ) );
        const mesh_face_record_t *pF = EditableMesh_GetFace( &rm.mesh, tess.triangleFace.pData[t] );
        REQUIRE( pF != nullptr );
        CHECK( math::Vec3d_Dot( n, pF->normal ) > 0.0 );
        vol += math::Vec3d_Dot( a, math::Vec3d_Cross( b, c ) ) / 6.0;
    }
    CHECK( vol == Approx( EditableMesh_SignedVolume( &rm.mesh ) ) );
    MeshTessellation_Shutdown( &tess );
}

TEST_CASE( "MeshTessellation: argument checks", "[Tessellation][MeshTessellation]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_tessellation_t tess{};
    editable_mesh_t mesh{};
    CHECK( MeshTessellation_TryBuild( &mesh, &tess, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( MeshTessellation_Init( &tess, &allocator ) == geometry_status_t::OK );
    CHECK( MeshTessellation_Init( &tess, &allocator ) == geometry_status_t::ALREADY_INITIALIZED );
    CHECK( MeshTessellation_TryBuild( &mesh, &tess, nullptr ) == geometry_status_t::NOT_INITIALIZED );
    MeshTessellation_Shutdown( &tess );
}

} // namespace cypher::editor::geometry
