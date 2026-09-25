//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshQuadSlice_Tests.cpp
//  Purpose: Tests Quad Slice: grid counts, watertight neighbours, shared
//           edges between sliced quads, identity and UVs at the source
//           level, rejections, and allocation-failure atomicity.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshQuadSlice.h"
#include "CypherGeometry_MeshSourceTopology.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
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
    common::u64 nextVertexId{ 100u };
    common::u64 nextFaceId{ 1000u };
    explicit Src( const common::allocator_t *pAllocator = common::Allocator_GetSystem() ) : allocator( *pAllocator ) {
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    }
    ~Src() {
        MeshSource_Shutdown( &s );
        MeshSourceDescription_Shutdown( &d );
    }
    common::u32 V( double x, double y, double z ) {
        common::u32 i = 0;
        REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( x, y, z ), Id( nextVertexId++ ), &i ) == geometry_status_t::OK );
        return i;
    }
    void F( std::vector<common::u32> idx ) {
        REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ idx.data(), idx.size() }, Id( nextFaceId++ ),
                                                   mesh_face_attributes_t{}, nullptr ) == geometry_status_t::OK );
    }
    void Build() { REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK ); }
    // Faces in order: -z, +z, -y, +y, -x, +x.
    void Box() {
        common::u32 v[8];
        for ( int i = 0; i < 8; ++i ) { v[i] = V( ( i & 1 ) ? 1 : 0, ( i & 2 ) ? 1 : 0, ( i & 4 ) ? 1 : 0 ); }
        const common::u32 faces[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( const auto &f : faces ) { F( { v[f[0]], v[f[1]], v[f[2]], v[f[3]] } ); }
    }
    geometry_mesh_face_handle_t Face( common::u64 id ) {
        geometry_mesh_face_handle_t h{};
        REQUIRE( MeshSource_TryFindFace( &s, Id( id ), &h ) );
        return h;
    }
    void Valid() {
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        CHECK( MeshSource_Validate( &s, &allocator ).fault == mesh_source_fault_t::NONE );
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

double FaceArea( const editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace ) {
    const mesh_loop_record_t *pL = common::GenerationPool_Get( &pMesh->loops, common::GenerationPool_Get( &pMesh->faces, hFace )->hOuterLoop );
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

struct slice_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *FailAllocate( void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<slice_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) { return nullptr; }
    void *pMemory = common::Allocator_Allocate( common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessfulAllocations; }
    return pMemory;
}

void FailFree( void *pUserData, void *pMemory, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<slice_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) { ++pState->cFrees; }
    common::Allocator_Free( common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

} // namespace

TEST_CASE( "Quad slice cuts a quad into an even grid", "[geometry][quadslice]" ) {
    Src m;
    const common::u32 a = m.V( 0, 0, 0 ), b = m.V( 3, 0, 0 ), c = m.V( 3, 2, 0 ), d = m.V( 0, 2, 0 );
    m.F( { a, b, c, d } );
    m.Build();
    common::vector_t<mesh_quad_slice_face_t> faces{};
    REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
    const geometry_mesh_face_handle_t quad = m.Face( 1000 );
    const mesh_quad_slice_result_t r = MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &quad, 1 }, 3u, 2u, &faces );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cQuadsSliced == 1u );
    CHECK( r.cNeighborsRebuilt == 0u );
    CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 6u );
    CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 12u );
    REQUIRE( faces.nCount == 6u );
    common::u32 cIdentity = 0u;
    for ( common::usize i = 0; i < faces.nCount; ++i ) {
        CHECK( FaceArea( &m.s.mesh, faces.pData[i].hFace ) == Approx( 1.0 ) ); // 3 x 2 in 1 x 1 cells
        CHECK( common::GenerationPool_Get( &m.s.mesh.faces, faces.pData[i].hFace )->normal.z == Approx( 1.0 ) );
        cIdentity += faces.pData[i].bKeepsIdentity ? 1u : 0u;
    }
    CHECK( cIdentity == 1u );
    m.Valid();
    common::Vector_Shutdown( &faces );
}

