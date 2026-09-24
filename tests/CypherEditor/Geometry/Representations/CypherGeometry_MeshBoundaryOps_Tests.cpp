//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBoundaryOps_Tests.cpp
//  Purpose: Contract tests for open-boundary mesh operations: delete faces,
//           fill hole, extrude boundary edges, and detach faces, at the raw
//           mesh level and through the identity-addressed wrappers.
//  Details: Oracles are volumes and counts: deleting a cube's top leaves
//           an open box with a 4-edge boundary; filling it restores a closed
//           box of volume 1; extruding the open rim up by 1 and filling
//           gives a closed box of volume exactly 2 (Euler characteristic 2).
//           An in-plane edge pull on a UV-mapped sheet must continue the
//           planar UV layout exactly. Every rejected call must leave the
//           canonical description bit-identical.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshValidation.h"

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
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    explicit Src( const common::allocator_t *pAllocator = common::Allocator_GetSystem() ) : allocator( *pAllocator ) {
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    }
    ~Src() {
        MeshSource_Shutdown( &s );
        MeshSourceDescription_Shutdown( &d );
    }
    common::u32 V( double x, double y, double z, common::u64 id ) {
        common::u32 i = 0;
        REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( x, y, z ), Id( id ), &i ) == geometry_status_t::OK );
        return i;
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
        F( { 4, 5, 7, 6 }, 21 ); // top
        F( { 0, 1, 5, 4 }, 22 );
        F( { 2, 6, 7, 3 }, 23 );
        F( { 0, 4, 6, 2 }, 24 );
        F( { 1, 3, 7, 5 }, 25 );
        Build();
    }
    void Quad() {
        V( 0, 0, 0, 10 );
        V( 1, 0, 0, 11 );
        V( 1, 1, 0, 12 );
        V( 0, 1, 0, 13 );
        F( { 0, 1, 2, 3 }, 20 );
        Build();
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
    // Canonical snapshot for "nothing changed" checks.
    void Snapshot( mesh_source_description_t *pOut ) {
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        REQUIRE( MeshSource_TryDescribe( &s, pOut ) == geometry_status_t::OK );
    }
    void Valid() {
        REQUIRE( MeshSource_TryAssignMissingIds( &s, &ids, nullptr ) == geometry_status_t::OK );
        const mesh_source_validation_t v = MeshSource_Validate( &s, &allocator );
        CHECK( v.fault == mesh_source_fault_t::NONE );
        // Round trip through the canonical description (open meshes too).
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

struct boundary_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *BoundaryFailureAllocate( void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept {
    auto *pState = static_cast<boundary_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) { return nullptr; }
    void *pMemory = common::Allocator_Allocate( common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessfulAllocations; }
    return pMemory;
}

void BoundaryFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<boundary_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) { ++pState->cFrees; }
    common::Allocator_Free( common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeBoundaryFailureAllocator( boundary_failure_allocator_state_t *pState ) noexcept {
    return common::allocator_t{ BoundaryFailureAllocate, nullptr, BoundaryFailureFree, pState };
}

common::usize OutstandingAllocations( const boundary_failure_allocator_state_t &state ) noexcept {
    return state.cSuccessfulAllocations - state.cFrees;
}

} // namespace

TEST_CASE( "Deleting a face opens the mesh; filling closes it again", "[geometry][meshboundary]" ) {
    Src m;
    m.Cube();
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
    const geometry_mesh_face_handle_t top[] = { m.Face( 21 ) };
    const mesh_boundary_delete_result_t del =
        MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ top, 1 } );
    REQUIRE( del.status == geometry_status_t::OK );
    CHECK( del.cFacesRemoved == 1u );
    CHECK( del.cEdgesRemoved == 0u );
    CHECK( del.cVerticesRemoved == 0u );
    CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 5u );
    CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 12u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 4u );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
    CHECK( MeshBoundary_IsBoundaryEdge( &m.s.mesh, m.Edge( 14, 15 ) ) );
    CHECK_FALSE( MeshBoundary_IsBoundaryEdge( &m.s.mesh, m.Edge( 10, 11 ) ) );
    m.Valid();

    const mesh_boundary_fill_result_t fill = MeshBoundary_FillHole( &m.s.mesh, m.Edge( 16, 17 ) );
    REQUIRE( fill.status == geometry_status_t::OK );
    CHECK( fill.cSides == 4u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 0u );
    const mesh_validation_result_t v = MeshValidation_Validate( &m.s.mesh );
    CHECK( v.status == geometry_status_t::OK );
    CHECK( v.fSignedVolume == Approx( 1.0 ) );
    CHECK( EditableMesh_GetFace( &m.s.mesh, fill.hFace )->normal.z == Approx( 1.0 ) );
    m.Valid();
}

