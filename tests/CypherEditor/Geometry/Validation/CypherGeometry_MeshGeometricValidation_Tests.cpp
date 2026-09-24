//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshGeometricValidation_Tests.cpp
//  Purpose: Contract tests for exact triangle intersection predicates and
//           geometric mesh validation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Kernel_TriangleIntersection.h"
#include "CypherGeometry_MeshGeometricValidation.h"
#include "CypherGeometry_MeshTopologyOps.h"
#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_Sanitation.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <catch2/catch_approx.hpp>

#include <string>

namespace cypher::editor::geometry {

using math::vec3d_t;
using math::Vec3d_Make;

namespace {

const vec3d_t kCube[8] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
                           { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
const common::u32 kCubeFaces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
                                       { 2, 3, 7, 6 }, { 1, 2, 6, 5 }, { 0, 4, 7, 3 } };

// Builds a mesh of one or more unit cubes via Sanitation.
struct CubeMesh {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    editable_mesh_t mesh{};

    explicit CubeMesh( std::initializer_list<vec3d_t> offsets, bool weld = false,
                       bool shareCorners = true ) {
        polygon_soup_t soup{};
        REQUIRE( PolygonSoup_Init( &soup, &allocator ) == geometry_status_t::OK );
        for ( const vec3d_t &o : offsets ) {
            if ( shareCorners ) {
                const common::u32 base = static_cast<common::u32>( PolygonSoup_VertexCount( &soup ) );
                for ( const vec3d_t &c : kCube ) {
                    REQUIRE( PolygonSoup_TryAddVertex( &soup, math::Vec3d_Add( c, o ), nullptr ) ==
                             geometry_status_t::OK );
                }
                for ( const auto &f : kCubeFaces ) {
                    const common::u32 idx[4] = { base + f[0], base + f[1], base + f[2], base + f[3] };
                    REQUIRE( PolygonSoup_TryAddFace( &soup, common::span_t<const common::u32>{ idx, 4 },
                                                     {}, 0u, nullptr ) == geometry_status_t::OK );
                }
            } else {
                for ( const auto &f : kCubeFaces ) {
                    common::u32 idx[4];
                    for ( int k = 0; k < 4; ++k ) {
                        REQUIRE( PolygonSoup_TryAddVertex( &soup, math::Vec3d_Add( kCube[f[k]], o ),
                                                           &idx[k] ) == geometry_status_t::OK );
                    }
                    REQUIRE( PolygonSoup_TryAddFace( &soup, common::span_t<const common::u32>{ idx, 4 },
                                                     {}, 0u, nullptr ) == geometry_status_t::OK );
                }
            }
        }
        sanitation_policy_t p{};
        p.bWeld = weld;
        p.bRequireClosed = shareCorners || weld;
        REQUIRE( Sanitation_TryPolygonSoupToMesh( &soup, p, &allocator, &mesh, nullptr ).status ==
                 geometry_status_t::OK );
        PolygonSoup_Shutdown( &soup );
    }
    ~CubeMesh() { EditableMesh_Shutdown( &mesh ); }

