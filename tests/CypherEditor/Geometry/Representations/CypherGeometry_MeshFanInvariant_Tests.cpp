//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshFanInvariant_Tests.cpp
//  Purpose: Regression tests for the review of the open-boundary operations:
//           the open-fan convention (each vertex's stored outgoing half-edge
//           must start its fan, so fan walks see every incident face), one
//           fan per vertex after delete / detach / add, and shell-capacity
//           accounting when most of the shell limit is in use.
//  Details: The invariant is checked directly: walking h -> next(twin(h))
//           from the stored half-edge must visit exactly as many faces as
//           there are half-edges leaving the vertex. The concrete symptom
//           (Codex's report) is also checked: moving a rim vertex of an open
//           box with MeshOps_MoveVertex must update every adjacent face's
//           normal, not only the first one the walk reaches.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshBridge.h"
#include "CypherGeometry_MeshKnife.h"
#include "CypherGeometry_MeshMerge.h"
#include "CypherGeometry_MeshSlice.h"
#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshTopologyOps.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <map>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

struct Mesh {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t s{};
    mesh_source_description_t d{};
    Mesh() { REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK ); }
    ~Mesh() {
        MeshSource_Shutdown( &s );
        MeshSourceDescription_Shutdown( &d );
    }
    void Cube() {
        for ( int i = 0; i < 8; ++i ) {
            REQUIRE( MeshSourceDescription_TryAddVertex(
                         &d, Vec3d_Make( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0 ),
                         Id( 10u + static_cast<common::u64>( i ) ), nullptr ) == geometry_status_t::OK );
        }
        const std::vector<std::vector<common::u32>> faces = {
            { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( common::usize f = 0; f < faces.size(); ++f ) {
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() },
                                                       Id( 20u + f ), mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
        }
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
    }
    geometry_mesh_face_handle_t Face( common::u64 id ) {
        geometry_mesh_face_handle_t h{};
        REQUIRE( MeshSource_TryFindFace( &s, Id( id ), &h ) );
        return h;
    }
    geometry_mesh_edge_handle_t Edge( common::u64 a, common::u64 b ) {
        geometry_mesh_edge_handle_t h{};
        REQUIRE( MeshSourceEdit_TryFindEdge( &s, Id( a ), Id( b ), &h ) );
        return h;
    }
    geometry_mesh_vertex_handle_t Vertex( common::u64 id ) {
        geometry_mesh_vertex_handle_t h{};
        REQUIRE( MeshSource_TryFindVertex( &s, Id( id ), &h ) );
        return h;
    }
    void OpenTop() {
        const geometry_mesh_face_handle_t top = Face( 21 );
        REQUIRE( MeshBoundary_DeleteFaces( &s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 } ).status ==
                 geometry_status_t::OK );
    }
};

// Every vertex's fan walk from its stored half-edge covers all its faces.
void CheckFanStarts( const editable_mesh_t *pMesh ) {
    std::map<common::u64, common::u32> outgoing;
    (void)common::GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t, const mesh_half_edge_record_t &h ) noexcept -> common::bool_t {
            ++outgoing[( static_cast<common::u64>( h.hOrigin.nSlot ) << 32 ) | h.hOrigin.nGeneration];
            return true;
        } );
    (void)common::GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hV, const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            const common::u64 key = ( static_cast<common::u64>( hV.nSlot ) << 32 ) | hV.nGeneration;
            common::u32 cVisited = 0u;
            geometry_mesh_half_edge_handle_t h = v.hOutHalfEdge;
            for ( common::u32 guard = 0u; guard < 1000u; ++guard ) {
                const mesh_half_edge_record_t *pH = common::GenerationPool_Get( &pMesh->halfEdges, h );
                if ( pH == nullptr ) { break; }
                ++cVisited;
                const mesh_half_edge_record_t *pT = common::GenerationPool_Get( &pMesh->halfEdges, pH->hTwin );
                if ( pT == nullptr ) { break; }
                h = pT->hNext;
                if ( h.nSlot == v.hOutHalfEdge.nSlot && h.nGeneration == v.hOutHalfEdge.nGeneration ) { break; }
            }
            CAPTURE( hV.nSlot, v.position.x, v.position.y, v.position.z );
            CHECK( cVisited == outgoing[key] );
            return true;
        } );
}