TEST_CASE( "Extruding an open rim and capping it builds a taller closed box", "[geometry][meshboundary]" ) {
    Src m;
    m.Cube();
    const geometry_mesh_face_handle_t top[] = { m.Face( 21 ) };
    REQUIRE( MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ top, 1 } ).status ==
             geometry_status_t::OK );
    const geometry_mesh_edge_handle_t rim[] = { m.Edge( 14, 15 ), m.Edge( 15, 17 ), m.Edge( 17, 16 ), m.Edge( 16, 14 ) };
    common::vector_t<geometry_mesh_edge_handle_t> outer{};
    REQUIRE( common::Vector_Init( &outer, &m.allocator ) );
    const mesh_boundary_extrude_result_t ex = MeshBoundary_ExtrudeEdges(
        &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ rim, 4 }, Vec3d_Make( 0, 0, 1 ), &outer );
    REQUIRE( ex.status == geometry_status_t::OK );
    CHECK( ex.cFacesCreated == 4u );
    CHECK( ex.cVerticesCreated == 4u ); // a closed loop shares every new vertex
    REQUIRE( outer.nCount == 4u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 4u );
    for ( common::usize i = 0; i < outer.nCount; ++i ) { CHECK( MeshBoundary_IsBoundaryEdge( &m.s.mesh, outer.pData[i] ) ); }
    // Walls face outward: every new quad's normal is horizontal and points
    // away from the box centre.
    common::usize cWalls = 0;
    (void)common::GenerationPool_ForEach( &m.s.mesh.faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> common::bool_t {
            if ( GeometrySourceId_IsValid( MeshSource_FaceId( &m.s, hF ) ) ) { return true; }
            ++cWalls;
            CHECK( f.normal.z == Approx( 0.0 ).margin( 1e-12 ) );
            return true;
        } );
    CHECK( cWalls == 4u );

    REQUIRE( MeshBoundary_FillHole( &m.s.mesh, outer.pData[0] ).status == geometry_status_t::OK );
    const mesh_validation_result_t v = MeshValidation_Validate( &m.s.mesh );
    CHECK( v.status == geometry_status_t::OK );
    CHECK( v.nEulerCharacteristic == 2 );
    CHECK( v.fSignedVolume == Approx( 2.0 ) );
    common::Vector_Shutdown( &outer );
    m.Valid();
}