    geometry_mesh_vertex_handle_t VertexAt( vec3d_t p ) {
        geometry_mesh_vertex_handle_t found{};
        (void)common::GenerationPool_ForEach( &mesh.vertices,
            [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
                if ( v.position.x == p.x && v.position.y == p.y && v.position.z == p.z ) {
                    found = h;
                    return false;
                }
                return true;
            } );
        return found;
    }
};

} // namespace

// ===========================================================================
// Kernel predicates
// ===========================================================================

TEST_CASE( "Kernel: segment vs triangle", "[Kernel][TriangleIntersection]" )
{
    const vec3d_t a{ 0, 0, 0 }, b{ 2, 0, 0 }, c{ 0, 2, 0 };
    CHECK( Kernel_SegmentIntersectsTriangle( { 0.5, 0.5, -1 }, { 0.5, 0.5, 1 }, a, b, c ) );
    CHECK_FALSE( Kernel_SegmentIntersectsTriangle( { 3, 3, -1 }, { 3, 3, 1 }, a, b, c ) );
    CHECK_FALSE( Kernel_SegmentIntersectsTriangle( { 0.5, 0.5, 1 }, { 0.5, 0.5, 2 }, a, b, c ) );
    // Through an edge and through a vertex count as contact.
    CHECK( Kernel_SegmentIntersectsTriangle( { 1, 0, -1 }, { 1, 0, 1 }, a, b, c ) );
    CHECK( Kernel_SegmentIntersectsTriangle( { 0, 0, -1 }, { 0, 0, 1 }, a, b, c ) );
    // Endpoint resting on the face.
    CHECK( Kernel_SegmentIntersectsTriangle( { 0.5, 0.5, 0 }, { 0.5, 0.5, 5 }, a, b, c ) );
    // Coplanar crossing and coplanar miss.
    CHECK( Kernel_SegmentIntersectsTriangle( { -1, 0.5, 0 }, { 3, 0.5, 0 }, a, b, c ) );
    CHECK_FALSE( Kernel_SegmentIntersectsTriangle( { -1, 5, 0 }, { 3, 5, 0 }, a, b, c ) );
    // Degenerate triangle never intersects.
    CHECK_FALSE( Kernel_SegmentIntersectsTriangle( { 0.5, 0, -1 }, { 0.5, 0, 1 }, a, b, { 4, 0, 0 } ) );
}

TEST_CASE( "Kernel: triangle vs triangle", "[Kernel][TriangleIntersection]" )
{
    const vec3d_t t0[3] = { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 } };
    SECTION( "piercing" ) {
        const vec3d_t t1[3] = { { 0.5, 0.5, -1 }, { 0.5, 0.5, 1 }, { 3, 3, 0.5 } };
        CHECK( Kernel_TrianglesIntersect( t0, t1 ) );
    }
    SECTION( "separated" ) {
        const vec3d_t t1[3] = { { 0, 0, 1 }, { 2, 0, 1 }, { 0, 2, 1 } };
        CHECK_FALSE( Kernel_TrianglesIntersect( t0, t1 ) );
    }
    SECTION( "coplanar overlap and coplanar containment" ) {
        const vec3d_t t1[3] = { { 1, 1, 0 }, { 3, 1, 0 }, { 1, 3, 0 } };
        CHECK( Kernel_TrianglesIntersect( t0, t1 ) );
        const vec3d_t inner[3] = { { 0.2, 0.2, 0 }, { 0.6, 0.2, 0 }, { 0.2, 0.6, 0 } };
        CHECK( Kernel_TrianglesIntersect( t0, inner ) );
        const vec3d_t far[3] = { { 5, 5, 0 }, { 6, 5, 0 }, { 5, 6, 0 } };
        CHECK_FALSE( Kernel_TrianglesIntersect( t0, far ) );
    }
    SECTION( "near-parallel decided exactly" ) {
        const double z = std::nextafter( 0.0, 1.0 );
        const vec3d_t t1[3] = { { 0, 0, z }, { 2, 0, z }, { 0, 2, z } };
        CHECK_FALSE( Kernel_TrianglesIntersect( t0, t1 ) );
    }
}

