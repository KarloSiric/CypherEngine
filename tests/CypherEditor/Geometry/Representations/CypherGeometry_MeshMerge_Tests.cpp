//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshMerge_Tests.cpp
//  Purpose: Tests vertex merging, edge sewing, and merge by distance on
//           editable meshes and mesh sources: joins, collapses, fold
//           rejection, identity, and allocation-failure atomicity.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshMerge.h"
#include "CypherGeometry_MeshSourceMerge.h"
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
    // Vertex IDs are 100, 101, ... in creation order.
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
    void Box( vec3d_t lo, vec3d_t hi ) {
        common::u32 v[8];
        for ( int i = 0; i < 8; ++i ) {
            v[i] = V( ( i & 1 ) ? hi.x : lo.x, ( i & 2 ) ? hi.y : lo.y, ( i & 4 ) ? hi.z : lo.z );
        }
        const common::u32 faces[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( const auto &f : faces ) { F( { v[f[0]], v[f[1]], v[f[2]], v[f[3]] } ); }
    }
    geometry_mesh_vertex_handle_t Vertex( common::u64 id ) {
        geometry_mesh_vertex_handle_t h{};
        REQUIRE( MeshSource_TryFindVertex( &s, Id( id ), &h ) );
        return h;
    }
    geometry_mesh_edge_handle_t Edge( common::u64 a, common::u64 b ) {
        geometry_mesh_edge_handle_t h{};
        REQUIRE( MeshSourceEdit_TryFindEdge( &s, Id( a ), Id( b ), &h ) );
        return h;
    }
    void Valid() {
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        CHECK( MeshSource_Validate( &s, &allocator ).fault == mesh_source_fault_t::NONE );
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

// Two unit quads side by side with separate vertices along x = 1.
// IDs: left 100..103, right 104..107; right's 104 = (1,0), 107 = (1,1).
void TwoQuads( Src &m, bool bRightFlipped = false ) {
    const common::u32 a = m.V( 0, 0, 0 ), b = m.V( 1, 0, 0 ), c = m.V( 1, 1, 0 ), d = m.V( 0, 1, 0 );
    const common::u32 e = m.V( 1, 0, 0 ), f = m.V( 2, 0, 0 ), g = m.V( 2, 1, 0 ), h = m.V( 1, 1, 0 );
    m.F( { a, b, c, d } );
    if ( bRightFlipped ) {
        m.F( { h, g, f, e } );
    } else {
        m.F( { e, f, g, h } );
    }
    m.Build();
}

struct merge_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *MergeFailureAllocate( void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<merge_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) { return nullptr; }
    void *pMemory = common::Allocator_Allocate( common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessfulAllocations; }
    return pMemory;
}

void MergeFailureFree( void *pUserData, void *pMemory, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<merge_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) { ++pState->cFrees; }
    common::Allocator_Free( common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

} // namespace

TEST_CASE( "Sewing two separate quads along a coincident edge joins them", "[geometry][meshmerge]" ) {
    Src m;
    TwoQuads( m );
    REQUIRE( EditableMesh_ShellCount( &m.s.mesh ) == 2u );
    const geometry_mesh_edge_handle_t pair[] = { m.Edge( 101, 102 ), m.Edge( 107, 104 ) };
    const mesh_merge_result_t r =
        MeshMerge_SewEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ pair, 2 }, mesh_merge_target_t::CENTER, nullptr );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cVerticesMerged == 2u );
    CHECK( r.cFacesRebuilt == 2u );
    CHECK( r.cEdgesJoined == 1u );
    CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 6u );
    CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 7u );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 6u );
    // The survivors are the first edge's ends, and keep their IDs.
    CHECK( m.Vertex( 101 ).nSlot != CY_INVALID_INDEX );
    CHECK( m.Vertex( 102 ).nSlot != CY_INVALID_INDEX );
    geometry_mesh_vertex_handle_t gone{};
    CHECK_FALSE( MeshSource_TryFindVertex( &m.s, Id( 104 ), &gone ) );
    m.Valid();
}

TEST_CASE( "Sewing rejects edges whose faces disagree on orientation", "[geometry][meshmerge]" ) {
    Src m;
    TwoQuads( m, true );
    Desc before, after;
    m.Snapshot( &before.d );
    const geometry_mesh_edge_handle_t pair[] = { m.Edge( 101, 102 ), m.Edge( 107, 104 ) };
    CHECK( MeshMerge_SewEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ pair, 2 }, mesh_merge_target_t::CENTER,
                               nullptr )
               .status == geometry_status_t::NON_MANIFOLD );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