TEST_CASE( "In-plane edge pull extends a sheet and its UV layout", "[geometry][meshboundary]" ) {
    // Unit quad at z = 0 with UV = (x, y).
    Src m;
    m.V( 0, 0, 0, 10 );
    m.V( 1, 0, 0, 11 );
    m.V( 1, 1, 0, 12 );
    m.V( 0, 1, 0, 13 );
    m.F( { 0, 1, 2, 3 }, 20 );
    for ( common::usize i = 0; i < m.d.corners.nCount; ++i ) {
        const vec3d_t p = m.d.vertices.pData[m.d.corners.pData[i].iVertex].position;
        m.d.corners.pData[i].attributes.uv0 = math::vec2d_t{ p.x, p.y };
    }
    m.d.faces.pData[0].attributes.material.value = 7u;
    m.Build();

    common::vector_t<geometry_mesh_edge_handle_t> outer{};
    REQUIRE( common::Vector_Init( &outer, &m.allocator ) );
    const geometry_source_id_t edge[] = { Id( 11 ), Id( 12 ) };
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TryExtrudeEdges( &m.s, common::span_t<const geometry_source_id_t>{ edge, 2 },
                                             Vec3d_Make( 1, 0, 0 ), &outer, &report ) == geometry_status_t::OK );
    CHECK( report.stats.cFacesFromParent == 1u );
    CHECK( report.stats.cCornersDefaulted == 0u );
    REQUIRE( outer.nCount == 1u );
    // Second pull from the new outer edge, addressed by its fresh IDs.
    REQUIRE( MeshSource_TryAssignMissingIds( &m.s, &m.ids, nullptr ) == geometry_status_t::OK );
    const mesh_edge_record_t *pE = GenerationPool_Get( &m.s.mesh.edges, outer.pData[0] );
    const mesh_half_edge_record_t *pH = GenerationPool_Get( &m.s.mesh.halfEdges, pE->hHalfEdge );
    const mesh_half_edge_record_t *pN = GenerationPool_Get( &m.s.mesh.halfEdges, pH->hNext );
    const geometry_source_id_t next[] = { MeshSource_VertexId( &m.s, pH->hOrigin ), MeshSource_VertexId( &m.s, pN->hOrigin ) };
    common::Vector_Clear( &outer );
    REQUIRE( MeshSourceEdit_TryExtrudeEdges( &m.s, common::span_t<const geometry_source_id_t>{ next, 2 },
                                             Vec3d_Make( 1, 0, 0 ), &outer, nullptr ) == geometry_status_t::OK );
    common::Vector_Shutdown( &outer );
    m.Valid();

    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &m.allocator, Id( 1 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &m.s, &d ) == geometry_status_t::OK );
    CHECK( d.faces.nCount == 3u );
    CHECK( d.vertices.nCount == 8u );
    for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
        CHECK( d.faces.pData[f].attributes.material.value == 7u );
        for ( common::u32 k = 0; k < d.faces.pData[f].cCorners; ++k ) {
            const mesh_source_corner_t &c = d.corners.pData[d.faces.pData[f].iFirstCorner + k];
            const vec3d_t p = d.vertices.pData[c.iVertex].position;
            CHECK( c.attributes.uv0.x == Approx( p.x ).margin( 1e-12 ) );
            CHECK( c.attributes.uv0.y == Approx( p.y ).margin( 1e-12 ) );
        }
    }
    // All three faces keep the sheet's orientation.
    (void)common::GenerationPool_ForEach( &m.s.mesh.faces,
        [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept -> common::bool_t {
            CHECK( f.normal.z == Approx( 1.0 ) );
            return true;
        } );
    MeshSourceDescription_Shutdown( &d );
}

TEST_CASE( "Detach splits a shell and cuts the shared edges", "[geometry][meshboundary]" ) {
    Src m;
    m.Cube();
    const geometry_mesh_face_handle_t top[] = { m.Face( 21 ) };
    const mesh_boundary_detach_result_t r =
        MeshBoundary_DetachFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ top, 1 } );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cVerticesDuplicated == 4u );
    CHECK( r.cEdgesCut == 4u );
    CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 12u );
    CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 16u );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 2u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 8u );
    // The top face keeps its identity; its corners now use the copies.
    CHECK( GeometrySourceId_IsValid( MeshSource_FaceId( &m.s, m.Face( 21 ) ) ) );
    m.Valid();
    // Detaching everything is a no-op.
    std::vector<geometry_mesh_face_handle_t> all;
    (void)common::GenerationPool_ForEach( &m.s.mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> common::bool_t {
            all.push_back( h );
            return true;
        } );
    const mesh_boundary_detach_result_t none =
        MeshBoundary_DetachFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ all.data(), all.size() } );
    CHECK( none.status == geometry_status_t::OK );
    CHECK( none.cVerticesDuplicated == 0u );
}