TEST_CASE( "Kernel: shared-corner exclusion", "[Kernel][TriangleIntersection]" )
{
    const vec3d_t t0[3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const common::u32 i0[3] = { 10, 11, 12 };
    SECTION( "sharing an edge, folded upward: no intersection" ) {
        const vec3d_t t1[3] = { { 1, 0, 0 }, { 0, 0, 0 }, { 0.5, -0.5, 0.5 } };
        const common::u32 i1[3] = { 11, 10, 13 };
        CHECK_FALSE( Kernel_TrianglesIntersectExcludingShared( t0, i0, t1, i1 ) );
    }
    SECTION( "sharing an edge, folded flat onto each other: intersection" ) {
        const vec3d_t t1[3] = { { 1, 0, 0 }, { 0, 0, 0 }, { 0.3, 0.3, 0 } };
        const common::u32 i1[3] = { 11, 10, 13 };
        CHECK( Kernel_TrianglesIntersectExcludingShared( t0, i0, t1, i1 ) );
    }
    SECTION( "sharing a vertex, fanning away: no intersection" ) {
        const vec3d_t t1[3] = { { 0, 0, 0 }, { -1, 0, 0.2 }, { 0, -1, 0.2 } };
        const common::u32 i1[3] = { 10, 20, 21 };
        CHECK_FALSE( Kernel_TrianglesIntersectExcludingShared( t0, i0, t1, i1 ) );
    }
    SECTION( "sharing a vertex, passing through: intersection" ) {
        const vec3d_t t1[3] = { { 0, 0, 0 }, { 0.5, 0.5, 1 }, { 0.5, 0.5, -1 } };
        const common::u32 i1[3] = { 10, 20, 21 };
        CHECK( Kernel_TrianglesIntersectExcludingShared( t0, i0, t1, i1 ) );
    }
    SECTION( "sharing a vertex, coplanar overlap" ) {
        const vec3d_t t1[3] = { { 0, 0, 0 }, { 0.5, 0.1, 0 }, { 0.1, 0.5, 0 } };
        const common::u32 i1[3] = { 10, 20, 21 };
        CHECK( Kernel_TrianglesIntersectExcludingShared( t0, i0, t1, i1 ) );
    }
    SECTION( "without shared ids the same contact counts" ) {
        const vec3d_t t1[3] = { { 0, 0, 0 }, { -1, 0, 0.2 }, { 0, -1, 0.2 } };
        const common::u32 i1[3] = { 30, 31, 32 };
        CHECK( Kernel_TrianglesIntersectExcludingShared( t0, i0, t1, i1 ) );
    }
}

// ===========================================================================
// Mesh geometric validation
// ===========================================================================

TEST_CASE( "MeshGeometry: clean cube has no findings", "[Validation][MeshGeometry]" )
{
    CubeMesh cube( { { 0, 0, 0 } } );
    const mesh_geometric_validation_t v =
        MeshValidation_ValidateGeometry( &cube.mesh, geometry_policy_t{}, {} );
    CHECK( v.status == geometry_status_t::OK );
    CHECK( v.cTotalIssues == 0u );
    CHECK( v.cCandidatePairsTested > 0u ); // adjacent faces were examined and excused
}

TEST_CASE( "MeshGeometry: interpenetrating shells are self-intersecting",
           "[Validation][MeshGeometry]" )
{
    CubeMesh cubes( { { 0, 0, 0 }, { 0.5, 0.5, 0.5 } } );
    const mesh_geometric_validation_t v =
        MeshValidation_ValidateGeometry( &cubes.mesh, geometry_policy_t{}, {} );
    CHECK( v.status == geometry_status_t::SELF_INTERSECTING );
    CHECK( v.cSelfIntersections > 0u );
    REQUIRE( v.cIssues > 0u );
    CHECK( v.issues[0].kind == mesh_geometric_issue_kind_t::SELF_INTERSECTION );
}

TEST_CASE( "MeshGeometry: disjoint shells are fine", "[Validation][MeshGeometry]" )
{
    CubeMesh cubes( { { 0, 0, 0 }, { 3, 0, 0 } } );
    CHECK( MeshValidation_ValidateGeometry( &cubes.mesh, geometry_policy_t{}, {} ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshGeometry: corner pushed through the solid", "[Validation][MeshGeometry]" )
{
    CubeMesh cube( { { 0, 0, 0 } } );
    const geometry_mesh_vertex_handle_t h = cube.VertexAt( { 1, 1, 1 } );
    REQUIRE( GeometryHandle_IsValid( h ) );
    REQUIRE( MeshOps_MoveVertex( &cube.mesh, h, Vec3d_Make( -0.5, -0.5, -0.5 ) ) ==
             geometry_status_t::OK );
    const mesh_geometric_validation_t v =
        MeshValidation_ValidateGeometry( &cube.mesh, geometry_policy_t{}, {} );
    CHECK( v.status == geometry_status_t::SELF_INTERSECTING );
    CHECK( v.cNonPlanarFaces > 0u ); // the three incident quads are warped too
}

TEST_CASE( "MeshGeometry: warnings keep status OK", "[Validation][MeshGeometry]" )
{
    SECTION( "slightly warped face" ) {
        CubeMesh cube( { { 0, 0, 0 } } );
        const geometry_mesh_vertex_handle_t h = cube.VertexAt( { 1, 1, 1 } );
        REQUIRE( MeshOps_MoveVertex( &cube.mesh, h, Vec3d_Make( 1, 1, 1.001 ) ) ==
                 geometry_status_t::OK );
        const mesh_geometric_validation_t v =
            MeshValidation_ValidateGeometry( &cube.mesh, geometry_policy_t{}, {} );
        CHECK( v.status == geometry_status_t::OK );
        // Raising the corner along z keeps it in the x = 1 and y = 1 planes;
        // only the top face (z = 1) is warped.
        CHECK( v.cNonPlanarFaces == 1u );
    }
    SECTION( "short edge" ) {
        CubeMesh cube( { { 0, 0, 0 } } );
        geometry_mesh_edge_handle_t hE{};
        (void)common::GenerationPool_ForEach( &cube.mesh.edges,
            [&]( geometry_mesh_edge_handle_t h, const mesh_edge_record_t & ) noexcept -> common::bool_t {
                hE = h;
                return false;
            } );
        REQUIRE( MeshOps_SplitEdge( &cube.mesh, hE, 1e-9 ).status == geometry_status_t::OK );
        const mesh_geometric_validation_t v =
            MeshValidation_ValidateGeometry( &cube.mesh, geometry_policy_t{}, {} );
        CHECK( v.status == geometry_status_t::OK );
        CHECK( v.cShortEdges == 1u );
    }
    SECTION( "coincident unwelded vertices" ) {
        CubeMesh cube( { { 0, 0, 0 } }, false, false ); // 6 open quads, 24 vertices
        mesh_geometric_options_t o{};
        o.bCheckSelfIntersection = false;
        const mesh_geometric_validation_t v =
            MeshValidation_ValidateGeometry( &cube.mesh, geometry_policy_t{}, o );
        CHECK( v.cCoincidentPairs == 24u ); // 8 corners x C(3,2)
        CHECK( v.status == geometry_status_t::OK );
    }
}

TEST_CASE( "MeshGeometry: bounded issue list, full counts", "[Validation][MeshGeometry]" )
{
    CubeMesh cube( { { 0, 0, 0 } }, false, false );
    geometry_policy_t p{};
    p.numerical.fWeldDistance = 10.0; // every pair is "coincident"
    mesh_geometric_options_t o{};
    o.bCheckSelfIntersection = false;
    const mesh_geometric_validation_t v = MeshValidation_ValidateGeometry( &cube.mesh, p, o );
    CHECK( v.cCoincidentPairs == 24u * 23u / 2u );
    CHECK( v.cIssues == kMeshGeometricIssuesMax );
    CHECK( v.cTotalIssues == v.cCoincidentPairs );
    CHECK( std::string( MeshGeometricIssue_Name( mesh_geometric_issue_kind_t::SHORT_EDGE ) ) ==
           "short_edge" );
}

TEST_CASE( "MeshGeometry: uninitialized mesh", "[Validation][MeshGeometry]" )
{
    editable_mesh_t m{};
    CHECK( MeshValidation_ValidateGeometry( &m, geometry_policy_t{}, {} ).status ==
           geometry_status_t::NOT_INITIALIZED );
}

} // namespace cypher::editor::geometry
