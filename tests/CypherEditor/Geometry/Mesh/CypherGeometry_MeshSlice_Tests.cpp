//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSlice_Tests.cpp
//  Purpose: Tests cutting editable meshes and mesh sources with a plane:
//           slice, clip, capped clip (with and without holes), concave
//           faces, vertices on the plane, rejections, allocation-failure
//           atomicity, and a randomized volume oracle.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshSlice.h"
#include "CypherGeometry_MeshSourceSlice.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

struct Src {
    common::allocator_t allocator{};
    mesh_source_t s{};
    mesh_source_description_t d{};
    geometry_source_id_allocator_t ids{ Id( 5000 ) };
    common::u64 nextId{ 10u };
    explicit Src( const common::allocator_t *pAllocator = common::Allocator_GetSystem() ) : allocator( *pAllocator ) {
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    }
    ~Src() {
        MeshSource_Shutdown( &s );
        MeshSourceDescription_Shutdown( &d );
    }
    common::u32 V( double x, double y, double z ) {
        common::u32 i = 0;
        REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( x, y, z ), Id( nextId++ ), &i ) == geometry_status_t::OK );
        return i;
    }
    void F( std::vector<common::u32> idx ) {
        REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ idx.data(), idx.size() }, Id( nextId++ ),
                                                   mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
    }
    void Build() { REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK ); }
    // Axis-aligned box; `inward` reverses every face (a cavity's walls).
    void Box( vec3d_t lo, vec3d_t hi, bool inward = false ) {
        common::u32 v[8];
        for ( int i = 0; i < 8; ++i ) {
            v[i] = V( ( i & 1 ) ? hi.x : lo.x, ( i & 2 ) ? hi.y : lo.y, ( i & 4 ) ? hi.z : lo.z );
        }
        const common::u32 faces[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( const auto &f : faces ) {
            if ( inward ) {
                F( { v[f[3]], v[f[2]], v[f[1]], v[f[0]] } );
            } else {
                F( { v[f[0]], v[f[1]], v[f[2]], v[f[3]] } );
            }
        }
    }
    // A prism over a CCW outline in the xy plane, from z = 0 to z = h.
    void Prism( const std::vector<std::pair<double, double>> &outline, double h ) {
        const common::u32 n = static_cast<common::u32>( outline.size() );
        std::vector<common::u32> bottom, top;
        for ( const auto &p : outline ) { bottom.push_back( V( p.first, p.second, 0.0 ) ); }
        for ( const auto &p : outline ) { top.push_back( V( p.first, p.second, h ) ); }
        F( std::vector<common::u32>( bottom.rbegin(), bottom.rend() ) );
        F( top );
        for ( common::u32 i = 0; i < n; ++i ) {
            const common::u32 j = ( i + 1 ) % n;
            F( { bottom[i], bottom[j], top[j], top[i] } );
        }
    }
    void Valid() {
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        CHECK( MeshSource_Validate( &s, &allocator ).fault == mesh_source_fault_t::NONE );
        // Round trip through the canonical description.
        mesh_source_description_t a{};
        REQUIRE( MeshSourceDescription_Init( &a, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &s, &a ) == geometry_status_t::OK );
        mesh_source_t copy{};
        CHECK( MeshSource_TryBuild( &a, &allocator, &copy ) == geometry_status_t::OK );
        MeshSource_Shutdown( &copy );
        MeshSourceDescription_Shutdown( &a );
    }
    void Snapshot( mesh_source_description_t *pOut ) {
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &s, pOut ) == geometry_status_t::OK );
    }
};

struct Desc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    Desc() { REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK ); }
    ~Desc() { MeshSourceDescription_Shutdown( &d ); }
};

mesh_slice_params_t Params( vec3d_t n, double d, mesh_slice_keep_t keep = mesh_slice_keep_t::BOTH, bool bCap = false ) {
    mesh_slice_params_t p{};
    p.plane = math::planed_t{ n, d };
    p.keep = keep;
    p.bCap = bCap;
    return p;
}

