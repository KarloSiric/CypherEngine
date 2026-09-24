//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBevel_Tests.cpp
//  Purpose: Contract tests for MeshBoundary_ReplaceFaces (the failure-atomic
//           local rebuild) and for the edge bevel built on it.
//  Details: Oracles are closed-form volumes of a unit cube:
//             - chamfer one edge by w:          1 - w^2 / 2;
//             - round one edge, s segments:      1 - (w^2 - s w^2 sin(pi/2s) / 2)
//               (square minus the inscribed polygonal quarter disc);
//             - chamfer the top border:          1 - w + frustum(1, 1 - 2w, w);
//             - chamfer all twelve edges:        1 - 6 w^2 + 16 w^3 / 3
//               (12 prisms of w^2/2 x (1 - 2w), and each corner cube
//               [0, w]^3 keeps only the tetrahedron u + v + t <= w);
//           plus Euler characteristic 2, closedness, exact face counts, and
//           "rejected means bit-identical" for every refusal.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBevel.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshKnife.h"
#include "CypherGeometry_MeshSourceBevel.h"
#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

// Unit cube, vertex IDs 10 + (x | y << 1 | z << 2); faces 20..25, 21 = top
// (14 -> 15 -> 17 -> 16).
struct Mesh {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t s{};
    mesh_source_description_t d{};
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    Mesh() { REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK ); }
    ~Mesh() {
        MeshSource_Shutdown( &s );
        MeshSourceDescription_Shutdown( &d );
    }
    void V( double x, double y, double z, common::u64 id ) {
        REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( x, y, z ), Id( id ), nullptr ) == geometry_status_t::OK );
    }
    void F( std::vector<common::u32> idx, common::u64 id ) {
        REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ idx.data(), idx.size() }, Id( id ),
                                                   mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
    }
    void Build() { REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK ); }
    void Cube() {
        for ( int i = 0; i < 8; ++i ) {
            V( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0, 10u + static_cast<common::u64>( i ) );
        }
        F( { 0, 2, 3, 1 }, 20 );
        F( { 4, 5, 7, 6 }, 21 );
        F( { 0, 1, 5, 4 }, 22 );
        F( { 2, 6, 7, 3 }, 23 );
        F( { 0, 4, 6, 2 }, 24 );
        F( { 1, 3, 7, 5 }, 25 );
        Build();
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
    // Closed, Euler 2, the given volume, valid as a source, round-trips.
    void Solid( double volume ) {
        const mesh_validation_result_t v = MeshValidation_Validate( &s.mesh );
        CHECK( v.status == geometry_status_t::OK );
        CHECK( v.nEulerCharacteristic == 2 );
        CHECK( v.fSignedVolume == Approx( volume ).epsilon( 1e-12 ) );
        CHECK( MeshBoundary_CountBoundaryEdges( &s.mesh ) == 0u );
        CHECK( EditableMesh_ShellCount( &s.mesh ) == 1u );
        Sourced();
    }
    void Sourced() {
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
};

struct Desc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    Desc() { REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK ); }
    ~Desc() { MeshSourceDescription_Shutdown( &d ); }
};

mesh_boundary_corner_t Old( geometry_mesh_vertex_handle_t h ) {
    mesh_boundary_corner_t c{};
    c.hVertex = h;
    return c;
}

mesh_boundary_corner_t New( common::u32 i ) {
    mesh_boundary_corner_t c{};
    c.iNew = i;
    return c;
}

mesh_boundary_replace_result_t Replace( Mesh &m, std::vector<geometry_mesh_face_handle_t> remove,
                                        std::vector<mesh_boundary_corner_t> corners, std::vector<common::u32> sizes,
                                        std::vector<vec3d_t> fresh = {} ) {
    return MeshBoundary_ReplaceFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ remove.data(), remove.size() },
                                      common::span_t<const mesh_boundary_corner_t>{ corners.data(), corners.size() },
                                      common::span_t<const common::u32>{ sizes.data(), sizes.size() },
                                      common::span_t<const vec3d_t>{ fresh.data(), fresh.size() }, nullptr );
}

mesh_bevel_result_t Bevel( Mesh &m, std::vector<geometry_mesh_edge_handle_t> edges, double w, common::u32 segments,
                           common::vector_t<mesh_bevel_face_t> *pOut = nullptr ) {
    mesh_bevel_params_t p{};
    p.width = w;
    p.cSegments = segments;
    return MeshBevel_Edges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ edges.data(), edges.size() }, p, pOut );
}