// The stored normal of every face equals its recomputed Newell normal.
void CheckNormals( const editable_mesh_t *pMesh ) {
    (void)common::GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept -> common::bool_t {
            const mesh_loop_record_t *pL = common::GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
            vec3d_t n{};
            geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
            std::vector<vec3d_t> pts;
            for ( common::u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                const mesh_half_edge_record_t *pH = common::GenerationPool_Get( &pMesh->halfEdges, h );
                pts.push_back( common::GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position );
                h = pH->hNext;
            }
            for ( common::usize i = 0; i < pts.size(); ++i ) {
                const vec3d_t a = pts[i], b = pts[( i + 1 ) % pts.size()];
                n.x += ( a.y - b.y ) * ( a.z + b.z );
                n.y += ( a.z - b.z ) * ( a.x + b.x );
                n.z += ( a.x - b.x ) * ( a.y + b.y );
            }
            const double len = std::sqrt( n.x * n.x + n.y * n.y + n.z * n.z );
            CHECK( f.normal.x == Approx( n.x / len ).margin( 1e-12 ) );
            CHECK( f.normal.y == Approx( n.y / len ).margin( 1e-12 ) );
            CHECK( f.normal.z == Approx( n.z / len ).margin( 1e-12 ) );
            return true;
        } );
}

} // namespace

TEST_CASE( "Open box: moving a rim vertex updates every adjacent face normal", "[geometry][meshboundary][fan]" ) {
    Mesh m;
    m.Cube();
    m.OpenTop();
    CheckFanStarts( &m.s.mesh );
    // Rim vertex 17 has two faces (x = 1 and y = 1 sides); lift it.
    REQUIRE( MeshOps_MoveVertex( &m.s.mesh, m.Vertex( 17 ), Vec3d_Make( 1.2, 1.2, 1.5 ) ) == geometry_status_t::OK );
    CheckNormals( &m.s.mesh );
}