double Area( const editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace ) {
    const mesh_face_record_t *pF = common::GenerationPool_Get( &pMesh->faces, hFace );
    const mesh_loop_record_t *pL = common::GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
    std::vector<vec3d_t> pts;
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( common::u32 k = 0; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = common::GenerationPool_Get( &pMesh->halfEdges, h );
        pts.push_back( common::GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position );
        h = pH->hNext;
    }
    vec3d_t n{};
    for ( std::size_t i = 0; i < pts.size(); ++i ) {
        const vec3d_t a = pts[i], b = pts[( i + 1 ) % pts.size()];
        n.x += ( a.y - b.y ) * ( a.z + b.z );
        n.y += ( a.z - b.z ) * ( a.x + b.x );
        n.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return 0.5 * std::sqrt( n.x * n.x + n.y * n.y + n.z * n.z );
}

double TotalArea( const editable_mesh_t *pMesh ) {
    double total = 0.0;
    (void)common::GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> common::bool_t {
            total += Area( pMesh, h );
            return true;
        } );
    return total;
}

struct slice_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *SliceFailureAllocate( void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<slice_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) { return nullptr; }
    void *pMemory = common::Allocator_Allocate( common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessfulAllocations; }
    return pMemory;
}

void SliceFailureFree( void *pUserData, void *pMemory, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<slice_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) { ++pState->cFrees; }
    common::Allocator_Free( common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

} // namespace

TEST_CASE( "Slicing a box through its middle cuts the four sides and stays closed", "[geometry][meshslice]" ) {
    Src m;
    m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    m.Build();
    common::vector_t<mesh_slice_face_t> faces{};
    REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
    const mesh_slice_result_t r = MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 2 ), -1.0 ), &faces );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cFacesCut == 4u );
    CHECK( r.cPieces == 8u );
    CHECK( r.cVerticesCreated == 4u );
    CHECK( r.cCapFaces == 0u );
    CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 10u );
    CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 12u );
    CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 20u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
    CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 1.0 ) );
    REQUIRE( faces.nCount == 8u );
    common::u32 cLargest = 0u;
    for ( common::usize i = 0; i < faces.nCount; ++i ) {
        CHECK( faces.pData[i].role == mesh_slice_face_role_t::PIECE );
        CHECK( Area( &m.s.mesh, faces.pData[i].hFace ) == Approx( 0.5 ) );
        cLargest += faces.pData[i].bLargestPiece ? 1u : 0u;
    }
    CHECK( cLargest == 4u ); // one per cut face, ties to the first piece
    // The new vertices sit exactly halfway up.
    (void)common::GenerationPool_ForEach( &m.s.mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t, const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            CHECK( ( v.position.z == 0.0 || v.position.z == 0.5 || v.position.z == 1.0 ) );
            return true;
        } );
    m.Valid();
    common::Vector_Shutdown( &faces );
}

TEST_CASE( "Clipping keeps one side; capping closes it facing the removed side", "[geometry][meshslice]" ) {
    SECTION( "Keep the back half, open" ) {
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Build();
        const mesh_slice_result_t r =
            MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -0.5, mesh_slice_keep_t::BACK ), nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cFacesCut == 4u );
        CHECK( r.cFacesDiscarded == 1u );
        CHECK( r.cPieces == 4u );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 5u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 8u );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 4u );
        m.Valid();
    }
    SECTION( "Keep the back half, capped" ) {
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Build();
        common::vector_t<mesh_slice_face_t> faces{};
        REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
        const mesh_slice_result_t r =
            MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -0.5, mesh_slice_keep_t::BACK, true ), &faces );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cCapFaces == 1u );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 6u );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
        CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 0.5 ) );
        for ( common::usize i = 0; i < faces.nCount; ++i ) {
            if ( faces.pData[i].role != mesh_slice_face_role_t::CAP ) { continue; }
            CHECK( common::GenerationPool_Get( &m.s.mesh.faces, faces.pData[i].hFace )->normal.z == Approx( 1.0 ) );
            CHECK( Area( &m.s.mesh, faces.pData[i].hFace ) == Approx( 1.0 ) );
        }
        m.Valid();
        common::Vector_Shutdown( &faces );
    }
    SECTION( "Keep the front half, capped" ) {
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Build();
        const mesh_slice_result_t r =
            MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -0.25, mesh_slice_keep_t::FRONT, true ), nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 0.75 ) );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
        m.Valid();
    }
}