TEST_CASE( "Sewing zips a slit, including from a shared end", "[geometry][meshmerge]" ) {
    SECTION( "A square tube whose last seam is open" ) {
        // Bottom ring 100..104 and top ring 105..109; 104 / 109 repeat 100 / 105.
        Src m;
        const double xs[5] = { 0, 1, 1, 0, 0 }, ys[5] = { 0, 0, 1, 1, 0 };
        common::u32 lo[5], hi[5];
        for ( int i = 0; i < 5; ++i ) { lo[i] = m.V( xs[i], ys[i], 0 ); }
        for ( int i = 0; i < 5; ++i ) { hi[i] = m.V( xs[i], ys[i], 1 ); }
        for ( int i = 0; i < 4; ++i ) { m.F( { lo[i], lo[i + 1], hi[i + 1], hi[i] } ); }
        m.Build();
        REQUIRE( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 10u );
        const geometry_mesh_edge_handle_t pair[] = { m.Edge( 105, 100 ), m.Edge( 104, 109 ) };
        const mesh_merge_result_t r =
            MeshMerge_SewEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ pair, 2 }, mesh_merge_target_t::FIRST, nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 8u );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 8u );
        m.Valid();
    }
    SECTION( "A triangle fan with a gap closes around its centre" ) {
        // Centre 100; rim 101..105 at 0, 90, 180, 270 and 360 degrees.
        Src m;
        const common::u32 c = m.V( 0, 0, 0 );
        common::u32 rim[5];
        for ( int i = 0; i < 5; ++i ) {
            const double a = 1.5707963267948966 * i;
            rim[i] = m.V( std::round( std::cos( a ) ), std::round( std::sin( a ) ), 0 );
        }
        for ( int i = 0; i < 4; ++i ) { m.F( { c, rim[i], rim[i + 1] } ); }
        m.Build();
        const geometry_mesh_edge_handle_t pair[] = { m.Edge( 100, 101 ), m.Edge( 105, 100 ) };
        const mesh_merge_result_t r =
            MeshMerge_SewEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ pair, 2 }, mesh_merge_target_t::FIRST, nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cVerticesMerged == 1u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 5u );
        CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 8u );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 4u );
        m.Valid();
    }
}

TEST_CASE( "Merging a box's top corners to their centre makes a pyramid", "[geometry][meshmerge]" ) {
    Src m;
    m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    m.Build();
    const geometry_mesh_vertex_handle_t top[] = { m.Vertex( 104 ), m.Vertex( 105 ), m.Vertex( 106 ), m.Vertex( 107 ) };
    const common::u32 size = 4u;
    const mesh_merge_result_t r = MeshMerge_Vertices( &m.s.mesh, common::span_t<const geometry_mesh_vertex_handle_t>{ top, 4 },
                                                      common::span_t<const common::u32>{ &size, 1 }, mesh_merge_target_t::CENTER, nullptr );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cVerticesMerged == 3u );
    CHECK( r.cFacesCollapsed == 1u );
    CHECK( r.cFacesRebuilt == 4u );
    CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 5u );
    CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 5u );
    CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 8u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
    CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 1.0 / 3.0 ) );
    const vec3d_t apex = common::GenerationPool_Get( &m.s.mesh.vertices, m.Vertex( 104 ) )->position;
    CHECK( apex.x == 0.5 );
    CHECK( apex.y == 0.5 );
    CHECK( apex.z == 1.0 );
    m.Valid();
}

TEST_CASE( "Merges that would fold the surface are rejected", "[geometry][meshmerge]" ) {
    SECTION( "Two boxes touching at one merged corner (bow-tie vertex)" ) {
        // The corners are apart, so the survivor really moves to the centre
        // before the rebuild rejects the merge - and must move back.
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Box( Vec3d_Make( 1.1, 1.1, 1.1 ), Vec3d_Make( 2, 2, 2 ) );
        m.Build();
        Desc before, after;
        m.Snapshot( &before.d );
        const geometry_mesh_vertex_handle_t corner[] = { m.Vertex( 107 ), m.Vertex( 108 ) }; // (1, 1, 1) and (1.1, 1.1, 1.1)
        const common::u32 size = 2u;
        CHECK( MeshMerge_Vertices( &m.s.mesh, common::span_t<const geometry_mesh_vertex_handle_t>{ corner, 2 },
                                   common::span_t<const common::u32>{ &size, 1 }, mesh_merge_target_t::CENTER, nullptr )
                   .status == geometry_status_t::NON_MANIFOLD );
        m.Snapshot( &after.d );
        CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
    }
    SECTION( "Opposite corners of one face (pinch)" ) {
        Src m;
        const common::u32 a = m.V( 0, 0, 0 ), b = m.V( 1, 0, 0 ), c = m.V( 1, 1, 0 ), d = m.V( 0, 1, 0 );
        m.F( { a, b, c, d } );
        m.Build();
        const geometry_mesh_vertex_handle_t diagonal[] = { m.Vertex( 100 ), m.Vertex( 102 ) };
        const common::u32 size = 2u;
        CHECK( MeshMerge_Vertices( &m.s.mesh, common::span_t<const geometry_mesh_vertex_handle_t>{ diagonal, 2 },
                                   common::span_t<const common::u32>{ &size, 1 }, mesh_merge_target_t::CENTER, nullptr )
                   .status == geometry_status_t::NON_MANIFOLD );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 4u );
        CHECK( common::GenerationPool_Get( &m.s.mesh.vertices, m.Vertex( 100 ) )->position.x == 0.0 ); // moved back
    }
}