TEST_CASE( "Deleting the middle of a strip splits it into two shells", "[geometry][meshboundary]" ) {
    Src m;
    for ( int i = 0; i < 4; ++i ) {
        m.V( i, 0, 0, 10u + static_cast<common::u64>( i ) );
        m.V( i, 1, 0, 20u + static_cast<common::u64>( i ) );
    }
    // Vertex index = 2 * column + row.
    m.F( { 0, 2, 3, 1 }, 30 );
    m.F( { 2, 4, 5, 3 }, 31 );
    m.F( { 4, 6, 7, 5 }, 32 );
    m.Build();
    const geometry_mesh_face_handle_t mid[] = { m.Face( 31 ) };
    const mesh_boundary_delete_result_t r =
        MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ mid, 1 } );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cEdgesRemoved == 2u ); // the middle quad's top and bottom
    CHECK( r.cVerticesRemoved == 0u );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 2u );
    CHECK( EditableMesh_EdgeCount( &m.s.mesh ) == 8u );
    m.Valid();
    // Delete one end quad as well: its two outer vertices go with it.
    const geometry_mesh_face_handle_t end[] = { m.Face( 32 ) };
    const mesh_boundary_delete_result_t r2 =
        MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ end, 1 } );
    REQUIRE( r2.status == geometry_status_t::OK );
    CHECK( r2.cVerticesRemoved == 4u );
    CHECK( r2.cEdgesRemoved == 4u );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
    m.Valid();
}

TEST_CASE( "Boundary ops reject bad input and leave the mesh unchanged", "[geometry][meshboundary]" ) {
    Src m;
    m.Cube();
    Desc before, after;
    m.Snapshot( &before.d );
    auto unchanged = [&]() {
        m.Snapshot( &after.d );
        CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
    };

    std::vector<geometry_mesh_face_handle_t> all;
    (void)common::GenerationPool_ForEach( &m.s.mesh.faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> common::bool_t {
            all.push_back( h );
            return true;
        } );
    CHECK( MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ all.data(), all.size() } )
               .status == geometry_status_t::DEGENERATE );
    const geometry_mesh_face_handle_t dup[] = { m.Face( 20 ), m.Face( 20 ) };
    CHECK( MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ dup, 2 } ).status ==
           geometry_status_t::INVALID_HANDLE );
    CHECK( MeshBoundary_FillHole( &m.s.mesh, m.Edge( 10, 11 ) ).status == geometry_status_t::INVALID_ARGUMENT );
    const geometry_mesh_edge_handle_t interior[] = { m.Edge( 10, 11 ) };
    CHECK( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ interior, 1 },
                                      Vec3d_Make( 0, 0, 1 ), nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
    unchanged();

    // Open it, then try degenerate extrusions.
    const geometry_mesh_face_handle_t top[] = { m.Face( 21 ) };
    REQUIRE( MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ top, 1 } ).status ==
             geometry_status_t::OK );
    m.Snapshot( &before.d );
    const geometry_mesh_edge_handle_t rimEdge[] = { m.Edge( 14, 15 ), m.Edge( 14, 15 ) };
    CHECK( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ rimEdge, 1 },
                                      Vec3d_Make( 0, 0, 0 ), nullptr )
               .status == geometry_status_t::DEGENERATE );
    CHECK( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ rimEdge, 2 },
                                      Vec3d_Make( 0, 0, 1 ), nullptr )
               .status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ rimEdge, 1 },
                                      Vec3d_Make( std::nan( "" ), 0, 0 ), nullptr )
               .status == geometry_status_t::NUMERIC_FAILURE );
    CHECK( MeshBoundary_ExtrudeEdges( &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ rimEdge, 1 },
                                      Vec3d_Make( 1, 0, 0 ), nullptr )
               .status == geometry_status_t::DEGENERATE );
    unchanged();
}