TEST_CASE( "A plane through vertices cuts along existing corners and caps over existing edges", "[geometry][meshslice]" ) {
    // x = y passes through the vertical edges at (0, 0) and (1, 1): the top
    // and bottom split along their diagonals with no new vertex.
    SECTION( "Slice" ) {
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Build();
        const mesh_slice_result_t r = MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 1, -1, 0 ), 0.0 ), nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cFacesCut == 2u );
        CHECK( r.cPieces == 4u );
        CHECK( r.cVerticesCreated == 0u );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 8u );
        CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 14u );
        CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 1.0 ) );
        m.Valid();
    }
    SECTION( "Capped clip is a triangular prism" ) {
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Build();
        const mesh_slice_result_t r =
            MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 1, -1, 0 ), 0.0, mesh_slice_keep_t::FRONT, true ), nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cCapFaces == 1u );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 5u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 6u );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
        CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 0.5 ) );
        m.Valid();
    }
    SECTION( "Clipping at a face's own plane keeps the solid behind it" ) {
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Build();
        Desc before, after;
        m.Snapshot( &before.d );
        const mesh_slice_result_t r =
            MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -1.0, mesh_slice_keep_t::BACK, true ), nullptr );
        CHECK( r.status == geometry_status_t::OK );
        CHECK( r.cFacesCut == 0u );
        m.Snapshot( &after.d );
        CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
        // Keeping the (empty) front side would remove everything.
        CHECK( MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -1.0, mesh_slice_keep_t::FRONT ), nullptr ).status ==
               geometry_status_t::DEGENERATE );
    }
}

TEST_CASE( "Concave faces fall into several pieces", "[geometry][meshslice]" ) {
    // A U-shaped sheet (area 7); the line y = 2 crosses both prongs.
    auto uSheet = []( Src &m ) {
        const common::u32 v[] = { m.V( 0, 0, 0 ), m.V( 3, 0, 0 ), m.V( 3, 3, 0 ), m.V( 2, 3, 0 ),
                                  m.V( 2, 1, 0 ), m.V( 1, 1, 0 ), m.V( 1, 3, 0 ), m.V( 0, 3, 0 ) };
        m.F( { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7] } );
        m.Build();
    };
    SECTION( "Slice: the base and two prong tips" ) {
        Src m;
        uSheet( m );
        common::vector_t<mesh_slice_face_t> faces{};
        REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
        const mesh_slice_result_t r = MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 1, 0 ), -2.0 ), &faces );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cPieces == 3u );
        CHECK( r.cVerticesCreated == 4u );
        REQUIRE( faces.nCount == 3u );
        double total = 0.0;
        for ( common::usize i = 0; i < faces.nCount; ++i ) {
            const double a = Area( &m.s.mesh, faces.pData[i].hFace );
            total += a;
            CHECK( faces.pData[i].bLargestPiece == ( a > 2.0 ) );
            CHECK( ( a == Approx( 5.0 ) || a == Approx( 1.0 ) ) );
        }
        CHECK( total == Approx( 7.0 ) );
        CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
        m.Valid();
        common::Vector_Shutdown( &faces );
    }
    SECTION( "Clip: keeping the prong side leaves two separate tips" ) {
        Src m;
        uSheet( m );
        const mesh_slice_result_t r =
            MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 1, 0 ), -2.0, mesh_slice_keep_t::FRONT ), nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cPieces == 2u );
        CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 2u );
        CHECK( TotalArea( &m.s.mesh ) == Approx( 2.0 ) );
        m.Valid();
    }
    SECTION( "A reflex corner touching the line splits the far side in two" ) {
        // Pentagon with a notch whose tip (1, 1) touches y = 1 from above.
        Src m;
        const common::u32 v[] = { m.V( 0, 0, 0 ), m.V( 2, 0, 0 ), m.V( 2, 2, 0 ), m.V( 1, 1, 0 ), m.V( 0, 2, 0 ) };
        m.F( { v[0], v[1], v[2], v[3], v[4] } );
        m.Build();
        common::vector_t<mesh_slice_face_t> faces{};
        REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
        const mesh_slice_result_t r = MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 1, 0 ), -1.0 ), &faces );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cPieces == 3u );
        CHECK( r.cVerticesCreated == 2u );
        std::vector<double> areas;
        for ( common::usize i = 0; i < faces.nCount; ++i ) { areas.push_back( Area( &m.s.mesh, faces.pData[i].hFace ) ); }
        std::sort( areas.begin(), areas.end() );
        REQUIRE( areas.size() == 3u );
        CHECK( areas[0] == Approx( 0.5 ) );
        CHECK( areas[1] == Approx( 0.5 ) );
        CHECK( areas[2] == Approx( 2.0 ) );
        m.Valid();
        common::Vector_Shutdown( &faces );
    }
    SECTION( "A convex corner touching the line from outside is only a corner" ) {
        // A spike tip at (3, 2) touches y = 2 from below; the line also
        // crosses the left column, which is the only stretch cut.
        Src m;
        const common::u32 v[] = { m.V( 0, 0, 0 ), m.V( 4, 0, 0 ), m.V( 4, 1, 0 ), m.V( 3, 2, 0 ),
                                  m.V( 2, 1, 0 ), m.V( 1, 1, 0 ), m.V( 1, 3, 0 ), m.V( 0, 3, 0 ) };
        m.F( { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7] } );
        m.Build();
        const double before = TotalArea( &m.s.mesh );
        const mesh_slice_result_t r = MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 1, 0 ), -2.0 ), nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cPieces == 2u );
        CHECK( r.cVerticesCreated == 2u );
        CHECK( TotalArea( &m.s.mesh ) == Approx( before ) );
        m.Valid();
    }
}