TEST_CASE( "Merge by distance re-welds a detached shell and collapses tiny edges", "[geometry][meshmerge]" ) {
    SECTION( "Detach then weld restores the closed box" ) {
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Build();
        geometry_mesh_face_handle_t top{};
        REQUIRE( MeshSource_TryFindFace( &m.s, Id( 1001 ), &top ) );
        REQUIRE( MeshBoundary_DetachFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ &top, 1 } ).status ==
                 geometry_status_t::OK );
        REQUIRE( EditableMesh_VertexCount( &m.s.mesh ) == 12u );
        const mesh_merge_result_t r = MeshMerge_ByDistance( &m.s.mesh, 0.0, nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cVerticesMerged == 4u );
        CHECK( r.cEdgesJoined == 4u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 8u );
        CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 12u );
        CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
        CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 1.0 ) );
        m.Valid();
        // Nothing left within tolerance.
        const mesh_merge_result_t again = MeshMerge_ByDistance( &m.s.mesh, 1e-6, nullptr );
        CHECK( again.status == geometry_status_t::OK );
        CHECK( again.cVerticesMerged == 0u );
    }
    SECTION( "A near-duplicate corner collapses its edge" ) {
        Src m;
        const common::u32 a = m.V( 0, 0, 0 ), b = m.V( 1, 0, 0 ), b2 = m.V( 1, 1e-7, 0 ), c = m.V( 1, 1, 0 ), d = m.V( 0, 1, 0 );
        m.F( { a, b, b2, c, d } );
        m.Build();
        const mesh_merge_result_t r = MeshMerge_ByDistance( &m.s.mesh, 1e-6, nullptr );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cVerticesMerged == 1u );
        CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 4u );
        CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 4u );
        // The lowest handle survives in place.
        CHECK( common::GenerationPool_Get( &m.s.mesh.vertices, m.Vertex( 101 ) )->position.y == 0.0 );
        m.Valid();
    }
}