TEST_CASE( "Every open-boundary operation leaves each fan starting at its open end", "[geometry][meshboundary][fan]" ) {
    SECTION( "Delete faces" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        CheckFanStarts( &m.s.mesh );
    }
    SECTION( "Extrude the rim twice" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        const geometry_mesh_edge_handle_t rim[] = { m.Edge( 14, 15 ), m.Edge( 15, 17 ), m.Edge( 17, 16 ), m.Edge( 16, 14 ) };
        common::vector_t<geometry_mesh_edge_handle_t> outer{};
        REQUIRE( common::Vector_Init( &outer, &m.allocator ) );
        REQUIRE( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ rim, 4 },
                                            Vec3d_Make( 0, 0, 1 ), &outer )
                     .status == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        std::vector<geometry_mesh_edge_handle_t> again( outer.pData, outer.pData + outer.nCount );
        common::Vector_Clear( &outer );
        REQUIRE( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ again.data(), again.size() },
                                            Vec3d_Make( 0.5, 0, 0.5 ), &outer )
                     .status == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        common::Vector_Shutdown( &outer );
    }
    SECTION( "Extrude a single open edge (chain ends)" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        const geometry_mesh_edge_handle_t one[] = { m.Edge( 14, 15 ) };
        REQUIRE( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ one, 1 },
                                            Vec3d_Make( 0, -1, 0 ), nullptr )
                     .status == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
    }
    SECTION( "Detach a face" ) {
        Mesh m;
        m.Cube();
        const geometry_mesh_face_handle_t f[] = { m.Face( 21 ) };
        REQUIRE( MeshBoundary_DetachFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ f, 1 } ).status ==
                 geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
    }
    SECTION( "Add faces on the rim" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        mesh_boundary_corner_t ring[3]{};
        ring[0].hVertex = m.Vertex( 14 ); // 14 -> 15 stitches to the open 15 -> 14
        ring[1].hVertex = m.Vertex( 15 );
        ring[2].iNew = 0u;
        const common::u32 size = 3u;
        const vec3d_t apex = Vec3d_Make( 0.5, -0.5, 1.5 );
        REQUIRE( MeshBoundary_AddFaces( &m.s.mesh, common::span_t<const mesh_boundary_corner_t>{ ring, 3 },
                                        common::span_t<const common::u32>{ &size, 1 }, common::span_t<const vec3d_t>{ &apex, 1 },
                                        nullptr )
                     .status == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
    }
    SECTION( "Knife across an open rim" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        // Across the +y side from its rim edge down to its bottom edge.
        mesh_knife_point_t p[2]{};
        p[0].kind = p[1].kind = mesh_knife_point_kind_t::EDGE;
        p[0].hEdge = m.Edge( 16, 17 );
        p[1].hEdge = m.Edge( 12, 13 );
        REQUIRE( MeshKnife_Cut( &m.s.mesh, common::span_t<const mesh_knife_point_t>{ p, 2 }, nullptr, nullptr ).status ==
                 geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        // And from a rim vertex straight down its side's diagonal.
        mesh_knife_point_t q[2]{};
        q[0].hVertex = m.Vertex( 14 );
        q[1].hVertex = m.Vertex( 11 );
        REQUIRE( MeshKnife_Cut( &m.s.mesh, common::span_t<const mesh_knife_point_t>{ q, 2 }, nullptr, nullptr ).status ==
                 geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        REQUIRE( MeshOps_MoveVertex( &m.s.mesh, m.Vertex( 14 ), Vec3d_Make( -0.1, -0.1, 1.2 ) ) == geometry_status_t::OK );
        CheckNormals( &m.s.mesh );
    }
    SECTION( "Flip one side of an open box" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        const geometry_mesh_face_handle_t f[] = { m.Face( 22 ) };
        REQUIRE( MeshBoundary_FlipFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ f, 1 } ).status ==
                 geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        CheckNormals( &m.s.mesh );
    }
    SECTION( "Flip a whole open box (the rim fans change ends)" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        const geometry_mesh_face_handle_t f[] = { m.Face( 20 ), m.Face( 22 ), m.Face( 23 ), m.Face( 24 ), m.Face( 25 ) };
        REQUIRE( MeshBoundary_FlipFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ f, 5 } ).status ==
                 geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        CheckNormals( &m.s.mesh );
        REQUIRE( MeshOps_MoveVertex( &m.s.mesh, m.Vertex( 17 ), Vec3d_Make( 1.2, 1.2, 1.5 ) ) == geometry_status_t::OK );
        CheckNormals( &m.s.mesh );
    }
    SECTION( "Mirror an open box (every loop reverses)" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        REQUIRE( MeshOps_Mirror( &m.s.mesh, math::planed_t{ Vec3d_Make( 1, 0, 0 ), -0.5 } ) == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        REQUIRE( MeshOps_MoveVertex( &m.s.mesh, m.Vertex( 17 ), Vec3d_Make( 1.2, 1.2, 1.5 ) ) == geometry_status_t::OK );
        CheckNormals( &m.s.mesh );
    }
    SECTION( "Clip an open box below its rim" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        mesh_slice_params_t p{};
        p.plane = math::planed_t{ Vec3d_Make( 0, 0, 1 ), -0.5 };
        p.keep = mesh_slice_keep_t::BACK;
        REQUIRE( MeshSlice_ByPlane( &m.s.mesh, p, nullptr ).status == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        CheckNormals( &m.s.mesh );
    }
    SECTION( "Detach the top, then weld it back only along two edges" ) {
        Mesh m;
        m.Cube();
        const geometry_mesh_face_handle_t f[] = { m.Face( 21 ) };
        REQUIRE( MeshBoundary_DetachFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ f, 1 } ).status ==
                 geometry_status_t::OK );
        // Sew one rim edge back: the other three stay open, so the two
        // welded corners end on open fans.
        const geometry_mesh_face_handle_t top = m.Face( 21 );
        const mesh_loop_record_t *pL =
            common::GenerationPool_Get( &m.s.mesh.loops, common::GenerationPool_Get( &m.s.mesh.faces, top )->hOuterLoop );
        const mesh_half_edge_record_t *pH = common::GenerationPool_Get( &m.s.mesh.halfEdges, pL->hFirstHalfEdge );
        const vec3d_t a = common::GenerationPool_Get( &m.s.mesh.vertices, pH->hOrigin )->position;
        const vec3d_t b =
            common::GenerationPool_Get( &m.s.mesh.vertices, common::GenerationPool_Get( &m.s.mesh.halfEdges, pH->hNext )->hOrigin )->position;
        geometry_mesh_edge_handle_t partner{};
        (void)common::GenerationPool_ForEach( &m.s.mesh.halfEdges,
            [&]( geometry_mesh_half_edge_handle_t, const mesh_half_edge_record_t &h ) noexcept -> common::bool_t {
                const mesh_half_edge_record_t *pN = common::GenerationPool_Get( &m.s.mesh.halfEdges, h.hNext );
                const vec3d_t p = common::GenerationPool_Get( &m.s.mesh.vertices, h.hOrigin )->position;
                const vec3d_t q = common::GenerationPool_Get( &m.s.mesh.vertices, pN->hOrigin )->position;
                if ( math::Vec3d_EqualsExact( p, b ) && math::Vec3d_EqualsExact( q, a ) ) { partner = h.hEdge; }
                return true;
            } );
        const geometry_mesh_edge_handle_t pair[] = { pH->hEdge, partner };
        REQUIRE( MeshMerge_SewEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ pair, 2 }, mesh_merge_target_t::FIRST,
                                     nullptr )
                     .status == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        CheckNormals( &m.s.mesh );
    }
    SECTION( "Bridge an open box's rim edge to a new sheet's border" ) {
        Mesh m;
        m.Cube();
        m.OpenTop();
        // Two rim edges of the open box, bridged across the opening to the
        // opposite rim edges (a strip lid with open ends).
        const geometry_mesh_edge_handle_t a[] = { m.Edge( 14, 15 ) };
        const geometry_mesh_edge_handle_t b[] = { m.Edge( 17, 16 ) };
        REQUIRE( MeshBridge_EdgeChains( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ a, 1 },
                                        common::span_t<const geometry_mesh_edge_handle_t>{ b, 1 }, 2u, nullptr )
                     .status == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
        CheckNormals( &m.s.mesh );
    }
    SECTION( "Replace faces into an open result" ) {
        Mesh m;
        m.Cube();
        const geometry_mesh_face_handle_t rm[] = { m.Face( 21 ), m.Face( 22 ) };
        REQUIRE( MeshBoundary_ReplaceFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ rm, 2 }, {}, {}, {}, nullptr )
                     .status == geometry_status_t::OK );
        CheckFanStarts( &m.s.mesh );
    }
}

