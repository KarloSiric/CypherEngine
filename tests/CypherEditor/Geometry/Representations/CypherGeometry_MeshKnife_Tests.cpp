//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshKnife_Tests.cpp
//  Purpose: Contract tests for the knife cut, at the raw mesh level and
//           through the identity-addressed source wrapper.
//  Details: Oracles are counts, Euler characteristic and volume (a knife
//           never changes the solid: a unit cube stays volume 1, Euler 2),
//           exact vertex positions, and - at the source level - the planar
//           UV / per-axis material oracle: every corner a cut creates must
//           carry the projection of its position, and every piece must keep
//           its face's material. Every rejected path must leave the
//           canonical description bit-identical.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshKnife.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

// Dominant-axis projection used as the UV oracle, and a material per axis.
int DominantAxis( vec3d_t n ) {
    const double ax = std::fabs( n.x ), ay = std::fabs( n.y ), az = std::fabs( n.z );
    return az >= ax && az >= ay ? 2 : ( ay >= ax ? 1 : 0 );
}

vec2d_t Project( int axis, vec3d_t p ) {
    switch ( axis ) {
    case 2: return vec2d_t{ p.x, p.y };
    case 1: return vec2d_t{ p.x, p.z };
    default: return vec2d_t{ p.y, p.z };
    }
}

common::u64 MaterialFor( vec3d_t n ) {
    const int axis = DominantAxis( n );
    const double c = axis == 2 ? n.z : ( axis == 1 ? n.y : n.x );
    return 10u + static_cast<common::u64>( axis ) * 2u + ( c > 0.0 ? 1u : 0u );
}