TEST_CASE( "Slicing a box's top rebuilds the sides without T-junctions", "[geometry][quadslice]" ) {
    Src m;
    m.Box();
    m.Build();
    const geometry_mesh_face_handle_t top = m.Face( 1001 );
    common::vector_t<mesh_quad_slice_face_t> faces{};
    REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
    const mesh_quad_slice_result_t r = MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 }, 2u, 2u, &faces );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cNeighborsRebuilt == 4u );
    CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 9u );
    CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 13u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
    CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 1.0 ) );
    // The four sides were rebuilt with one extra corner each (identity is
    // carried by the source-level wrapper; see the source test below).
    common::u32 cNeighbors = 0u;
    for ( common::usize i = 0; i < faces.nCount; ++i ) {
        if ( faces.pData[i].role != mesh_quad_slice_role_t::NEIGHBOR ) { continue; }
        ++cNeighbors;
        const geometry_mesh_face_handle_t h = faces.pData[i].hFace;
        CHECK( common::GenerationPool_Get( &m.s.mesh.loops, common::GenerationPool_Get( &m.s.mesh.faces, h )->hOuterLoop )->cHalfEdges == 5u );
        CHECK( FaceArea( &m.s.mesh, h ) == Approx( 1.0 ) );
    }
    CHECK( cNeighbors == 4u );
    m.Valid();
    common::Vector_Shutdown( &faces );
}

TEST_CASE( "Neighbouring sliced quads share their edge points", "[geometry][quadslice]" ) {
    SECTION( "Consistent orientation, uneven counts" ) {
        Src m;
        const common::u32 a = m.V( 0, 0, 0 ), b = m.V( 1, 0, 0 ), c = m.V( 2, 0, 0 ), d = m.V( 0, 1, 0 ), e = m.V( 1, 1, 0 ), f = m.V( 2, 1, 0 );
        m.F( { a, b, e, d } );
        m.F( { b, c, f, e } );
        m.Build();
        const geometry_mesh_face_handle_t both[] = { m.Face( 1000 ), m.Face( 1001 ) };
        // The shared edge b-e is a V edge of both quads.
        REQUIRE( MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ both, 2 }, 3u, 2u, nullptr ).status ==
                 geometry_status_t::OK );
        CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 12u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 7u * 3u );
        CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 2u * ( 6u + 2u ) );
        m.Valid();
    }
    SECTION( "A shared edge that is U for one quad and V for the other" ) {
        Src m;
        const common::u32 a = m.V( 0, 0, 0 ), b = m.V( 1, 0, 0 ), c = m.V( 2, 0, 0 ), d = m.V( 0, 1, 0 ), e = m.V( 1, 1, 0 ), f = m.V( 2, 1, 0 );
        m.F( { a, b, e, d } );
        m.F( { e, b, c, f } ); // starts on the shared edge: it is this quad's U edge
        m.Build();
        Desc before, after;
        m.Snapshot( &before.d );
        const geometry_mesh_face_handle_t both[] = { m.Face( 1000 ), m.Face( 1001 ) };
        CHECK( MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ both, 2 }, 3u, 2u, nullptr ).status ==
               geometry_status_t::INVALID_ARGUMENT );
        m.Snapshot( &after.d );
        CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
        // With equal counts every edge agrees.
        const geometry_mesh_face_handle_t again[] = { m.Face( 1000 ), m.Face( 1001 ) };
        CHECK( MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ again, 2 }, 2u, 2u, nullptr ).status ==
               geometry_status_t::OK );
        m.Valid();
    }
}