TEST_CASE( "Detach gives every separate fan its own vertex", "[geometry][meshboundary][fan]" ) {
    // 2 x 2 sheet around centre 14; select the diagonal quads 30 and 33.
    // Around 14 the selection is two separate sectors and so is the rest:
    // four fans, so 14 becomes four vertices; the edge midpoints 11, 13,
    // 15, 17 each sit between a selected and an unselected quad and split
    // in two. All four quads end up separate.
    Mesh m;
    for ( int y = 0; y < 3; ++y ) {
        for ( int x = 0; x < 3; ++x ) {
            REQUIRE( MeshSourceDescription_TryAddVertex( &m.d, Vec3d_Make( x, y, 0 ), Id( 10u + static_cast<common::u64>( y * 3 + x ) ),
                                                         nullptr ) == geometry_status_t::OK );
        }
    }
    const std::vector<std::vector<common::u32>> quads = { { 0, 1, 4, 3 }, { 1, 2, 5, 4 }, { 3, 4, 7, 6 }, { 4, 5, 8, 7 } };
    for ( common::usize f = 0; f < quads.size(); ++f ) {
        REQUIRE( MeshSourceDescription_TryAddFace( &m.d, common::span_t<const common::u32>{ quads[f].data(), quads[f].size() },
                                                   Id( 30u + f ), mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
    }
    REQUIRE( MeshSource_TryBuild( &m.d, &m.allocator, &m.s ) == geometry_status_t::OK );
    const geometry_mesh_face_handle_t sel[] = { m.Face( 30 ), m.Face( 33 ) };
    const mesh_boundary_detach_result_t r =
        MeshBoundary_DetachFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ sel, 2 } );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cVerticesDuplicated == 7u ); // 3 extra at the centre + 1 at each of 4 midpoints
    CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 16u );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 4u );
    CheckFanStarts( &m.s.mesh );
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    REQUIRE( MeshSource_TryAssignMissingIds( &m.s, &ids, nullptr ) == geometry_status_t::OK );
    CHECK( MeshSource_Validate( &m.s, &m.allocator ).fault == mesh_source_fault_t::NONE );
}