vec3d_t Newell( const mesh_source_description_t &d, const mesh_source_face_t &f ) {
    vec3d_t n{};
    for ( common::u32 k = 0; k < f.cCorners; ++k ) {
        const vec3d_t a = d.vertices.pData[d.corners.pData[f.iFirstCorner + k].iVertex].position;
        const vec3d_t b = d.vertices.pData[d.corners.pData[f.iFirstCorner + ( k + 1 ) % f.cCorners].iVertex].position;
        n.x += ( a.y - b.y ) * ( a.z + b.z );
        n.y += ( a.z - b.z ) * ( a.x + b.x );
        n.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return n;
}

// Unit cube, vertex IDs 10 + (x | y << 1 | z << 2), faces 20..25 with 21 the
// top (z = 1: 14 -> 15 -> 17 -> 16). Optional planar UV / material oracle.
struct Cube {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t s{};
    mesh_source_description_t d{};
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    explicit Cube( bool bOracle = false ) {
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        for ( int i = 0; i < 8; ++i ) {
            REQUIRE( MeshSourceDescription_TryAddVertex(
                         &d, Vec3d_Make( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0 ),
                         Id( 10u + static_cast<common::u64>( i ) ), nullptr ) == geometry_status_t::OK );
        }
        const std::vector<std::vector<common::u32>> faces = {
            { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( common::usize f = 0; f < faces.size(); ++f ) {
            REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() },
                                                       Id( 20u + f ), mesh_face_attributes_t{}, nullptr ) ==
                     geometry_status_t::OK );
        }
        if ( bOracle ) {
            for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
                mesh_source_face_t &face = d.faces.pData[f];
                const vec3d_t n = Newell( d, face );
                face.attributes.material.value = MaterialFor( n );
                for ( common::u32 k = 0; k < face.cCorners; ++k ) {
                    mesh_source_corner_t &c = d.corners.pData[face.iFirstCorner + k];
                    c.attributes.uv0 = Project( DominantAxis( n ), d.vertices.pData[c.iVertex].position );
                }
            }
        }
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
    }
    ~Cube() {
        MeshSource_Shutdown( &s );
        MeshSourceDescription_Shutdown( &d );
    }
    geometry_mesh_vertex_handle_t Vertex( common::u64 id ) {
        geometry_mesh_vertex_handle_t h{};
        REQUIRE( MeshSource_TryFindVertex( &s, Id( id ), &h ) );
        return h;
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
    void Snapshot( mesh_source_description_t *pOut ) {
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &s, pOut ) == geometry_status_t::OK );
    }
    // Closed, volume 1, Euler 2, valid as a source, and round-trips.
    void Solid() {
        const mesh_validation_result_t v = MeshValidation_Validate( &s.mesh );
        CHECK( v.status == geometry_status_t::OK );
        CHECK( v.nEulerCharacteristic == 2 );
        CHECK( v.fSignedVolume == Approx( 1.0 ) );
        CHECK( MeshBoundary_CountBoundaryEdges( &s.mesh ) == 0u );
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        CHECK( MeshSource_Validate( &s, &allocator ).fault == mesh_source_fault_t::NONE );
        mesh_source_description_t a{}, b{};
        REQUIRE( MeshSourceDescription_Init( &a, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSourceDescription_Init( &b, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &s, &a ) == geometry_status_t::OK );
        mesh_source_t copy{};
        REQUIRE( MeshSource_TryBuild( &a, &allocator, &copy ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &copy, &b ) == geometry_status_t::OK );
        CHECK( MeshSourceDescription_Equal( &a, &b ) );
        MeshSource_Shutdown( &copy );
        MeshSourceDescription_Shutdown( &a );
        MeshSourceDescription_Shutdown( &b );
    }
    // Every face's material matches its axis and every corner UV is the
    // projection of its position. Returns the face count.
    common::usize CheckOracle() {
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &s, &d ) == geometry_status_t::OK );
        for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
            const mesh_source_face_t &face = d.faces.pData[f];
            const vec3d_t n = Newell( d, face );
            CAPTURE( f, face.sourceId.value );
            CHECK( face.attributes.material.value == MaterialFor( n ) );
            for ( common::u32 k = 0; k < face.cCorners; ++k ) {
                const mesh_source_corner_t &c = d.corners.pData[face.iFirstCorner + k];
                const vec2d_t want = Project( DominantAxis( n ), d.vertices.pData[c.iVertex].position );
                CHECK( c.attributes.uv0.x == Approx( want.x ).margin( 1e-9 ) );
                CHECK( c.attributes.uv0.y == Approx( want.y ).margin( 1e-9 ) );
            }
        }
        return d.faces.nCount;
    }
    common::u32 Corners( geometry_mesh_face_handle_t h ) {
        const mesh_face_record_t *pF = EditableMesh_GetFace( &s.mesh, h );
        REQUIRE( pF != nullptr );
        return common::GenerationPool_Get( &s.mesh.loops, pF->hOuterLoop )->cHalfEdges;
    }
};

struct Desc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    Desc() { REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK ); }
    ~Desc() { MeshSourceDescription_Shutdown( &d ); }
};

mesh_knife_point_t AtVertex( geometry_mesh_vertex_handle_t h ) {
    mesh_knife_point_t p{};
    p.kind = mesh_knife_point_kind_t::VERTEX;
    p.hVertex = h;
    return p;
}

mesh_knife_point_t AtEdge( geometry_mesh_edge_handle_t h, double t ) {
    mesh_knife_point_t p{};
    p.kind = mesh_knife_point_kind_t::EDGE;
    p.hEdge = h;
    p.t = t;
    return p;
}

mesh_knife_point_t InFace( geometry_mesh_face_handle_t h, vec3d_t pos ) {
    mesh_knife_point_t p{};
    p.kind = mesh_knife_point_kind_t::FACE;
    p.hFace = h;
    p.position = pos;
    return p;
}

mesh_knife_result_t Cut( Cube &c, std::vector<mesh_knife_point_t> path,
                         common::vector_t<geometry_mesh_vertex_handle_t> *pVerts = nullptr,
                         common::vector_t<mesh_knife_split_t> *pSplits = nullptr ) {
    return MeshKnife_Cut( &c.s.mesh, common::span_t<const mesh_knife_point_t>{ path.data(), path.size() }, pVerts, pSplits );
}

} // namespace

