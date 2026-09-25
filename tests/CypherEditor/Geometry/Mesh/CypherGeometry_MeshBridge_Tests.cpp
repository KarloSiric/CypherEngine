//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBridge_Tests.cpp
//  Purpose: Tests bridging two faces (connector, handle) and two open edge
//           chains (strip across a gap) on editable meshes and mesh
//           sources, rejections, and allocation-failure atomicity.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshBridge.h"
#include "CypherGeometry_MeshSourceBridge.h"
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
    // Vertex IDs 100, 101, ...; face IDs 1000, 1001, ... in creation order.
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
    void Box( vec3d_t lo, vec3d_t hi ) {
        common::u32 v[8];
        for ( int i = 0; i < 8; ++i ) {
            v[i] = V( ( i & 1 ) ? hi.x : lo.x, ( i & 2 ) ? hi.y : lo.y, ( i & 4 ) ? hi.z : lo.z );
        }
        const common::u32 faces[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for ( const auto &f : faces ) { F( { v[f[0]], v[f[1]], v[f[2]], v[f[3]] } ); }
    }
    // A prism over a CCW outline in the xy plane, z in [0, 1]: faces bottom,
    // top, then one side per outline edge.
    void Prism( const std::vector<std::pair<double, double>> &outline ) {
        const common::u32 n = static_cast<common::u32>( outline.size() );
        std::vector<common::u32> bottom, top;
        for ( const auto &p : outline ) { bottom.push_back( V( p.first, p.second, 0.0 ) ); }
        for ( const auto &p : outline ) { top.push_back( V( p.first, p.second, 1.0 ) ); }
        F( std::vector<common::u32>( bottom.rbegin(), bottom.rend() ) );
        F( top );
        for ( common::u32 i = 0; i < n; ++i ) { F( { bottom[i], bottom[( i + 1 ) % n], top[( i + 1 ) % n], top[i] } ); }
    }
    // A grid sheet in the plane z = 0 over [x0, x0 + nx] x [y0, y0 + ny].
    void Sheet( double x0, double y0, int nx, int ny ) {
        std::vector<common::u32> v;
        for ( int j = 0; j <= ny; ++j ) {
            for ( int i = 0; i <= nx; ++i ) { v.push_back( V( x0 + i, y0 + j, 0 ) ); }
        }
        for ( int j = 0; j < ny; ++j ) {
            for ( int i = 0; i < nx; ++i ) {
                const int a = j * ( nx + 1 ) + i;
                F( { v[a], v[a + 1], v[a + nx + 2], v[a + nx + 1] } );
            }
        }
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

double TotalArea( const editable_mesh_t *pMesh ) {
    double total = 0.0;
    (void)common::GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept -> common::bool_t {
            const mesh_loop_record_t *pL = common::GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
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
            total += 0.5 * std::sqrt( n.x * n.x + n.y * n.y + n.z * n.z );
            return true;
        } );
    return total;
}

struct bridge_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *BridgeFailureAllocate( void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<bridge_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) { return nullptr; }
    void *pMemory = common::Allocator_Allocate( common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessfulAllocations; }
    return pMemory;
}

void BridgeFailureFree( void *pUserData, void *pMemory, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<bridge_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) { ++pState->cFrees; }
    common::Allocator_Free( common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

} // namespace

TEST_CASE( "Bridging facing faces of two boxes builds a closed connector", "[geometry][meshbridge]" ) {
    for ( const common::u32 cSegments : { 1u, 3u } ) {
        CAPTURE( cSegments );
        Src m;
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Box( Vec3d_Make( 2, 0, 0 ), Vec3d_Make( 3, 1, 1 ) );
        m.Build();
        common::vector_t<mesh_bridge_face_t> faces{};
        REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
        // Box 1's +x face (1005) faces box 2's -x face (1010).
        const mesh_bridge_result_t r = MeshBridge_Faces( &m.s.mesh, m.Face( 1005 ), m.Face( 1010 ), cSegments, &faces );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.cFacesRemoved == 2u );
        CHECK( r.cFacesCreated == 4u * cSegments );
        CHECK( r.cVerticesCreated == 4u * ( cSegments - 1u ) );
        CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
        CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
        CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 3.0 ) );
        CHECK( EditableMesh_EulerCharacteristic( &m.s.mesh ) == 2 );
        // Untwisted: every tube quad is a unit-by-(1 / segments) rectangle.
        CHECK( TotalArea( &m.s.mesh ) == Approx( 10.0 + 4.0 ) );
        for ( common::usize i = 0; i < faces.nCount; ++i ) { CHECK( GeometryHandle_IsValid( faces.pData[i].hSource ) ); }
        m.Valid();
        common::Vector_Shutdown( &faces );
    }
}