TEST_CASE( "Quad slice rejects bad input and leaves the mesh unchanged", "[geometry][quadslice]" ) {
    Src m;
    m.Box();
    const common::u32 t0 = m.V( 3, 0, 0 ), t1 = m.V( 4, 0, 0 ), t2 = m.V( 3, 1, 0 );
    m.F( { t0, t1, t2 } );
    m.Build();
    Desc before, after;
    m.Snapshot( &before.d );
    const geometry_mesh_face_handle_t tri = m.Face( 1006 ), top = m.Face( 1001 );
    CHECK( MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &tri, 1 }, 2u, 2u, nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 }, 0u, 2u, nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 }, 65u, 2u, nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    const geometry_mesh_face_handle_t twice[] = { top, top };
    CHECK( MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ twice, 2 }, 2u, 2u, nullptr ).status ==
           geometry_status_t::INVALID_HANDLE );
    const mesh_quad_slice_result_t none = MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 }, 1u, 1u, nullptr );
    CHECK( none.status == geometry_status_t::OK );
    CHECK( none.cQuadsSliced == 0u );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

TEST_CASE( "Source quad slice keeps identity and interpolates UVs", "[geometry][quadslice][meshedit]" ) {
    Src m;
    m.Box();
    for ( common::usize i = 0; i < m.d.corners.nCount; ++i ) {
        const vec3d_t p = m.d.vertices.pData[m.d.corners.pData[i].iVertex].position;
        m.d.corners.pData[i].attributes.uv0 = math::vec2d_t{ p.x + 2.0 * p.z, p.y - p.z };
    }
    m.Build();
    const geometry_source_id_t ids[] = { Id( 1001 ) };
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TryQuadSlice( &m.s, common::span_t<const geometry_source_id_t>{ ids, 1 }, 4u, 3u, &report ) == geometry_status_t::OK );
    CHECK( report.stats.cCornersDefaulted == 0u );
    CHECK( report.stats.cFacesInheritingIdentity == 1u + 4u ); // one cell + the four sides
    geometry_mesh_face_handle_t h{};
    CHECK( MeshSource_TryFindFace( &m.s, Id( 1001 ), &h ) );
    m.Valid();
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &m.allocator, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &m.s, &d ) == geometry_status_t::OK );
    CHECK( d.faces.nCount == 5u + 12u );
    for ( common::usize i = 0; i < d.corners.nCount; ++i ) {
        const vec3d_t p = d.vertices.pData[d.corners.pData[i].iVertex].position;
        CHECK( d.corners.pData[i].attributes.uv0.x == Approx( p.x + 2.0 * p.z ).margin( 1e-12 ) );
        CHECK( d.corners.pData[i].attributes.uv0.y == Approx( p.y - p.z ).margin( 1e-12 ) );
    }
    MeshSourceDescription_Shutdown( &d );
}

TEST_CASE( "Every quad slice allocation failure is atomic", "[geometry][quadslice][allocation]" ) {
    auto run = []( Src &m ) {
        const geometry_mesh_face_handle_t top = m.Face( 1001 );
        return MeshQuadSlice_Faces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 }, 2u, 3u, nullptr );
    };
    common::usize cOperationAllocations = 0u;
    slice_failure_allocator_state_t probeState{};
    common::allocator_t probeAllocator{ FailAllocate, nullptr, FailFree, &probeState };
    {
        Src probe( &probeAllocator );
        probe.Box();
        probe.Build();
        const common::usize cBefore = probeState.cAllocationCalls;
        REQUIRE( run( probe ).status == geometry_status_t::OK );
        cOperationAllocations = probeState.cAllocationCalls - cBefore;
    }
    REQUIRE( probeState.cSuccessfulAllocations == probeState.cFrees );
    for ( common::usize iFailure = 1u; iFailure <= cOperationAllocations; ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        slice_failure_allocator_state_t state{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
        {
            Src m( &allocator );
            m.Box();
            m.Build();
            Desc before, after;
            m.Snapshot( &before.d );
            state.iFailOnCall = state.cAllocationCalls + iFailure;
            CHECK( run( m ).status == geometry_status_t::ALLOCATION_FAILED );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            m.Snapshot( &after.d );
            CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

} // namespace cypher::editor::geometry