double RoundRemoved( double w, common::u32 s ) {
    const double pi = 3.14159265358979323846;
    return w * w - 0.5 * s * w * w * std::sin( pi / ( 2.0 * s ) );
}

} // namespace

// ---------------------------------------------------------------------------
// ReplaceFaces
// ---------------------------------------------------------------------------

TEST_CASE( "ReplaceFaces rebuilds a region in one step", "[geometry][meshreplace]" ) {
    SECTION( "Top face -> two triangles" ) {
        Mesh m;
        m.Cube();
        const auto r = Replace( m, { m.Face( 21 ) },
                                { Old( m.Vertex( 14 ) ), Old( m.Vertex( 15 ) ), Old( m.Vertex( 17 ) ), Old( m.Vertex( 14 ) ),
                                  Old( m.Vertex( 17 ) ), Old( m.Vertex( 16 ) ) },
                                { 3, 3 } );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cFacesRemoved == 1u );
        CHECK( r.cFacesCreated == 2u );
        CHECK( r.cVerticesRemoved == 0u );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 7u );
        m.Solid( 1.0 );
    }
    SECTION( "Top face -> pyramid with a new apex" ) {
        Mesh m;
        m.Cube();
        const auto a = New( 0 );
        const auto r = Replace( m, { m.Face( 21 ) },
                                { Old( m.Vertex( 14 ) ), Old( m.Vertex( 15 ) ), a, Old( m.Vertex( 15 ) ), Old( m.Vertex( 17 ) ), a,
                                  Old( m.Vertex( 17 ) ), Old( m.Vertex( 16 ) ), a, Old( m.Vertex( 16 ) ), Old( m.Vertex( 14 ) ), a },
                                { 3, 3, 3, 3 }, { Vec3d_Make( 0.5, 0.5, 1.5 ) } );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cVerticesCreated == 1u );
        m.Solid( 1.0 + 1.0 / 6.0 );
    }
    SECTION( "Removing the faces around a corner removes the corner" ) {
        Mesh m;
        m.Cube();
        // Replace the three faces at corner 17 by the cube minus a corner
        // tetrahedron: three trimmed faces plus the cut triangle.
        const std::vector<vec3d_t> fresh = { Vec3d_Make( 0.5, 1, 1 ), Vec3d_Make( 1, 0.5, 1 ), Vec3d_Make( 1, 1, 0.5 ) };
        // top 14 15 17 16 -> 14 15 [1] [0] 16; +x 11 13 17 15 -> 11 13 [2] [1] 15; +y 12 16 17 13 -> 12 16 [0] [2] 13
        const auto r = Replace( m, { m.Face( 21 ), m.Face( 25 ), m.Face( 23 ) },
                                { Old( m.Vertex( 14 ) ), Old( m.Vertex( 15 ) ), New( 1 ), New( 0 ), Old( m.Vertex( 16 ) ),
                                  Old( m.Vertex( 11 ) ), Old( m.Vertex( 13 ) ), New( 2 ), New( 1 ), Old( m.Vertex( 15 ) ),
                                  Old( m.Vertex( 12 ) ), Old( m.Vertex( 16 ) ), New( 0 ), New( 2 ), Old( m.Vertex( 13 ) ),
                                  New( 0 ), New( 1 ), New( 2 ) },
                                { 5, 5, 5, 3 }, fresh );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cVerticesRemoved == 1u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 10u );
        m.Solid( 1.0 - 0.125 / 6.0 );
    }
}