TEST_CASE( "Bridging a U's arms closes it into a ring", "[geometry][meshbridge]" ) {
    // The inner walls are split at y = 2 and only the upper halves are
    // bridged. (Bridging the full inner walls would lay the tube's side
    // exactly on the notch's floor wall - a third face on its edges.)
    Src m;
    m.Prism( { { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 2 }, { 2, 1 }, { 1, 1 }, { 1, 2 }, { 1, 3 }, { 0, 3 } } );
    m.Build();
    // Sides are 1002 + i for outline edge i: edge 3 (2,3)->(2,2) faces -x,
    // edge 7 (1,2)->(1,3) faces +x.
    const mesh_bridge_result_t r = MeshBridge_Faces( &m.s.mesh, m.Face( 1005 ), m.Face( 1009 ), 1u, nullptr );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( EditableMesh_EulerCharacteristic( &m.s.mesh ) == 0 ); // genus 1
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
    CHECK( EditableMesh_SignedVolume( &m.s.mesh ) == Approx( 8.0 ) );
    m.Valid();
    // The full inner walls would coincide with the notch floor.
    Src full;
    full.Prism( { { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } } );
    full.Build();
    CHECK( MeshBridge_Faces( &full.s.mesh, full.Face( 1005 ), full.Face( 1007 ), 1u, nullptr ).status == geometry_status_t::NON_MANIFOLD );
}

TEST_CASE( "Bridging two open edge chains fills the gap between sheets", "[geometry][meshbridge]" ) {
    // Sheet A: y in [0, 1], vertices 100..105 (top row 103, 104, 105).
    // Sheet B: y in [2, 3], vertices 106..111 (bottom row 106, 107, 108).
    for ( const bool bReverseInput : { false, true } ) {
        for ( const common::u32 cSegments : { 1u, 2u } ) {
            CAPTURE( bReverseInput, cSegments );
            Src m;
            m.Sheet( 0, 0, 2, 1 );
            m.Sheet( 0, 2, 2, 1 );
            m.Build();
            std::vector<geometry_mesh_edge_handle_t> a = { m.Edge( 105, 104 ), m.Edge( 104, 103 ) };
            std::vector<geometry_mesh_edge_handle_t> b = { m.Edge( 106, 107 ), m.Edge( 107, 108 ) };
            if ( bReverseInput ) {
                std::swap( a[0], a[1] );
                std::swap( b[0], b[1] );
            }
            const mesh_bridge_result_t r = MeshBridge_EdgeChains( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ a.data(), 2 },
                                                                  common::span_t<const geometry_mesh_edge_handle_t>{ b.data(), 2 }, cSegments,
                                                                  nullptr );
            REQUIRE( r.status == geometry_status_t::OK );
            CHECK( r.cFacesCreated == 2u * cSegments );
            CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
            CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 10u + 2u * ( cSegments - 1u ) );
            CHECK( TotalArea( &m.s.mesh ) == Approx( 6.0 ) );
            (void)common::GenerationPool_ForEach( &m.s.mesh.faces,
                [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept -> common::bool_t {
                    CHECK( f.normal.z == Approx( 1.0 ) );
                    return true;
                } );
            m.Valid();
        }
    }
}