TEST_CASE( "Face-list allocation failures are reported exactly and preserve the mesh", "[geometry][meshboundary]" ) {
    SECTION( "delete faces" ) {
        boundary_failure_allocator_state_t state{};
        common::allocator_t allocator = MakeBoundaryFailureAllocator( &state );
        {
            Src m( &allocator );
            m.Cube();
            Desc before, after;
            m.Snapshot( &before.d );
            const geometry_mesh_face_handle_t face[] = { m.Face( 21 ) };
            state.iFailOnCall = state.cAllocationCalls + 1u;
            const mesh_boundary_delete_result_t result =
                MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ face, 1u } );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( result.status == geometry_status_t::ALLOCATION_FAILED );
            m.Snapshot( &after.d );
            CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
            m.Valid();
        }
        CHECK( OutstandingAllocations( state ) == 0u );
    }

    SECTION( "detach faces" ) {
        boundary_failure_allocator_state_t state{};
        common::allocator_t allocator = MakeBoundaryFailureAllocator( &state );
        {
            Src m( &allocator );
            m.Cube();
            Desc before, after;
            m.Snapshot( &before.d );
            const geometry_mesh_face_handle_t face[] = { m.Face( 21 ) };
            state.iFailOnCall = state.cAllocationCalls + 1u;
            const mesh_boundary_detach_result_t result =
                MeshBoundary_DetachFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ face, 1u } );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( result.status == geometry_status_t::ALLOCATION_FAILED );
            m.Snapshot( &after.d );
            CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
            m.Valid();
        }
        CHECK( OutstandingAllocations( state ) == 0u );
    }
}

TEST_CASE( "Every boundary extrusion allocation failure is atomic", "[geometry][meshboundary][allocation]" ) {
    common::usize cOperationAllocations = 0u;
    boundary_failure_allocator_state_t probeState{};
    common::allocator_t probeAllocator = MakeBoundaryFailureAllocator( &probeState );
    {
        Src probe( &probeAllocator );
        probe.Quad();
        const geometry_mesh_edge_handle_t edge[] = { probe.Edge( 11, 12 ) };
        const common::usize cBefore = probeState.cAllocationCalls;
        REQUIRE( MeshBoundary_ExtrudeEdges( &probe.s.mesh,
                     common::span_t<const geometry_mesh_edge_handle_t>{ edge, 1u }, Vec3d_Make( 1, 0, 0 ), nullptr )
                     .status == geometry_status_t::OK );
        cOperationAllocations = probeState.cAllocationCalls - cBefore;
    }
    REQUIRE( OutstandingAllocations( probeState ) == 0u );
    REQUIRE( cOperationAllocations > 0u );

    for ( common::usize iFailure = 1u; iFailure <= cOperationAllocations; ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        boundary_failure_allocator_state_t state{};
        common::allocator_t allocator = MakeBoundaryFailureAllocator( &state );
        {
            Src m( &allocator );
            m.Quad();
            Desc before, after;
            m.Snapshot( &before.d );
            const geometry_mesh_edge_handle_t edge[] = { m.Edge( 11, 12 ) };
            state.iFailOnCall = state.cAllocationCalls + iFailure;
            const mesh_boundary_extrude_result_t result = MeshBoundary_ExtrudeEdges(
                &m.s.mesh, common::span_t<const geometry_mesh_edge_handle_t>{ edge, 1u }, Vec3d_Make( 1, 0, 0 ), nullptr );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( result.status == geometry_status_t::ALLOCATION_FAILED );
            m.Snapshot( &after.d );
            CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
            m.Valid();
        }
        CHECK( OutstandingAllocations( state ) == 0u );
    }
}