TEST_CASE( "ReplaceFaces rejects results that are not manifold", "[geometry][meshreplace]" ) {
    Mesh m;
    m.Cube();
    Desc before, after;
    m.Snapshot( &before.d );
    // Wrong winding: every edge runs the same way as its neighbour's.
    CHECK( Replace( m, { m.Face( 21 ) }, { Old( m.Vertex( 14 ) ), Old( m.Vertex( 16 ) ), Old( m.Vertex( 17 ) ), Old( m.Vertex( 15 ) ) },
                    { 4 } )
               .status == geometry_status_t::NON_MANIFOLD );
    // A new face hanging off a closed corner.
    CHECK( Replace( m, {}, { Old( m.Vertex( 10 ) ), New( 0 ), New( 1 ) }, { 3 },
                    { Vec3d_Make( -1, 0, 0 ), Vec3d_Make( -1, -1, 0 ) } )
               .status == geometry_status_t::NON_MANIFOLD );
    // Degenerate, unused, malformed.
    CHECK( Replace( m, { m.Face( 21 ) }, { Old( m.Vertex( 14 ) ), Old( m.Vertex( 15 ) ), Old( m.Vertex( 17 ) ), Old( m.Vertex( 16 ) ) },
                    { 4 }, { Vec3d_Make( 9, 9, 9 ) } )
               .status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Replace( m, { m.Face( 21 ), m.Face( 21 ) }, {}, {} ).status == geometry_status_t::INVALID_HANDLE );
    CHECK( Replace( m, {}, {}, {} ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Replace( m, { m.Face( 20 ), m.Face( 21 ), m.Face( 22 ), m.Face( 23 ), m.Face( 24 ), m.Face( 25 ) }, {}, {} ).status ==
           geometry_status_t::DEGENERATE );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

TEST_CASE( "ReplaceFaces refuses to leave a bowtie vertex", "[geometry][meshreplace]" ) {
    // 2 x 2 sheet around centre vertex 14.
    Mesh m;
    for ( int y = 0; y < 3; ++y ) {
        for ( int x = 0; x < 3; ++x ) { m.V( x, y, 0, 10u + static_cast<common::u64>( y * 3 + x ) ); }
    }
    m.F( { 0, 1, 4, 3 }, 30 );
    m.F( { 1, 2, 5, 4 }, 31 );
    m.F( { 3, 4, 7, 6 }, 32 );
    m.F( { 4, 5, 8, 7 }, 33 );
    m.Build();
    Desc before, after;
    m.Snapshot( &before.d );
    // Removing two diagonal quads leaves the centre with two open fans.
    CHECK( Replace( m, { m.Face( 30 ), m.Face( 33 ) }, {}, {} ).status == geometry_status_t::NON_MANIFOLD );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
    // Removing two adjacent quads is fine.
    CHECK( Replace( m, { m.Face( 30 ), m.Face( 31 ) }, {}, {} ).status == geometry_status_t::OK );
    m.Sourced();
}

TEST_CASE( "Repeated replace edits reuse pool slots instead of growing", "[geometry][meshreplace]" ) {
    Mesh m;
    m.Cube();
    common::usize faceSlots = 0u, halfEdgeSlots = 0u;
    for ( int k = 0; k < 50; ++k ) {
        geometry_mesh_face_handle_t top{};
        (void)common::GenerationPool_ForEach( &m.s.mesh.faces,
            [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t &f ) noexcept -> common::bool_t {
                if ( f.normal.z > 0.5 ) { top = h; }
                return true;
            } );
        REQUIRE( Replace( m, { top }, { Old( m.Vertex( 14 ) ), Old( m.Vertex( 15 ) ), Old( m.Vertex( 17 ) ), Old( m.Vertex( 16 ) ) },
                          { 4 } )
                     .status == geometry_status_t::OK );
        if ( k == 0 ) {
            faceSlots = m.s.mesh.faces.cSlots;
            halfEdgeSlots = m.s.mesh.halfEdges.cSlots;
        }
    }
    CHECK( m.s.mesh.faces.cSlots == faceSlots );
    CHECK( m.s.mesh.halfEdges.cSlots == halfEdgeSlots );
    m.Solid( 1.0 );
}

// ---------------------------------------------------------------------------
// Bevel
// ---------------------------------------------------------------------------

TEST_CASE( "Bevel one cube edge: chamfer and rounded", "[geometry][meshbevel]" ) {
    SECTION( "Chamfer" ) {
        Mesh m;
        m.Cube();
        common::vector_t<mesh_bevel_face_t> faces{};
        REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
        const auto r = Bevel( m, { m.Edge( 10, 11 ) }, 0.25, 1u, &faces );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cFacesReshaped == 4u ); // the faces around vertices 10 and 11
        CHECK( r.cStripFaces == 1u );
        CHECK( r.cCornerFaces == 0u );
        CHECK( r.cVerticesRemoved == 2u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 10u );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 7u );
        CHECK( faces.nCount == 5u );
        common::u32 cStrip = 0u;
        for ( common::usize i = 0; i < faces.nCount; ++i ) {
            if ( faces.pData[i].role == mesh_bevel_face_role_t::STRIP ) {
                ++cStrip;
                const vec3d_t n = EditableMesh_GetFace( &m.s.mesh, faces.pData[i].hFace )->normal;
                CHECK( n.x == Approx( 0.0 ).margin( 1e-12 ) );
                CHECK( n.y == Approx( -std::sqrt( 0.5 ) ) );
                CHECK( n.z == Approx( -std::sqrt( 0.5 ) ) );
            }
        }
        CHECK( cStrip == 1u );
        m.Solid( 1.0 - 0.25 * 0.25 / 2.0 );
        common::Vector_Shutdown( &faces );
    }
    SECTION( "Rounded, several segment counts" ) {
        for ( common::u32 seg : { 2u, 3u, 5u, 8u } ) {
            CAPTURE( seg );
            Mesh m;
            m.Cube();
            const auto r = Bevel( m, { m.Edge( 10, 11 ) }, 0.25, seg );
            REQUIRE( r.status == geometry_status_t::OK );
            CHECK( r.cStripFaces == seg );
            m.Solid( 1.0 - RoundRemoved( 0.25, seg ) );
        }
    }
}