TEST_CASE( "Bridge rejects bad input and leaves the mesh unchanged", "[geometry][meshbridge]" ) {
    Src m;
    m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    // A triangle apart from the box.
    const common::u32 a = m.V( 3, 0, 0 ), b = m.V( 4, 0, 0 ), c = m.V( 3, 1, 0 );
    m.F( { a, b, c } );
    m.Build();
    Desc before, after;
    m.Snapshot( &before.d );
    CHECK( MeshBridge_Faces( &m.s.mesh, m.Face( 1000 ), m.Face( 1006 ), 1u, nullptr ).status == geometry_status_t::INVALID_ARGUMENT ); // 4 vs 3
    CHECK( MeshBridge_Faces( &m.s.mesh, m.Face( 1000 ), m.Face( 1002 ), 1u, nullptr ).status == geometry_status_t::INVALID_ARGUMENT ); // adjacent
    CHECK( MeshBridge_Faces( &m.s.mesh, m.Face( 1000 ), m.Face( 1000 ), 1u, nullptr ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshBridge_Faces( &m.s.mesh, m.Face( 1000 ), m.Face( 1001 ), 0u, nullptr ).status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshBridge_Faces( &m.s.mesh, m.Face( 1000 ), geometry_mesh_face_handle_t{ 999u, 1u }, 1u, nullptr ).status ==
           geometry_status_t::INVALID_HANDLE );
    // Chains: interior edges, unequal lengths, a broken chain, a loop.
    const geometry_mesh_edge_handle_t interior[] = { m.Edge( 100, 101 ) };
    const geometry_mesh_edge_handle_t open[] = { m.Edge( 108, 109 ) };
    CHECK( MeshBridge_EdgeChains( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ interior, 1 },
                                  common::span_t<const geometry_mesh_edge_handle_t>{ open, 1 }, 1u, nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
    const geometry_mesh_edge_handle_t two[] = { m.Edge( 108, 109 ), m.Edge( 109, 110 ) };
    CHECK( MeshBridge_EdgeChains( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ two, 2 },
                                  common::span_t<const geometry_mesh_edge_handle_t>{ open, 1 }, 1u, nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
    const geometry_mesh_edge_handle_t loop[] = { m.Edge( 108, 109 ), m.Edge( 109, 110 ), m.Edge( 110, 108 ) };
    CHECK( MeshBridge_EdgeChains( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ loop, 3 },
                                  common::span_t<const geometry_mesh_edge_handle_t>{ loop, 3 }, 1u, nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );

    Src sheet;
    sheet.Sheet( 0, 0, 3, 1 );
    sheet.Build();
    const geometry_mesh_edge_handle_t broken[] = { sheet.Edge( 100, 101 ), sheet.Edge( 102, 103 ) };
    const geometry_mesh_edge_handle_t other[] = { sheet.Edge( 105, 104 ), sheet.Edge( 106, 105 ) };
    CHECK( MeshBridge_EdgeChains( &sheet.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ broken, 2 },
                                  common::span_t<const geometry_mesh_edge_handle_t>{ other, 2 }, 1u, nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Source bridge carries UVs across the gap", "[geometry][meshbridge][meshedit]" ) {
    Src m;
    m.Sheet( 0, 0, 2, 1 );
    m.Sheet( 0, 2, 2, 1 );
    for ( common::usize i = 0; i < m.d.corners.nCount; ++i ) {
        const vec3d_t p = m.d.vertices.pData[m.d.corners.pData[i].iVertex].position;
        m.d.corners.pData[i].attributes.uv0 = math::vec2d_t{ p.x, p.y };
    }
    m.Build();
    const geometry_source_id_t pathA[] = { Id( 105 ), Id( 104 ), Id( 103 ) };
    const geometry_source_id_t pathB[] = { Id( 106 ), Id( 107 ), Id( 108 ) };
    mesh_edit_report_t report{};
    common::u32 cCreated = 0u;
    REQUIRE( MeshSourceEdit_TryBridgeEdgeChains( &m.s, common::span_t<const geometry_source_id_t>{ pathA, 3 },
                                                 common::span_t<const geometry_source_id_t>{ pathB, 3 }, 2u, &cCreated,
                                                 &report ) == geometry_status_t::OK );
    CHECK( cCreated == 4u );
    CHECK( report.stats.cFacesFromParent == 4u );
    CHECK( report.stats.cCornersDefaulted == 0u );
    m.Valid();
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &m.allocator, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &m.s, &d ) == geometry_status_t::OK );
    CHECK( d.faces.nCount == 8u );
    for ( common::usize i = 0; i < d.corners.nCount; ++i ) {
        const vec3d_t p = d.vertices.pData[d.corners.pData[i].iVertex].position;
        CHECK( d.corners.pData[i].attributes.uv0.x == Approx( p.x ).margin( 1e-12 ) );
        CHECK( d.corners.pData[i].attributes.uv0.y == Approx( p.y ).margin( 1e-12 ) );
    }
    MeshSourceDescription_Shutdown( &d );

    // Faces by ID; the bridged faces' IDs are gone afterwards.
    Src boxes;
    boxes.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
    boxes.Box( Vec3d_Make( 2, 0, 0 ), Vec3d_Make( 3, 1, 1 ) );
    boxes.Build();
    REQUIRE( MeshSourceEdit_TryBridgeFaces( &boxes.s, Id( 1005 ), Id( 1010 ), 1u, &cCreated, nullptr ) == geometry_status_t::OK );
    CHECK( cCreated == 4u );
    geometry_mesh_face_handle_t gone{};
    CHECK_FALSE( MeshSource_TryFindFace( &boxes.s, Id( 1005 ), &gone ) );
    CHECK( MeshSourceEdit_TryBridgeFaces( &boxes.s, Id( 1005 ), Id( 1001 ), 1u, nullptr, nullptr ) == geometry_status_t::INVALID_HANDLE );
    boxes.Valid();
}

TEST_CASE( "Every face-bridge allocation failure is atomic", "[geometry][meshbridge][allocation]" ) {
    auto build = []( Src &m ) {
        m.Box( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ) );
        m.Box( Vec3d_Make( 2, 0, 0 ), Vec3d_Make( 3, 1, 1 ) );
        m.Build();
    };
    auto run = []( Src &m ) { return MeshBridge_Faces( &m.s.mesh, m.Face( 1005 ), m.Face( 1010 ), 2u, nullptr ); };
    common::usize cOperationAllocations = 0u;
    bridge_failure_allocator_state_t probeState{};
    common::allocator_t probeAllocator{ BridgeFailureAllocate, nullptr, BridgeFailureFree, &probeState };
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
        bridge_failure_allocator_state_t state{};
        common::allocator_t allocator{ BridgeFailureAllocate, nullptr, BridgeFailureFree, &state };
        {
            Src m( &allocator );
            build( m );
            Desc before, after;
            m.Snapshot( &before.d );
            state.iFailOnCall = state.cAllocationCalls + iFailure;
            const mesh_bridge_result_t result = run( m );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( result.status == geometry_status_t::ALLOCATION_FAILED );
            m.Snapshot( &after.d );
            CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

} // namespace cypher::editor::geometry