TEST_CASE( "Capping a clipped hollow box triangulates the wall ring", "[geometry][meshslice]" ) {
    Src m;
    m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 4, 4, 4 ) );
    m.Box( Vec3d_Make( 1, 1, 1 ), Vec3d_Make( 3, 3, 3 ), true );
    m.Build();
    REQUIRE( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 56.0 ) );
    common::vector_t<mesh_slice_face_t> faces{};
    REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
    const mesh_slice_result_t r =
        MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -2.0, mesh_slice_keep_t::BACK, true ), &faces );
    REQUIRE( r.status == geometry_status_t::OK );
    // 8 outline corners with one hole: 8 + 2 * 1 - 2 triangles.
    CHECK( r.cCapFaces == 8u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
    CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 28.0 ) );
    double capArea = 0.0;
    for ( common::usize i = 0; i < faces.nCount; ++i ) {
        if ( faces.pData[i].role != mesh_slice_face_role_t::CAP ) { continue; }
        CHECK( common::GenerationPool_Get( &m.s.mesh.faces, faces.pData[i].hFace )->normal.z == Approx( 1.0 ) );
        capArea += Area( &m.s.mesh, faces.pData[i].hFace );
    }
    CHECK( capArea == Approx( 12.0 ) );
    m.Valid();
    common::Vector_Shutdown( &faces );
}

TEST_CASE( "Slice rejects bad parameters and leaves the mesh unchanged", "[geometry][meshslice]" ) {
    Src m;
    m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    m.Build();
    Desc before, after;
    m.Snapshot( &before.d );
    CHECK( MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -0.5, mesh_slice_keep_t::BOTH, true ), nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 0 ), -0.5 ), nullptr ).status == geometry_status_t::DEGENERATE );
    CHECK( MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, std::nan( "" ) ), -0.5 ), nullptr ).status ==
           geometry_status_t::NUMERIC_FAILURE );
    mesh_slice_params_t negative = Params( Vec3d_Make( 0, 0, 1 ), -0.5 );
    negative.onPlaneTolerance = -1.0;
    CHECK( MeshSlice_ByPlane( &m.s.mesh, negative, nullptr ).status == geometry_status_t::INVALID_ARGUMENT );
    // Clipping everything away.
    CHECK( MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -2.0, mesh_slice_keep_t::FRONT ), nullptr ).status ==
           geometry_status_t::DEGENERATE );
    // A plane that misses is a successful no-op.
    const mesh_slice_result_t miss = MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -2.0 ), nullptr );
    CHECK( miss.status == geometry_status_t::OK );
    CHECK( miss.cFacesCut == 0u );
    // A capped clip of an open sheet has no closed opening to cap.
    Src sheet;
    const common::u32 v[] = { sheet.V( 0, 0, 0 ), sheet.V( 1, 0, 0 ), sheet.V( 1, 1, 0 ), sheet.V( 0, 1, 0 ) };
    sheet.F( { v[0], v[1], v[2], v[3] } );
    sheet.Build();
    CHECK( MeshSlice_ByPlane( &sheet.s.mesh, Params( Vec3d_Make( 1, 0, 0 ), -0.5, mesh_slice_keep_t::BACK, true ), nullptr ).status ==
           geometry_status_t::DEGENERATE );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