TEST_CASE( "Knife across a face from edge to edge", "[geometry][meshknife]" ) {
    Cube c;
    const geometry_mesh_face_handle_t top = c.Face( 21 );
    common::vector_t<geometry_mesh_vertex_handle_t> verts{};
    common::vector_t<mesh_knife_split_t> splits{};
    REQUIRE( common::Vector_Init( &verts, &c.allocator ) );
    REQUIRE( common::Vector_Init( &splits, &c.allocator ) );
    const mesh_knife_result_t r = Cut( c, { AtEdge( c.Edge( 14, 15 ), 0.5 ), AtEdge( c.Edge( 16, 17 ), 0.5 ) }, &verts, &splits );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cVerticesCreated == 2u );
    CHECK( r.cEdgesCreated == 3u ); // two edge halves and the cut
    CHECK( r.cFacesCreated == 1u );
    CHECK( EditableMesh_VertexCount( &c.s.mesh ) == 10u );
    CHECK( EditableMesh_EdgeCount( &c.s.mesh ) == 15u );
    CHECK( EditableMesh_FaceCount( &c.s.mesh ) == 7u );
    REQUIRE( verts.nCount == 2u );
    const vec3d_t p0 = EditableMesh_GetVertex( &c.s.mesh, verts.pData[0] )->position;
    const vec3d_t p1 = EditableMesh_GetVertex( &c.s.mesh, verts.pData[1] )->position;
    CHECK( ( p0.x == 0.5 && p0.y == 0.0 && p0.z == 1.0 ) );
    CHECK( ( p1.x == 0.5 && p1.y == 1.0 && p1.z == 1.0 ) );
    REQUIRE( splits.nCount == 1u );
    CHECK( ( splits.pData[0].hFace.nSlot == top.nSlot && splits.pData[0].hFace.nGeneration == top.nGeneration ) );
    CHECK( c.Corners( splits.pData[0].hFace ) == 4u );
    CHECK( c.Corners( splits.pData[0].hNewFace ) == 4u );
    CHECK( EditableMesh_GetFace( &c.s.mesh, splits.pData[0].hNewFace )->normal.z == Approx( 1.0 ) );
    // The side faces through the split edges each gained a corner.
    CHECK( c.Corners( c.Face( 22 ) ) == 5u );
    CHECK( c.Corners( c.Face( 23 ) ) == 5u );
    c.Solid();
    common::Vector_Shutdown( &verts );
    common::Vector_Shutdown( &splits );
}

TEST_CASE( "Knife loop around four sides is a loop cut", "[geometry][meshknife]" ) {
    Cube c;
    // Around the vertical edges at mid height, back to the start point.
    const mesh_knife_result_t r =
        Cut( c, { AtEdge( c.Edge( 10, 14 ), 0.5 ), AtEdge( c.Edge( 11, 15 ), 0.5 ), AtEdge( c.Edge( 13, 17 ), 0.5 ),
                  AtEdge( c.Edge( 12, 16 ), 0.5 ), AtEdge( c.Edge( 10, 14 ), 0.5 ) } );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cVerticesCreated == 4u ); // the repeated start point is one vertex
    CHECK( r.cEdgesCreated == 8u );
    CHECK( r.cFacesCreated == 4u );
    CHECK( EditableMesh_FaceCount( &c.s.mesh ) == 10u );
    // Every face is still a quad: top and bottom never touched a split edge.
    common::u32 cQuads = 0u;
    (void)common::GenerationPool_ForEach( &c.s.mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> common::bool_t {
            cQuads += c.Corners( h ) == 4u ? 1u : 0u;
            return true;
        } );
    CHECK( cQuads == 10u );
    c.Solid();
}