TEST_CASE( "Every detach allocation failure is atomic before a shell split", "[geometry][meshboundary][allocation]" ) {
    common::usize cOperationAllocations = 0u;
    boundary_failure_allocator_state_t probeState{};
    common::allocator_t probeAllocator = MakeBoundaryFailureAllocator( &probeState );
    {
        Src probe( &probeAllocator );
        probe.Cube();
        const geometry_mesh_face_handle_t face[] = { probe.Face( 21 ) };
        const common::usize cBefore = probeState.cAllocationCalls;
        REQUIRE( MeshBoundary_DetachFaces(
                     &probe.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ face, 1u } )
                     .status == geometry_status_t::OK );
        cOperationAllocations = probeState.cAllocationCalls - cBefore;
        CHECK( EditableMesh_ShellCount( &probe.s.mesh ) == 2u );
    }
    REQUIRE( OutstandingAllocations( probeState ) == 0u );
    REQUIRE( cOperationAllocations > 0u );

    for ( common::usize iFailure = 1u; iFailure <= cOperationAllocations; ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        boundary_failure_allocator_state_t state{};
        common::allocator_t allocator = MakeBoundaryFailureAllocator( &state );
        {
            Src m( &allocator );
            m.Cube();
            Desc before, after;
            m.Snapshot( &before.d );
            const geometry_mesh_face_handle_t face[] = { m.Face( 21 ) };
            state.iFailOnCall = state.cAllocationCalls + iFailure;
            const mesh_boundary_detach_result_t result =
                MeshBoundary_DetachFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ face, 1u } );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( result.status == geometry_status_t::ALLOCATION_FAILED );
            m.Snapshot( &after.d );
            CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
            m.Valid();
        }
        CHECK( OutstandingAllocations( state ) == 0u );
    }
}

TEST_CASE( "Source edge extrusion preflights the canonical coordinate domain", "[geometry][meshboundary]" ) {
    Src m;
    m.Quad();
    Desc before, after;
    m.Snapshot( &before.d );
    const geometry_source_id_t edge[] = { Id( 11 ), Id( 12 ) };
    CHECK( MeshSourceEdit_TryExtrudeEdges( &m.s, common::span_t<const geometry_source_id_t>{ edge, 2u },
                                          Vec3d_Make( kMeshSourceCoordinateMax, 0, 0 ), nullptr, nullptr ) ==
           geometry_status_t::NUMERIC_FAILURE );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
    m.Valid();
}

TEST_CASE( "Deleting faces may not leave a pinched (bow-tie) vertex", "[geometry][meshboundary]" ) {
    // 2 x 2 grid; deleting two diagonal quads would leave the centre vertex
    // with two separate fans. That is not a valid source, so the deletion
    // is refused (and FillHole's own pinch check stays a defensive guard).
    Src m;
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
    const geometry_mesh_face_handle_t diag[] = { m.Face( 31 ), m.Face( 32 ) };
    CHECK( MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ diag, 2 } ).status ==
           geometry_status_t::NON_MANIFOLD );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
    // Invalid handles are reported before "deleting everything".
    const geometry_mesh_face_handle_t twice[] = { m.Face( 30 ), m.Face( 30 ), m.Face( 31 ), m.Face( 32 ) };
    CHECK( MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ twice, 4 } ).status ==
           geometry_status_t::INVALID_HANDLE );
}

TEST_CASE( "Add face closes an open box and rejects conflicting winding", "[geometry][meshboundary]" ) {
    Src m;
    m.Cube();
    const geometry_mesh_face_handle_t top[] = { m.Face( 21 ) };
    REQUIRE( MeshBoundary_DeleteFaces( &m.s.mesh, common::span_t<const geometry_mesh_face_handle_t>{ top, 1 } ).status ==
             geometry_status_t::OK );
    Desc before, after;
    m.Snapshot( &before.d );

    // Clockwise seen from above: every new edge runs the same way as the
    // existing boundary edge it would share.
    const geometry_source_id_t cwIds[] = { Id( 14 ), Id( 16 ), Id( 17 ), Id( 15 ) };
    mesh_edit_corner_t cw[4];
    for ( int i = 0; i < 4; ++i ) { cw[i].vertexId = cwIds[i]; }
    CHECK( MeshSourceEdit_TryAddFace( &m.s, common::span_t<const mesh_edit_corner_t>{ cw, 4 }, nullptr, nullptr ) ==
           geometry_status_t::NON_MANIFOLD );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );

    const geometry_source_id_t ccwIds[] = { Id( 14 ), Id( 15 ), Id( 17 ), Id( 16 ) };
    mesh_edit_corner_t ccw[4];
    for ( int i = 0; i < 4; ++i ) { ccw[i].vertexId = ccwIds[i]; }
    geometry_mesh_face_handle_t hNew{};
    mesh_edit_report_t report{};
    REQUIRE( MeshSourceEdit_TryAddFace( &m.s, common::span_t<const mesh_edit_corner_t>{ ccw, 4 }, &hNew, &report ) ==
             geometry_status_t::OK );
    CHECK( report.stats.cFacesFromParent == 1u );
    const mesh_validation_result_t v = MeshValidation_Validate( &m.s.mesh );
    CHECK( v.status == geometry_status_t::OK );
    CHECK( v.fSignedVolume == Approx( 1.0 ) );
    CHECK( EditableMesh_GetFace( &m.s.mesh, hNew )->normal.z == Approx( 1.0 ) );
    m.Valid();

    // The box is closed now: a fifth face on any edge is a third face.
    const geometry_source_id_t extraIds[] = { Id( 14 ), Id( 15 ), Id( 17 ) };
    mesh_edit_corner_t extra[3];
    for ( int i = 0; i < 3; ++i ) { extra[i].vertexId = extraIds[i]; }
    CHECK( MeshSourceEdit_TryAddFace( &m.s, common::span_t<const mesh_edit_corner_t>{ extra, 3 }, nullptr, nullptr ) ==
           geometry_status_t::NON_MANIFOLD );
}