TEST_CASE( "Source slice: pieces keep material and UVs, the largest keeps the ID", "[geometry][meshslice][meshedit]" ) {
    Src m;
    m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    // Planar UVs per face orientation: a corner's UV is its position
    // dropped onto the face's plane, so the expected UV of any point on a
    // face is known.
    auto uvOf = []( vec3d_t p, vec3d_t n ) {
        if ( std::fabs( n.x ) > 0.5 ) { return math::vec2d_t{ p.y, p.z }; }
        if ( std::fabs( n.y ) > 0.5 ) { return math::vec2d_t{ p.x, p.z }; }
        return math::vec2d_t{ p.x, p.y };
    };
    for ( common::usize f = 0; f < m.d.faces.nCount; ++f ) {
        m.d.faces.pData[f].attributes.material.value = 40u + static_cast<common::u32>( f );
    }
    m.Build();
    // Fill UVs through the description of the built source (face normals are known there).
    {
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, &m.allocator, Id( 1 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &m.s, &d ) == geometry_status_t::OK );
        for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
            const mesh_source_face_t &face = d.faces.pData[f];
            geometry_mesh_face_handle_t h{};
            REQUIRE( MeshSource_TryFindFace( &m.s, face.sourceId, &h ) );
            const vec3d_t n = common::GenerationPool_Get( &m.s.mesh.faces, h )->normal;
            for ( common::u32 k = 0; k < face.cCorners; ++k ) {
                mesh_source_corner_t &c = d.corners.pData[face.iFirstCorner + k];
                c.attributes.uv0 = uvOf( d.vertices.pData[c.iVertex].position, n );
            }
        }
        MeshSource_Shutdown( &m.s );
        REQUIRE( MeshSource_TryBuild( &d, &m.allocator, &m.s ) == geometry_status_t::OK );
        MeshSourceDescription_Shutdown( &d );
    }
    std::vector<geometry_source_id_t> sideIds;
    (void)common::GenerationPool_ForEach( &m.s.mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t &f ) noexcept -> common::bool_t {
            if ( std::fabs( f.normal.z ) < 0.5 ) { sideIds.push_back( MeshSource_FaceId( &m.s, h ) ); }
            return true;
        } );
    REQUIRE( sideIds.size() == 4u );

    mesh_edit_report_t report{};
    common::vector_t<mesh_slice_face_t> faces{};
    REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
    REQUIRE( MeshSourceEdit_TrySliceByPlane( &m.s, Params( Vec3d_Make( 0, 0, 1 ), -0.25, mesh_slice_keep_t::BACK, true ), &faces,
                                             &report ) == geometry_status_t::OK );
    CHECK( report.stats.cFacesFromParent == 4u );
    CHECK( report.stats.cFacesInheritingIdentity == 4u );
    CHECK( report.stats.cFacesWithoutParent == 1u ); // the cap
    // Bottom face untouched (4 restored); each kept piece copies its 2
    // original corners and interpolates its 2 cut corners; the cap's 4
    // corners have no parent.
    CHECK( report.stats.cCornersRestored == 4u );
    CHECK( report.stats.cCornersCopied == 8u );
    CHECK( report.stats.cCornersInterpolated == 8u );
    CHECK( report.stats.cCornersDefaulted == 4u );
    for ( const geometry_source_id_t id : sideIds ) {
        geometry_mesh_face_handle_t h{};
        CHECK( MeshSource_TryFindFace( &m.s, id, &h ) );
    }
    m.Valid();

    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &m.allocator, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &m.s, &d ) == geometry_status_t::OK );
    CHECK( d.faces.nCount == 6u );
    for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        geometry_mesh_face_handle_t h{};
        REQUIRE( MeshSource_TryFindFace( &m.s, face.sourceId, &h ) );
        const vec3d_t n = common::GenerationPool_Get( &m.s.mesh.faces, h )->normal;
        if ( n.z > 0.5 ) {
            CHECK( face.attributes.material.value == 0u ); // the cap has no parent
            continue;
        }
        CHECK( face.attributes.material.value >= 40u );
        for ( common::u32 k = 0; k < face.cCorners; ++k ) {
            const mesh_source_corner_t &c = d.corners.pData[face.iFirstCorner + k];
            const math::vec2d_t want = uvOf( d.vertices.pData[c.iVertex].position, n );
            CHECK( c.attributes.uv0.x == Approx( want.x ).margin( 1e-12 ) );
            CHECK( c.attributes.uv0.y == Approx( want.y ).margin( 1e-12 ) );
        }
    }
    MeshSourceDescription_Shutdown( &d );
    common::Vector_Shutdown( &faces );
}