TEST_CASE( "Add faces may not attach a closed group to a rim at one vertex", "[geometry][meshboundary][fan]" ) {
    Mesh m;
    m.Cube();
    m.OpenTop();
    // A closed tetrahedron whose only link to the box is rim vertex 17: an
    // edge-based check sees one open fan at 17, but 17 would have two fans.
    mesh_boundary_corner_t c[12]{};
    const common::u32 tris[4][3] = { { 0, 2, 1 }, { 0, 1, 3 }, { 0, 3, 2 }, { 1, 2, 3 } }; // 0 = vertex 17
    for ( int t = 0; t < 4; ++t ) {
        for ( int k = 0; k < 3; ++k ) {
            const common::u32 v = tris[t][k];
            if ( v == 0u ) {
                c[t * 3 + k].hVertex = m.Vertex( 17 );
            } else {
                c[t * 3 + k].iNew = v - 1u;
            }
        }
    }
    const common::u32 sizes[4] = { 3, 3, 3, 3 };
    const vec3d_t fresh[3] = { Vec3d_Make( 2, 1, 1 ), Vec3d_Make( 1, 2, 1 ), Vec3d_Make( 1, 1, 2 ) };
    CHECK( MeshBoundary_AddFaces( &m.s.mesh, common::span_t<const mesh_boundary_corner_t>{ c, 12 },
                                  common::span_t<const common::u32>{ sizes, 4 }, common::span_t<const vec3d_t>{ fresh, 3 }, nullptr )
               .status == geometry_status_t::NON_MANIFOLD );
    // The same tetrahedron floating free is fine: a new closed shell.
    for ( int i = 0; i < 12; ++i ) {
        if ( c[i].iNew == CY_INVALID_INDEX ) { c[i].iNew = 3u; }
    }
    const vec3d_t shifted[4] = { Vec3d_Make( 2, 1, 1 ), Vec3d_Make( 1, 2, 1 ), Vec3d_Make( 1, 1, 2 ), Vec3d_Make( 1.2, 1.2, 1.2 ) };
    CHECK( MeshBoundary_AddFaces( &m.s.mesh, common::span_t<const mesh_boundary_corner_t>{ c, 12 },
                                  common::span_t<const common::u32>{ sizes, 4 }, common::span_t<const vec3d_t>{ shifted, 4 }, nullptr )
               .status == geometry_status_t::OK );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 2u );
    CheckFanStarts( &m.s.mesh );
}