TEST_CASE( "Merge rejects bad input and leaves the mesh unchanged", "[geometry][meshmerge]" ) {
    Src m;
    m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    m.Build();
    Desc before, after;
    m.Snapshot( &before.d );
    const geometry_mesh_vertex_handle_t one[] = { m.Vertex( 100 ) };
    const common::u32 sizeOne = 1u, sizeTwo = 2u;
    CHECK( MeshMerge_Vertices( &m.s.mesh, common::span_t<const geometry_mesh_vertex_handle_t>{ one, 1 },
                               common::span_t<const common::u32>{ &sizeOne, 1 }, mesh_merge_target_t::CENTER, nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
    const geometry_mesh_vertex_handle_t twice[] = { m.Vertex( 100 ), m.Vertex( 100 ) };
    CHECK( MeshMerge_Vertices( &m.s.mesh, common::span_t<const geometry_mesh_vertex_handle_t>{ twice, 2 },
                               common::span_t<const common::u32>{ &sizeTwo, 1 }, mesh_merge_target_t::CENTER, nullptr )
               .status == geometry_status_t::INVALID_HANDLE );
    const geometry_mesh_vertex_handle_t stale[] = { m.Vertex( 100 ), geometry_mesh_vertex_handle_t{ 999u, 1u } };
    CHECK( MeshMerge_Vertices( &m.s.mesh, common::span_t<const geometry_mesh_vertex_handle_t>{ stale, 2 },
                               common::span_t<const common::u32>{ &sizeTwo, 1 }, mesh_merge_target_t::CENTER, nullptr )
               .status == geometry_status_t::INVALID_HANDLE );
    // Sewing needs pairs of open edges.
    const geometry_mesh_edge_handle_t closed[] = { m.Edge( 100, 101 ), m.Edge( 102, 103 ) };
    CHECK( MeshMerge_SewEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ closed, 2 }, mesh_merge_target_t::CENTER,
                               nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshMerge_SewEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ closed, 1 }, mesh_merge_target_t::CENTER,
                               nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshMerge_ByDistance( &m.s.mesh, -1.0, nullptr ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshMerge_ByDistance( &m.s.mesh, std::nan( "" ), nullptr ).status == geometry_status_t::NUMERIC_FAILURE );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

TEST_CASE( "Source sew keeps face IDs, survivor IDs, and UVs", "[geometry][meshmerge][meshedit]" ) {
    Src m;
    TwoQuads( m );
    for ( common::usize i = 0; i < m.d.corners.nCount; ++i ) {
        const vec3d_t p = m.d.vertices.pData[m.d.corners.pData[i].iVertex].position;
        m.d.corners.pData[i].attributes.uv0 = math::vec2d_t{ p.x, p.y };
    }
    MeshSource_Shutdown( &m.s );
    m.Build();
    const geometry_source_id_t ids[] = { Id( 101 ), Id( 102 ), Id( 107 ), Id( 104 ) };
    mesh_edit_report_t report{};
    common::u32 cMerged = 0u;
    REQUIRE( MeshSourceEdit_TrySewEdges( &m.s, common::span_t<const geometry_source_id_t>{ ids, 4 }, mesh_merge_target_t::CENTER, &cMerged,
                                         &report ) == geometry_status_t::OK );
    CHECK( cMerged == 2u );
    CHECK( report.stats.cFacesInheritingIdentity == 2u );
    CHECK( report.stats.cCornersDefaulted == 0u );
    for ( const common::u64 face : { 1000u, 1001u } ) {
        geometry_mesh_face_handle_t h{};
        CHECK( MeshSource_TryFindFace( &m.s, Id( face ), &h ) );
    }
    m.Valid();
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &m.allocator, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &m.s, &d ) == geometry_status_t::OK );
    CHECK( d.vertices.nCount == 6u );
    for ( common::usize i = 0; i < d.corners.nCount; ++i ) {
        const vec3d_t p = d.vertices.pData[d.corners.pData[i].iVertex].position;
        CHECK( d.corners.pData[i].attributes.uv0.x == Approx( p.x ).margin( 1e-12 ) );
        CHECK( d.corners.pData[i].attributes.uv0.y == Approx( p.y ).margin( 1e-12 ) );
    }
    MeshSourceDescription_Shutdown( &d );
    // Unknown IDs and non-edges.
    const geometry_source_id_t bad[] = { Id( 100 ), Id( 102 ), Id( 105 ), Id( 106 ) };
    CHECK( MeshSourceEdit_TrySewEdges( &m.s, common::span_t<const geometry_source_id_t>{ bad, 4 }, mesh_merge_target_t::CENTER, nullptr,
                                       nullptr ) == geometry_status_t::INVALID_HANDLE );
}

TEST_CASE( "Every merge allocation failure is atomic", "[geometry][meshmerge][allocation]" ) {
    auto build = []( Src &m ) {
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Build();
    };
    auto run = []( Src &m ) {
        const geometry_mesh_vertex_handle_t top[] = { m.Vertex( 104 ), m.Vertex( 105 ), m.Vertex( 106 ), m.Vertex( 107 ) };
        const common::u32 size = 4u;
        return MeshMerge_Vertices( &m.s.mesh, common::span_t<const geometry_mesh_vertex_handle_t>{ top, 4 },
                                   common::span_t<const common::u32>{ &size, 1 }, mesh_merge_target_t::CENTER, nullptr );
    };
    common::usize cOperationAllocations = 0u;
    merge_failure_allocator_state_t probeState{};
    common::allocator_t probeAllocator{ MergeFailureAllocate, nullptr, MergeFailureFree, &probeState };
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
        merge_failure_allocator_state_t state{};
        common::allocator_t allocator{ MergeFailureAllocate, nullptr, MergeFailureFree, &state };
        {
            Src m( &allocator );
            build( m );
            Desc before, after;
            m.Snapshot( &before.d );
            state.iFailOnCall = state.cAllocationCalls + iFailure;
            const mesh_merge_result_t result = run( m );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( result.status == geometry_status_t::ALLOCATION_FAILED );
            m.Snapshot( &after.d );
            CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

} // namespace cypher::editor::geometry