TEST_CASE( "Knife through face interior points and through a vertex", "[geometry][meshknife]" ) {
    SECTION( "Bent cut corner to corner" ) {
        Cube c;
        const mesh_knife_result_t r =
            Cut( c, { AtVertex( c.Vertex( 14 ) ), InFace( c.Face( 21 ), Vec3d_Make( 0.5, 0.25, 1.0 ) ), AtVertex( c.Vertex( 17 ) ) } );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cVerticesCreated == 1u );
        CHECK( r.cEdgesCreated == 2u );
        CHECK( r.cFacesCreated == 1u );
        c.Solid();
    }
    SECTION( "Straight diagonal" ) {
        Cube c;
        const mesh_knife_result_t r = Cut( c, { AtVertex( c.Vertex( 14 ) ), AtVertex( c.Vertex( 17 ) ) } );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cEdgesCreated == 1u );
        CHECK( r.cFacesCreated == 1u );
        c.Solid();
    }
    SECTION( "Across the top, through corner 15, down the +x side" ) {
        Cube c;
        const mesh_knife_result_t r =
            Cut( c, { AtEdge( c.Edge( 14, 16 ), 0.5 ), AtVertex( c.Vertex( 15 ) ), AtEdge( c.Edge( 11, 13 ), 0.5 ) } );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cVerticesCreated == 2u );
        CHECK( r.cEdgesCreated == 4u );
        CHECK( r.cFacesCreated == 2u );
        c.Solid();
    }
    SECTION( "Notch: in and out through the same edge" ) {
        Cube c;
        const mesh_knife_result_t r = Cut( c, { AtEdge( c.Edge( 14, 15 ), 0.25 ), InFace( c.Face( 21 ), Vec3d_Make( 0.5, 0.3, 1.0 ) ),
                                                AtEdge( c.Edge( 14, 15 ), 0.75 ) } );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cVerticesCreated == 3u );
        CHECK( r.cEdgesCreated == 4u );
        CHECK( c.Corners( c.Face( 22 ) ) == 6u ); // both points on its top edge
        c.Solid();
    }
}