TEST_CASE( "Boundary operations work when more than half the shell limit is in use", "[geometry][meshboundary][fan]" ) {
    // 130 separate quads = 130 shells, over half of the default 256. Shell
    // rebuilds reuse the old records' slots, so edits must not be refused.
    Mesh m;
    for ( common::u64 q = 0; q < 130u; ++q ) {
        const double x = 2.0 * static_cast<double>( q );
        for ( int k = 0; k < 4; ++k ) {
            REQUIRE( MeshSourceDescription_TryAddVertex( &m.d, Vec3d_Make( x + ( ( k == 1 || k == 2 ) ? 1.0 : 0.0 ), k >= 2 ? 1.0 : 0.0, 0.0 ),
                                                         Id( 1000u + q * 4u + static_cast<common::u64>( k ) ), nullptr ) ==
                     geometry_status_t::OK );
        }
        const common::u32 base = static_cast<common::u32>( q * 4u );
        const common::u32 ring[4] = { base, base + 1u, base + 2u, base + 3u };
        REQUIRE( MeshSourceDescription_TryAddFace( &m.d, common::span_t<const common::u32>{ ring, 4 }, Id( 5000u + q ),
                                                   mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
    }
    REQUIRE( MeshSource_TryBuild( &m.d, &m.allocator, &m.s ) == geometry_status_t::OK );
    REQUIRE( EditableMesh_ShellCount( &m.s.mesh ) == 130u );
    const geometry_mesh_edge_handle_t e[] = { m.Edge( 1000, 1001 ) };
    CHECK( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ e, 1 }, Vec3d_Make( 0, -1, 0 ),
                                      nullptr )
               .status == geometry_status_t::OK );
    const geometry_mesh_face_handle_t f[] = { m.Face( 5001 ) };
    CHECK( MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ f, 1 } ).status ==
           geometry_status_t::OK );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 129u );
    CheckFanStarts( &m.s.mesh );
}

TEST_CASE( "Detaching keeps edge flags and crease on both sides of a cut edge", "[geometry][meshboundary][meshedit][fan]" ) {
    Mesh m;
    m.Cube();
    mesh_edge_attributes_t hard{};
    hard.flags = MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM;
    REQUIRE( MeshSourceEdit_TrySetEdgeAttributes( &m.s, Id( 14 ), Id( 15 ), hard, 0.75 ) == geometry_status_t::OK );
    const geometry_source_id_t top[] = { Id( 21 ) };
    REQUIRE( MeshSourceEdit_TryDetachFaces( &m.s, common::span_t<const geometry_source_id_t>{ top, 1 }, nullptr ) ==
             geometry_status_t::OK );
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    REQUIRE( MeshSource_TryAssignMissingIds( &m.s, &ids, nullptr ) == geometry_status_t::OK );
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &m.allocator, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &m.s, &d ) == geometry_status_t::OK );
    // The edge now exists twice at the same place: 14-15 on the box and its
    // copy on the detached top. Both must carry the flags and the crease.
    const vec3d_t a = Vec3d_Make( 0, 0, 1 ), b = Vec3d_Make( 1, 0, 1 );
    common::u32 cAt = 0u, cFlagged = 0u;
    for ( common::usize i = 0; i < d.edges.nCount; ++i ) {
        const mesh_source_edge_t &e = d.edges.pData[i];
        const vec3d_t p = d.vertices.pData[e.iVertexA].position, q = d.vertices.pData[e.iVertexB].position;
        const bool bAt = ( p.x == a.x && p.y == a.y && p.z == a.z && q.x == b.x && q.y == b.y && q.z == b.z ) ||
                         ( p.x == b.x && p.y == b.y && p.z == b.z && q.x == a.x && q.y == a.y && q.z == a.z );
        if ( !bAt ) { continue; }
        ++cAt;
        CHECK( e.attributes.flags == ( MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM ) );
        CHECK( e.creaseWeight == 0.75 );
        cFlagged += e.attributes.flags == ( MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM ) ? 1u : 0u;
    }
    CHECK( cAt == 2u );
    CHECK( cFlagged == 2u );
    MeshSourceDescription_Shutdown( &d );
}

} // namespace cypher::editor::geometry