TEST_CASE( "Bevel the top border: mitered chains", "[geometry][meshbevel]" ) {
    const double w = 0.2;
    const double frustum = w / 3.0 * ( 1.0 + ( 1 - 2 * w ) * ( 1 - 2 * w ) + ( 1 - 2 * w ) );
    const double chamfered = 1.0 - w + frustum;
    SECTION( "Chamfer" ) {
        Mesh m;
        m.Cube();
        const auto r = Bevel( m, { m.Edge( 14, 15 ), m.Edge( 15, 17 ), m.Edge( 17, 16 ), m.Edge( 16, 14 ) }, w, 1u );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cStripFaces == 4u );
        CHECK( r.cCornerFaces == 0u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 12u );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 10u );
        m.Solid( chamfered );
    }
    SECTION( "Rounded keeps more than the chamfer" ) {
        Mesh m;
        m.Cube();
        const auto r = Bevel( m, { m.Edge( 14, 15 ), m.Edge( 15, 17 ), m.Edge( 17, 16 ), m.Edge( 16, 14 ) }, w, 4u );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cStripFaces == 16u );
        const mesh_validation_result_t v = MeshValidation_Validate( &m.s.mesh );
        CHECK( v.status == geometry_status_t::OK );
        CHECK( v.nEulerCharacteristic == 2 );
        CHECK( v.fSignedVolume > chamfered );
        CHECK( v.fSignedVolume < 1.0 );
        m.Sourced();
    }
}

TEST_CASE( "Bevel all twelve cube edges", "[geometry][meshbevel]" ) {
    auto all = []( Mesh &m ) {
        return std::vector<geometry_mesh_edge_handle_t>{ m.Edge( 10, 11 ), m.Edge( 12, 13 ), m.Edge( 14, 15 ), m.Edge( 16, 17 ),
                                                         m.Edge( 10, 12 ), m.Edge( 11, 13 ), m.Edge( 14, 16 ), m.Edge( 15, 17 ),
                                                         m.Edge( 10, 14 ), m.Edge( 11, 15 ), m.Edge( 12, 16 ), m.Edge( 13, 17 ) };
    };
    const double w = 0.2;
    const double chamfered = 1.0 - 6 * w * w + 16.0 * w * w * w / 3.0;
    SECTION( "Chamfer: 6 faces, 12 strips, 8 corner triangles" ) {
        Mesh m;
        m.Cube();
        const auto r = Bevel( m, all( m ), w, 1u );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cFacesReshaped == 6u );
        CHECK( r.cStripFaces == 12u );
        CHECK( r.cCornerFaces == 8u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 24u );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 26u );
        m.Solid( chamfered );
    }
    SECTION( "Rounded box" ) {
        for ( common::u32 seg : { 2u, 3u, 6u } ) {
            CAPTURE( seg );
            Mesh m;
            m.Cube();
            const auto r = Bevel( m, all( m ), w, seg );
            REQUIRE( r.status == geometry_status_t::OK );
            CHECK( r.cStripFaces == 12u * seg );
            CHECK( r.cCornerFaces == 8u * 3u * seg );
            const mesh_validation_result_t v = MeshValidation_Validate( &m.s.mesh );
            CHECK( v.status == geometry_status_t::OK );
            CHECK( v.nEulerCharacteristic == 2 );
            // Edges keep exactly the rounded cross-section; corners are a
            // fan approximating the sphere octant, so bound the volume
            // between "every edge rounded, corners chamfer-cut" and 1.
            CHECK( v.fSignedVolume > chamfered );
            CHECK( v.fSignedVolume < 1.0 );
            m.Sourced();
        }
    }
}