TEST_CASE( "Knife rejects bad paths and leaves the mesh unchanged", "[geometry][meshknife]" ) {
    Cube c;
    Desc before, after;
    c.Snapshot( &before.d );
    const geometry_mesh_face_handle_t top = c.Face( 21 );
    const geometry_mesh_edge_handle_t e1415 = c.Edge( 14, 15 ), e1617 = c.Edge( 16, 17 );

    // Ends inside a face: would leave a dangling edge.
    CHECK( Cut( c, { AtEdge( e1415, 0.5 ), InFace( top, Vec3d_Make( 0.5, 0.5, 1.0 ) ) } ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( Cut( c, { AtEdge( e1415, 0.5 ) } ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Cut( c, { AtEdge( e1415, 0.0 ), AtEdge( e1617, 0.5 ) } ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Cut( c, { AtEdge( e1415, std::nan( "" ) ), AtEdge( e1617, 0.5 ) } ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    // Interior point outside the face, on its boundary, or a self-crossing chain.
    CHECK( Cut( c, { AtEdge( e1415, 0.5 ), InFace( top, Vec3d_Make( 0.5, 1.5, 1.0 ) ), AtEdge( e1617, 0.5 ) } ).status ==
           geometry_status_t::SELF_INTERSECTING );
    CHECK( Cut( c, { AtEdge( e1415, 0.5 ), InFace( top, Vec3d_Make( 1.0, 0.5, 1.0 ) ), AtEdge( e1617, 0.5 ) } ).status ==
           geometry_status_t::SELF_INTERSECTING );
    CHECK( Cut( c, { AtEdge( e1415, 0.5 ), InFace( top, Vec3d_Make( 0.2, 0.7, 1.0 ) ), InFace( top, Vec3d_Make( 0.8, 0.7, 1.0 ) ),
                     InFace( top, Vec3d_Make( 0.2, 0.3, 1.0 ) ), AtEdge( e1617, 0.5 ) } )
               .status == geometry_status_t::SELF_INTERSECTING );
    // A chain through another corner of the face (15) would touch the
    // face's own boundary.
    CHECK( Cut( c, { AtVertex( c.Vertex( 14 ) ), InFace( top, Vec3d_Make( 1.0, 0.0, 1.0 ) ), AtVertex( c.Vertex( 17 ) ) } )
               .status == geometry_status_t::SELF_INTERSECTING );
    // No single face holds both points.
    CHECK( Cut( c, { AtVertex( c.Vertex( 10 ) ), AtVertex( c.Vertex( 17 ) ) } ).status == geometry_status_t::INVALID_ARGUMENT );
    // Interior points from two different faces in one segment.
    CHECK( Cut( c, { AtEdge( e1415, 0.5 ), InFace( top, Vec3d_Make( 0.5, 0.5, 1.0 ) ),
                     InFace( c.Face( 20 ), Vec3d_Make( 0.5, 0.5, 0.0 ) ), AtEdge( e1617, 0.5 ) } )
               .status == geometry_status_t::INVALID_ARGUMENT );
    // The same face twice in one path.
    CHECK( Cut( c, { AtEdge( e1415, 0.25 ), AtEdge( e1617, 0.25 ), AtEdge( e1415, 0.75 ) } ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    // Two points on the same edge with nothing between them.
    CHECK( Cut( c, { AtEdge( e1415, 0.25 ), AtEdge( e1415, 0.75 ) } ).status == geometry_status_t::INVALID_ARGUMENT );
    // Back to the same point.
    CHECK( Cut( c, { AtVertex( c.Vertex( 14 ) ), AtVertex( c.Vertex( 14 ) ) } ).status == geometry_status_t::DEGENERATE );
    CHECK( Cut( c, { AtEdge( e1415, 0.5 ), InFace( top, Vec3d_Make( 0.5, 0.0, 1.0 ) ), AtEdge( e1617, 0.5 ) } ).status ==
           geometry_status_t::DEGENERATE );
    // Stale handle.
    geometry_mesh_vertex_handle_t stale = c.Vertex( 14 );
    stale.nGeneration += 7u;
    CHECK( Cut( c, { AtVertex( stale ), AtVertex( c.Vertex( 17 ) ) } ).status == geometry_status_t::INVALID_HANDLE );

    c.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );

    // Along an existing edge: nothing to cut, succeeds without change.
    const mesh_knife_result_t along = Cut( c, { AtVertex( c.Vertex( 14 ) ), AtVertex( c.Vertex( 15 ) ) } );
    CHECK( along.status == geometry_status_t::OK );
    CHECK( along.cFacesCreated == 0u );
    c.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

TEST_CASE( "Knife through the source keeps UVs, materials, identity and edge attributes", "[geometry][meshknife][meshedit]" ) {
    Cube c( true );
    mesh_edge_attributes_t hard{};
    hard.flags = MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM;
    REQUIRE( MeshSourceEdit_TrySetEdgeAttributes( &c.s, Id( 14 ), Id( 15 ), hard, 0.5 ) == geometry_status_t::OK );

    // Off-centre on purpose: t is measured from the first vertex ID, so the
    // wrapper must flip it whenever the edge is recorded the other way.
    mesh_edit_knife_point_t path[3];
    path[0].kind = mesh_knife_point_kind_t::EDGE;
    path[0].vertexId = Id( 15 );
    path[0].otherVertexId = Id( 14 );
    path[0].t = 0.25; // x = 0.75
    path[1].kind = mesh_knife_point_kind_t::FACE;
    path[1].faceId = Id( 21 );
    path[1].position = Vec3d_Make( 0.4, 0.5, 1.0 );
    path[2].kind = mesh_knife_point_kind_t::EDGE;
    path[2].vertexId = Id( 16 );
    path[2].otherVertexId = Id( 17 );
    path[2].t = 0.75; // x = 0.75
    common::vector_t<geometry_mesh_vertex_handle_t> verts{};
    REQUIRE( common::Vector_Init( &verts, &c.allocator ) );
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TryKnife( &c.s, common::span_t<const mesh_edit_knife_point_t>{ path, 3 }, &verts, &report ) ==
             geometry_status_t::OK );
    REQUIRE( verts.nCount == 3u );
    const vec3d_t a = EditableMesh_GetVertex( &c.s.mesh, verts.pData[0] )->position;
    const vec3d_t m = EditableMesh_GetVertex( &c.s.mesh, verts.pData[1] )->position;
    const vec3d_t b = EditableMesh_GetVertex( &c.s.mesh, verts.pData[2] )->position;
    CHECK( a.x == Approx( 0.75 ) );
    CHECK( a.y == 0.0 );
    CHECK( ( m.x == 0.4 && m.y == 0.5 && m.z == 1.0 ) );
    CHECK( b.x == Approx( 0.75 ) );
    CHECK( b.y == 1.0 );

    CHECK( report.stats.cFacesFromParent == 1u );   // the new piece
    CHECK( report.stats.cCornersDefaulted == 0u );
    CHECK( c.CheckOracle() == 7u );
    // Face 21 survives under its ID (one piece kept the record).
    geometry_mesh_face_handle_t hTop{};
    CHECK( MeshSource_TryFindFace( &c.s, Id( 21 ), &hTop ) );

    // Both halves of the split hard edge stay hard seams with the crease.
    const common::u64 newId = MeshSource_VertexId( &c.s, verts.pData[0] ).value;
    common::u32 cHard = 0u;
    for ( common::usize i = 0; i < c.d.edges.nCount; ++i ) {
        const mesh_source_edge_t &ed = c.d.edges.pData[i];
        const common::u64 va = c.d.vertices.pData[ed.iVertexA].sourceId.value;
        const common::u64 vb = c.d.vertices.pData[ed.iVertexB].sourceId.value;
        if ( ( va == newId || vb == newId ) && ( va == 14u || vb == 14u || va == 15u || vb == 15u ) ) {
            CHECK( ed.attributes.flags == ( MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM ) );
            CHECK( ed.creaseWeight == 0.5 );
            ++cHard;
        }
    }
    CHECK( cHard == 2u );
    c.Solid();

    // An interior point the source cannot store (beyond the coordinate
    // domain) is rejected before anything changes.
    Desc before, after;
    c.Snapshot( &before.d );
    mesh_edit_knife_point_t far[3];
    far[0].kind = far[2].kind = mesh_knife_point_kind_t::VERTEX;
    far[0].vertexId = Id( 10 );
    far[2].vertexId = Id( 13 );
    far[1].kind = mesh_knife_point_kind_t::FACE;
    far[1].faceId = Id( 20 );
    far[1].position = Vec3d_Make( 0.5, 2.0 * kMeshSourceCoordinateMax, 0.0 );
    CHECK( MeshSourceEdit_TryKnife( &c.s, common::span_t<const mesh_edit_knife_point_t>{ far, 3 }, nullptr, nullptr ) ==
           geometry_status_t::NUMERIC_FAILURE );
    c.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
    common::Vector_Shutdown( &verts );
}

namespace {

// Deterministic LCG so a failure reproduces exactly.
struct Rng {
    common::u64 state{ 0x9E3779B97F4A7C15ull };
    common::u32 Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<common::u32>( state >> 33 );
    }
    double Unit() { return ( Next() & 0xFFFFFFu ) / double( 0x1000000u ); }
};

// One random chord across a random face: two points on two different edges
// of its loop. Faces stay convex under chords, so a chord is valid unless
// both edges lie on one line (pieces of an earlier split edge): exactly
// collinear is SELF_INTERSECTING, rounded-off-the-line is a sliver below the
// source's minimum area (DEGENERATE). Either way the mesh is untouched.
bool RandomChord( Cube &c, Rng &rng, bool bSource ) {
    std::vector<geometry_mesh_face_handle_t> faces;
    (void)common::GenerationPool_ForEach( &c.s.mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> common::bool_t {
            faces.push_back( h );
            return true;
        } );
    const geometry_mesh_face_handle_t hF = faces[rng.Next() % faces.size()];
    const mesh_loop_record_t *pL = common::GenerationPool_Get( &c.s.mesh.loops, EditableMesh_GetFace( &c.s.mesh, hF )->hOuterLoop );
    std::vector<geometry_mesh_half_edge_handle_t> ring;
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( common::u32 k = 0; k < pL->cHalfEdges; ++k ) {
        ring.push_back( h );
        h = common::GenerationPool_Get( &c.s.mesh.halfEdges, h )->hNext;
    }
    const common::u32 i = rng.Next() % ring.size();
    const common::u32 j = ( i + 1u + rng.Next() % ( ring.size() - 1u ) ) % ring.size();
    const double ti = 0.1 + 0.8 * rng.Unit(), tj = 0.1 + 0.8 * rng.Unit();
    const mesh_half_edge_record_t *pI = common::GenerationPool_Get( &c.s.mesh.halfEdges, ring[i] );
    const mesh_half_edge_record_t *pJ = common::GenerationPool_Get( &c.s.mesh.halfEdges, ring[j] );

    const common::usize cV = EditableMesh_VertexCount( &c.s.mesh ), cE = EditableMesh_EdgeCount( &c.s.mesh ),
                        cF = EditableMesh_FaceCount( &c.s.mesh );
    geometry_status_t st{};
    if ( bSource ) {
        REQUIRE( MeshSource_TryAssignMissingIds( &c.s, &c.ids, nullptr ) == geometry_status_t::OK );
        auto idOf = [&]( geometry_mesh_vertex_handle_t v ) { return MeshSource_VertexId( &c.s, v ); };
        const mesh_half_edge_record_t *pIn = common::GenerationPool_Get( &c.s.mesh.halfEdges, pI->hNext );
        const mesh_half_edge_record_t *pJn = common::GenerationPool_Get( &c.s.mesh.halfEdges, pJ->hNext );
        mesh_edit_knife_point_t path[2];
        path[0].kind = path[1].kind = mesh_knife_point_kind_t::EDGE;
        path[0].vertexId = idOf( pI->hOrigin );
        path[0].otherVertexId = idOf( pIn->hOrigin );
        path[0].t = ti;
        path[1].vertexId = idOf( pJ->hOrigin );
        path[1].otherVertexId = idOf( pJn->hOrigin );
        path[1].t = tj;
        st = MeshSourceEdit_TryKnife( &c.s, common::span_t<const mesh_edit_knife_point_t>{ path, 2 }, nullptr, nullptr );
    } else {
        st = Cut( c, { AtEdge( pI->hEdge, ti ), AtEdge( pJ->hEdge, tj ) } ).status;
    }
    if ( st == geometry_status_t::OK ) {
        CHECK( EditableMesh_VertexCount( &c.s.mesh ) == cV + 2u );
        CHECK( EditableMesh_EdgeCount( &c.s.mesh ) == cE + 3u );
        CHECK( EditableMesh_FaceCount( &c.s.mesh ) == cF + 1u );
        return true;
    }
    CHECK( ( st == geometry_status_t::SELF_INTERSECTING || st == geometry_status_t::DEGENERATE ) );
    CHECK( EditableMesh_VertexCount( &c.s.mesh ) == cV );
    CHECK( EditableMesh_EdgeCount( &c.s.mesh ) == cE );
    CHECK( EditableMesh_FaceCount( &c.s.mesh ) == cF );
    return false;
}

} // namespace

TEST_CASE( "Knife stress: hundreds of random chords keep the cube a valid unit solid", "[geometry][meshknife][stress]" ) {
    const common::u64 seed = GENERATE( 0x9E3779B97F4A7C15ull, 1ull, 2ull, 0xDEADBEEFull, 77ull );
    CAPTURE( seed );
    Cube c;
    Rng rng{ seed };
    common::u32 cOk = 0u;
    for ( int k = 0; k < 300; ++k ) {
        if ( RandomChord( c, rng, false ) ) { ++cOk; }
        const mesh_validation_result_t v = MeshValidation_Validate( &c.s.mesh );
        REQUIRE( v.status == geometry_status_t::OK );
    }
    CHECK( cOk > 150u );
    c.Solid();
}

TEST_CASE( "Knife stress through the source keeps the UV and material oracle", "[geometry][meshknife][meshedit][stress]" ) {
    const common::u64 seed = GENERATE( 0x1234567ull, 5ull, 0xC0FFEEull );
    CAPTURE( seed );
    Cube c( true );
    Rng rng{ seed };
    common::u32 cOk = 0u;
    for ( int k = 0; k < 40; ++k ) {
        if ( RandomChord( c, rng, true ) ) { ++cOk; }
    }
    CHECK( cOk > 20u );
    CHECK( c.CheckOracle() == 6u + cOk );
    c.Solid();
}

TEST_CASE( "Knife on an open sheet cuts from boundary edge to boundary edge", "[geometry][meshknife]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    const vec3d_t corners[4] = { Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 2, 0, 0 ), Vec3d_Make( 2, 1, 0 ), Vec3d_Make( 0, 1, 0 ) };
    for ( int i = 0; i < 4; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex( &d, corners[i], Id( 10u + static_cast<common::u64>( i ) ), nullptr ) ==
                 geometry_status_t::OK );
    }
    const common::u32 quad[4] = { 0, 1, 2, 3 };
    REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ quad, 4 }, Id( 20 ), mesh_face_attributes_t{},
                                               nullptr ) == geometry_status_t::OK );
    mesh_source_t s{};
    REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
    geometry_mesh_edge_handle_t bottom{}, top{};
    REQUIRE( MeshSourceEdit_TryFindEdge( &s, Id( 10 ), Id( 11 ), &bottom ) );
    REQUIRE( MeshSourceEdit_TryFindEdge( &s, Id( 12 ), Id( 13 ), &top ) );
    const mesh_knife_point_t path[2] = { AtEdge( bottom, 0.5 ), AtEdge( top, 0.5 ) };
    const mesh_knife_result_t r = MeshKnife_Cut( &s.mesh, common::span_t<const mesh_knife_point_t>{ path, 2 }, nullptr, nullptr );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &s.mesh ) == 2u );
    CHECK( EditableMesh_VertexCount( &s.mesh ) == 6u );
    CHECK( EditableMesh_EdgeCount( &s.mesh ) == 7u );
    CHECK( MeshBoundary_CountBoundaryEdges( &s.mesh ) == 6u ); // each split boundary edge stays open on both halves
    // Every vertex is on the open rim, so each must store the outgoing
    // half-edge that starts its open fan: the one whose previous half-edge
    // has no twin (fan walks rotate twin -> next from there).
    common::u32 cFanStart = 0u;
    (void)common::GenerationPool_ForEach( &s.mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t, const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            const mesh_half_edge_record_t *pH = common::GenerationPool_Get( &s.mesh.halfEdges, v.hOutHalfEdge );
            const mesh_half_edge_record_t *pPrev = pH ? common::GenerationPool_Get( &s.mesh.halfEdges, pH->hPrev ) : nullptr;
            cFanStart += ( pPrev != nullptr && common::GenerationPool_Get( &s.mesh.halfEdges, pPrev->hTwin ) == nullptr ) ? 1u : 0u;
            return true;
        } );
    CHECK( cFanStart == 6u );
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
    CHECK( MeshSource_Validate( &s, &allocator ).fault == mesh_source_fault_t::NONE );
    // A second cut across both pieces, through the new rim vertices' side.
    geometry_mesh_edge_handle_t left{}, right{};
    REQUIRE( MeshSourceEdit_TryFindEdge( &s, Id( 13 ), Id( 10 ), &left ) );
    REQUIRE( MeshSourceEdit_TryFindEdge( &s, Id( 11 ), Id( 12 ), &right ) );
    CHECK( MeshKnife_Cut( &s.mesh, common::span_t<const mesh_knife_point_t>{ std::vector<mesh_knife_point_t>{ AtEdge( left, 0.5 ),
                                                                                                      AtEdge( right, 0.5 ) }
                                                                                   .data(),
                                                                               2 },
                          nullptr, nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT ); // no single face holds both
    MeshSource_Shutdown( &s );
    MeshSourceDescription_Shutdown( &d );
}

} // namespace cypher::editor::geometry