TEST_CASE( "Every slice allocation failure is atomic", "[geometry][meshslice][allocation]" ) {
    auto run = []( Src &m ) {
        return MeshSlice_ByPlane( &m.s.mesh, Params( Vec3d_Make( 0, 0, 1 ), -2.0, mesh_slice_keep_t::BACK, true ), nullptr );
    };
    auto build = []( Src &m ) {
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 4, 4, 4 ) );
        m.Box( Vec3d_Make( 1, 1, 1 ), Vec3d_Make( 3, 3, 3 ), true );
        m.Build();
    };
    common::usize cOperationAllocations = 0u;
    slice_failure_allocator_state_t probeState{};
    common::allocator_t probeAllocator{ SliceFailureAllocate, nullptr, SliceFailureFree, &probeState };
    {
        Src probe( &probeAllocator );
        build( probe );
        const common::usize cBefore = probeState.cAllocationCalls;
        REQUIRE( run( probe ).status == geometry_status_t::OK );
        cOperationAllocations = probeState.cAllocationCalls - cBefore;
    }
    REQUIRE( probeState.cSuccessfulAllocations == probeState.cFrees );
    REQUIRE( cOperationAllocations > 0u );
    for ( common::usize iFailure = 1u; iFailure <= cOperationAllocations; ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        slice_failure_allocator_state_t state{};
        common::allocator_t allocator{ SliceFailureAllocate, nullptr, SliceFailureFree, &state };
        {
            Src m( &allocator );
            build( m );
            Desc before, after;
            m.Snapshot( &before.d );
            state.iFailOnCall = state.cAllocationCalls + iFailure;
            const mesh_slice_result_t result = run( m );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( result.status == geometry_status_t::ALLOCATION_FAILED );
            m.Snapshot( &after.d );
            CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

TEST_CASE( "Random planes: slicing keeps the volume, the two capped halves add up to it", "[geometry][meshslice]" ) {
    // An L-shaped prism: its top and bottom are concave hexagons.
    const std::vector<std::pair<double, double>> outline = { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 2 }, { 0, 2 } };
    std::mt19937_64 rng( 0x5eedu );
    std::uniform_real_distribution<double> unit( -1.0, 1.0 ), inside( 0.05, 0.95 );
    int cChecked = 0;
    for ( int trial = 0; trial < 60; ++trial ) {
        vec3d_t n = Vec3d_Make( unit( rng ), unit( rng ), unit( rng ) );
        const double len = std::sqrt( n.x * n.x + n.y * n.y + n.z * n.z );
        if ( len < 0.1 ) { continue; }
        n = Vec3d_Make( n.x / len, n.y / len, n.z / len );
        // Through a point of the L's footprint so the plane cuts it.
        const vec3d_t p = trial % 2 == 0 ? Vec3d_Make( 2.0 * inside( rng ), inside( rng ), inside( rng ) )
                                         : Vec3d_Make( inside( rng ), 2.0 * inside( rng ), inside( rng ) );
        const double d = -( n.x * p.x + n.y * p.y + n.z * p.z );
        CAPTURE( trial, n.x, n.y, n.z, d );

        Src whole;
        whole.Prism( outline, 1.0 );
        whole.Build();
        REQUIRE( MeshSlice_ByPlane( &whole.s.mesh, Params( n, d ), nullptr ).status == geometry_status_t::OK );
        CHECK( EditableMesh_SignedVolume( &whole.s.mesh ) == Approx( 3.0 ) );
        CHECK( MeshBoundary_CountBoundaryEdges( &whole.s.mesh ) == 0u );
        whole.Valid();

        double halves = 0.0;
        for ( const mesh_slice_keep_t keep : { mesh_slice_keep_t::FRONT, mesh_slice_keep_t::BACK } ) {
            Src half;
            half.Prism( outline, 1.0 );
            half.Build();
            REQUIRE( MeshSlice_ByPlane( &half.s.mesh, Params( n, d, keep, true ), nullptr ).status == geometry_status_t::OK );
            CHECK( MeshBoundary_CountBoundaryEdges( &half.s.mesh ) == 0u );
            const double v = EditableMesh_SignedVolume( &half.s.mesh );
            CHECK( v > 0.0 );
            halves += v;
            half.Valid();
        }
        CHECK( halves == Approx( 3.0 ).margin( 1e-9 ) );
        ++cChecked;
    }
    CHECK( cChecked > 40 );
}

} // namespace cypher::editor::geometry