TEST_CASE( "Bevel rejects what it cannot do and leaves the mesh unchanged", "[geometry][meshbevel]" ) {
    Mesh m;
    m.Cube();
    Desc before, after;
    m.Snapshot( &before.d );
    const auto e = m.Edge( 14, 15 );
    CHECK( Bevel( m, {}, 0.1, 1u ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Bevel( m, { e, e }, 0.1, 1u ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Bevel( m, { e }, 0.0, 1u ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Bevel( m, { e }, -0.1, 1u ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Bevel( m, { e }, std::nan( "" ), 1u ).status == geometry_status_t::NUMERIC_FAILURE );
    CHECK( Bevel( m, { e }, 0.1, 0u ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( Bevel( m, { e }, 0.1, kMeshBevelSegmentsMax + 1u ).status == geometry_status_t::INVALID_ARGUMENT );
    // Too wide: the top face would fold over itself.
    const auto border = std::vector<geometry_mesh_edge_handle_t>{ m.Edge( 14, 15 ), m.Edge( 15, 17 ), m.Edge( 17, 16 ), m.Edge( 16, 14 ) };
    const geometry_status_t wide = Bevel( m, border, 0.6, 1u ).status;
    CHECK( ( wide == geometry_status_t::SELF_INTERSECTING || wide == geometry_status_t::DEGENERATE ) );
    CHECK( Bevel( m, border, 0.5, 1u ).status != geometry_status_t::OK ); // exactly half: top face collapses
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );

    // A single beveled edge ending at a valence-4 vertex is not supported yet.
    const mesh_knife_point_t diag[2] = { [&] {
                                            mesh_knife_point_t p{};
                                            p.hVertex = m.Vertex( 14 );
                                            return p;
                                        }(),
                                         [&] {
                                             mesh_knife_point_t p{};
                                             p.hVertex = m.Vertex( 17 );
                                             return p;
                                         }() };
    REQUIRE( MeshKnife_Cut( &m.s.mesh, common::span_t<const mesh_knife_point_t>{ diag, 2 }, nullptr, nullptr ).status ==
             geometry_status_t::OK );
    m.Snapshot( &before.d );
    CHECK( Bevel( m, { m.Edge( 14, 15 ) }, 0.1, 1u ).status == geometry_status_t::UNSUPPORTED );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

TEST_CASE( "Bevel refuses open boundary edges", "[geometry][meshbevel]" ) {
    Mesh m;
    m.V( 0, 0, 0, 10 );
    m.V( 1, 0, 0, 11 );
    m.V( 1, 1, 0, 12 );
    m.V( 0, 1, 0, 13 );
    m.F( { 0, 1, 2, 3 }, 20 );
    m.Build();
    CHECK( Bevel( m, { m.Edge( 10, 11 ) }, 0.1, 1u ).status == geometry_status_t::UNSUPPORTED );
}

namespace {

// Planar UV / per-axis material oracle (as in the source-edit tests).
int Axis( vec3d_t n ) {
    const double ax = std::fabs( n.x ), ay = std::fabs( n.y ), az = std::fabs( n.z );
    return az >= ax && az >= ay ? 2 : ( ay >= ax ? 1 : 0 );
}
vec2d_t Planar( int axis, vec3d_t p ) {
    return axis == 2 ? vec2d_t{ p.x, p.y } : ( axis == 1 ? vec2d_t{ p.x, p.z } : vec2d_t{ p.y, p.z } );
}
common::u64 MaterialOf( vec3d_t n ) {
    const int a = Axis( n );
    const double c = a == 2 ? n.z : ( a == 1 ? n.y : n.x );
    return 10u + static_cast<common::u64>( a ) * 2u + ( c > 0.0 ? 1u : 0u );
}
vec3d_t NewellOfFace( const mesh_source_description_t &d, const mesh_source_face_t &f ) {
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
void ApplyOracle( mesh_source_description_t *pD ) {
    for ( common::usize f = 0; f < pD->faces.nCount; ++f ) {
        mesh_source_face_t &face = pD->faces.pData[f];
        const vec3d_t n = NewellOfFace( *pD, face );
        face.attributes.material.value = MaterialOf( n );
        for ( common::u32 k = 0; k < face.cCorners; ++k ) {
            mesh_source_corner_t &c = pD->corners.pData[face.iFirstCorner + k];
            c.attributes.uv0 = Planar( Axis( n ), pD->vertices.pData[c.iVertex].position );
        }
    }
}

} // namespace

TEST_CASE( "Bevel through the source keeps face identity, UVs, materials and nearby edge flags",
           "[geometry][meshbevel][meshedit]" ) {
    Mesh m;
    for ( int i = 0; i < 8; ++i ) {
        m.V( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0, 10u + static_cast<common::u64>( i ) );
    }
    m.F( { 0, 2, 3, 1 }, 20 );
    m.F( { 4, 5, 7, 6 }, 21 );
    m.F( { 0, 1, 5, 4 }, 22 );
    m.F( { 2, 6, 7, 3 }, 23 );
    m.F( { 0, 4, 6, 2 }, 24 );
    m.F( { 1, 3, 7, 5 }, 25 );
    ApplyOracle( &m.d );
    m.Build();
    mesh_edge_attributes_t hard{};
    hard.flags = MESH_EDGE_FLAG_HARD;
    // 12-13 is untouched by the bevel; 10-12 loses its end at 10 (slid).
    REQUIRE( MeshSourceEdit_TrySetEdgeAttributes( &m.s, Id( 12 ), Id( 13 ), hard, 0.0 ) == geometry_status_t::OK );
    REQUIRE( MeshSourceEdit_TrySetEdgeAttributes( &m.s, Id( 10 ), Id( 12 ), hard, 0.0 ) == geometry_status_t::OK );

    const geometry_source_id_t edge[2] = { Id( 10 ), Id( 11 ) };
    mesh_bevel_params_t params{};
    params.width = 0.25;
    params.cSegments = 3u;
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TryBevelEdges( &m.s, common::span_t<const geometry_source_id_t>{ edge, 2 }, params, &report ) ==
             geometry_status_t::OK );
    CHECK( report.stats.cFacesInheritingIdentity == 4u ); // the faces around 10 and 11
    CHECK( report.stats.cFacesWithoutParent == 0u );
    CHECK( report.stats.cCornersDefaulted == 0u );
    m.Solid( 1.0 - RoundRemoved( 0.25, 3u ) );

    // Every original face survives under its ID with the exact planar UVs
    // of its (moved) corners; strips took a real material from their source.
    m.Snapshot( &m.d );
    common::u32 cOriginal = 0u, cStrip = 0u;
    for ( common::usize f = 0; f < m.d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = m.d.faces.pData[f];
        CAPTURE( face.sourceId.value );
        if ( face.sourceId.value >= 20u && face.sourceId.value <= 25u ) {
            ++cOriginal;
            const vec3d_t n = NewellOfFace( m.d, face );
            CHECK( face.attributes.material.value == MaterialOf( n ) );
            for ( common::u32 k = 0; k < face.cCorners; ++k ) {
                const mesh_source_corner_t &c = m.d.corners.pData[face.iFirstCorner + k];
                const vec2d_t want = Planar( Axis( n ), m.d.vertices.pData[c.iVertex].position );
                CHECK( c.attributes.uv0.x == Approx( want.x ).margin( 1e-9 ) );
                CHECK( c.attributes.uv0.y == Approx( want.y ).margin( 1e-9 ) );
            }
        } else {
            ++cStrip;
            CHECK( face.attributes.material.value >= 10u );
            CHECK( face.attributes.material.value <= 15u );
        }
    }
    CHECK( cOriginal == 6u );
    CHECK( cStrip == 3u );

    // Edge flags: the untouched edge is restored; the slid edge (now from
    // the slide point to 12) keeps its flag as the surviving part of 10-12.
    common::u32 cHard = 0u;
    bool bUntouchedHard = false;
    for ( common::usize i = 0; i < m.d.edges.nCount; ++i ) {
        const mesh_source_edge_t &ed = m.d.edges.pData[i];
        if ( ed.attributes.flags != MESH_EDGE_FLAG_HARD ) { continue; }
        ++cHard;
        const common::u64 a = m.d.vertices.pData[ed.iVertexA].sourceId.value;
        const common::u64 b = m.d.vertices.pData[ed.iVertexB].sourceId.value;
        bUntouchedHard = bUntouchedHard || ( ( a == 12u && b == 13u ) || ( a == 13u && b == 12u ) );
    }
    CHECK( bUntouchedHard );
    CHECK( cHard == 2u );

    // A rejected bevel changes nothing.
    Desc before, after;
    m.Snapshot( &before.d );
    params.width = 5.0;
    const geometry_source_id_t other[2] = { Id( 16 ), Id( 17 ) };
    CHECK( MeshSourceEdit_TryBevelEdges( &m.s, common::span_t<const geometry_source_id_t>{ other, 2 }, params, nullptr ) !=
           geometry_status_t::OK );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

} // namespace cypher::editor::geometry