TEST_CASE( "Poly Pen: new faces grow a sheet from its boundary", "[geometry][meshboundary]" ) {
    Src m;
    m.V( 0, 0, 0, 10 );
    m.V( 1, 0, 0, 11 );
    m.V( 1, 1, 0, 12 );
    m.V( 0, 1, 0, 13 );
    m.F( { 0, 1, 2, 3 }, 20 );
    m.d.faces.pData[0].attributes.material.value = 5u;
    m.Build();
    // Quad to the right: two existing boundary vertices, two new points.
    mesh_edit_corner_t ring[4];
    ring[0].vertexId = Id( 12 ); // stitched along 11 -> 12 reversed
    ring[1].vertexId = Id( 11 );
    ring[2].position = Vec3d_Make( 2, 0, 0 );
    ring[3].position = Vec3d_Make( 2, 1, 0 );
    // Winding (12, 11, new0, new1) is clockwise from +z; the stitched edge
    // needs 12 -> 11, which the sheet's 11 -> 12 boundary edge reverses.
    CHECK( MeshSourceEdit_TryAddFace( &m.s, common::span_t<const mesh_edit_corner_t>{ ring, 4 }, nullptr, nullptr ) ==
           geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &m.s.mesh ) == 2u );
    CHECK( EditableMesh_VertexCount( &m.s.mesh ) == 6u );
    CHECK( MeshBoundary_CountBoundaryEdges( &m.s.mesh ) == 6u );
    m.Valid();
    Desc d;
    REQUIRE( MeshSource_TryDescribe( &m.s, &d.d ) == geometry_status_t::OK );
    for ( common::usize f = 0; f < d.d.faces.nCount; ++f ) { CHECK( d.d.faces.pData[f].attributes.material.value == 5u ); }

    // A floating face touching nothing starts a new shell.
    mesh_edit_corner_t island[3];
    island[0].position = Vec3d_Make( 10, 0, 0 );
    island[1].position = Vec3d_Make( 11, 0, 0 );
    island[2].position = Vec3d_Make( 10, 1, 0 );
    mesh_edit_report_t report{};
    CHECK( MeshSourceEdit_TryAddFace( &m.s, common::span_t<const mesh_edit_corner_t>{ island, 3 }, nullptr, &report ) ==
           geometry_status_t::OK );
    CHECK( report.stats.cFacesWithoutParent == 1u );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 2u );
    m.Valid();

    // Touching only at a corner of the sheet would create a second open
    // fan at that vertex.
    mesh_edit_corner_t pinch[3];
    pinch[0].vertexId = Id( 10 );
    pinch[1].position = Vec3d_Make( -1, -1, 0 );
    pinch[2].position = Vec3d_Make( 0, -1, 0 );
    CHECK( MeshSourceEdit_TryAddFace( &m.s, common::span_t<const mesh_edit_corner_t>{ pinch, 3 }, nullptr, nullptr ) ==
           geometry_status_t::NON_MANIFOLD );
    // Degenerate and malformed input.
    mesh_edit_corner_t flat[3];
    flat[0].position = Vec3d_Make( 20, 0, 0 );
    flat[1].position = Vec3d_Make( 21, 0, 0 );
    flat[2].position = Vec3d_Make( 22, 0, 0 );
    CHECK( MeshSourceEdit_TryAddFace( &m.s, common::span_t<const mesh_edit_corner_t>{ flat, 3 }, nullptr, nullptr ) ==
           geometry_status_t::DEGENERATE );
    mesh_edit_corner_t repeat[3];
    repeat[0].vertexId = Id( 10 );
    repeat[1].vertexId = Id( 10 );
    repeat[2].vertexId = Id( 11 );
    CHECK( MeshSourceEdit_TryAddFace( &m.s, common::span_t<const mesh_edit_corner_t>{ repeat, 3 }, nullptr, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Bridging two facing sheets builds a closed box", "[geometry][meshboundary]" ) {
    Src m;
    // Bottom sheet faces -z, top sheet faces +z.
    m.V( 0, 0, 0, 10 );
    m.V( 1, 0, 0, 11 );
    m.V( 1, 1, 0, 12 );
    m.V( 0, 1, 0, 13 );
    m.V( 0, 0, 1, 14 );
    m.V( 1, 0, 1, 15 );
    m.V( 1, 1, 1, 16 );
    m.V( 0, 1, 1, 17 );
    m.F( { 0, 3, 2, 1 }, 20 ); // clockwise from above
    m.F( { 4, 5, 6, 7 }, 21 ); // counter-clockwise from above
    m.Build();
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 2u );
    common::vector_t<geometry_mesh_face_handle_t> faces{};
    REQUIRE( common::Vector_Init( &faces, &m.allocator ) );
    // Bottom edge 10 -> 13 and the top edge above it, 17 -> 14.
    const mesh_boundary_add_result_t r = MeshBoundary_Bridge( &m.s.mesh, m.Edge( 10, 13 ), m.Edge( 17, 14 ), &faces );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.cFacesCreated == 4u );
    CHECK( r.cEdgesJoined == 8u );
    CHECK( faces.nCount == 4u );
    const mesh_validation_result_t v = MeshValidation_Validate( &m.s.mesh );
    CHECK( v.status == geometry_status_t::OK );
    CHECK( v.nEulerCharacteristic == 2 );
    CHECK( v.fSignedVolume == Approx( 1.0 ) );
    CHECK( EditableMesh_ShellCount( &m.s.mesh ) == 1u );
    for ( common::usize i = 0; i < faces.nCount; ++i ) {
        CHECK( EditableMesh_GetFace( &m.s.mesh, faces.pData[i] )->normal.z == Approx( 0.0 ).margin( 1e-12 ) );
    }
    common::Vector_Shutdown( &faces );
    m.Valid();
}

TEST_CASE( "Bridge rejects mismatched or identical loops", "[geometry][meshboundary]" ) {
    Src m;
    m.V( 0, 0, 0, 10 );
    m.V( 1, 0, 0, 11 );
    m.V( 1, 1, 0, 12 );
    m.V( 0, 1, 0, 13 );
    m.V( 0, 0, 1, 14 );
    m.V( 1, 0, 1, 15 );
    m.V( 0, 1, 1, 16 );
    m.F( { 0, 3, 2, 1 }, 20 );
    m.F( { 4, 5, 6 }, 21 ); // triangle: 3-loop vs 4-loop
    m.Build();
    Desc before, after;
    m.Snapshot( &before.d );
    CHECK( MeshBoundary_Bridge( &m.s.mesh, m.Edge( 10, 13 ), m.Edge( 14, 15 ), nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshBoundary_Bridge( &m.s.mesh, m.Edge( 10, 13 ), m.Edge( 13, 12 ), nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    m.Snapshot( &after.d );
    CHECK( MeshSourceDescription_Equal( &before.d, &after.d ) );
}

} // namespace cypher::editor::geometry
