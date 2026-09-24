//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSource_Tests.cpp
//  Purpose: Contract tests for the document-level Mesh source: build from
//           a canonical description, identity sidecars, attribute round
//           trips, clone, and validation.
//  Details: The central oracle is the round trip: for a canonical
//           description D, Describe(Build(D)) == D bit for bit, and Clone
//           preserves the description exactly. Rejection cases each break
//           exactly one rule and must leave the destination empty.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSource.h"
#include "CypherGeometry_MeshTopologyOps.h"
#include "CypherGeometry_MeshValidation.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace cypher::editor::geometry {

using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

struct Desc {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    explicit Desc( common::u64 root = 1 ) {
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( root ) ) == geometry_status_t::OK );
    }
    ~Desc() { MeshSourceDescription_Shutdown( &d ); }
    common::u32 V( double x, double y, double z, common::u64 id ) {
        common::u32 i = 0;
        REQUIRE( MeshSourceDescription_TryAddVertex( &d, Vec3d_Make( x, y, z ), Id( id ), &i ) == geometry_status_t::OK );
        return i;
    }
    void F( std::vector<common::u32> idx, common::u64 id, common::u64 material = 0, common::u32 smoothing = 1 ) {
        mesh_face_attributes_t a{};
        a.material.value = material;
        a.smoothingGroups = smoothing;
        REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ idx.data(), idx.size() },
                                                   Id( id ), a, nullptr ) == geometry_status_t::OK );
    }
};

// Unit cube, outward CCW faces, vertex IDs 10..17, face IDs 20..25.
void Cube( Desc &c ) {
    for ( int i = 0; i < 8; ++i ) {
        c.V( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0, 10u + static_cast<common::u64>( i ) );
    }
    c.F( { 0, 2, 3, 1 }, 20 ); // -z
    c.F( { 4, 5, 7, 6 }, 21 ); // +z
    c.F( { 0, 1, 5, 4 }, 22 ); // -y
    c.F( { 2, 6, 7, 3 }, 23 ); // +y
    c.F( { 0, 4, 6, 2 }, 24 ); // -x
    c.F( { 1, 3, 7, 5 }, 25 ); // +x
}

// 2 x 1 open strip of quads in the z = 0 plane.
void Strip( Desc &c ) {
    c.V( 0, 0, 0, 10 );
    c.V( 1, 0, 0, 11 );
    c.V( 2, 0, 0, 12 );
    c.V( 0, 1, 0, 13 );
    c.V( 1, 1, 0, 14 );
    c.V( 2, 1, 0, 15 );
    c.F( { 0, 1, 4, 3 }, 20 );
    c.F( { 1, 2, 5, 4 }, 21 );
}

// Two open triangles with no shared topology. Vertices 10 and 13 occupy the
// same position so a corruption test can merge their handles without also
// changing either face's geometry or winding.
void DisconnectedTriangles( Desc &c ) {
    c.V( 0, 0, 0, 10 );
    c.V( 1, 0, 0, 11 );
    c.V( 0, 1, 0, 12 );
    c.V( 0, 0, 0, 13 );
    c.V( -1, 0, 0, 14 );
    c.V( 0, -1, 0, 15 );
    c.F( { 0, 1, 2 }, 20 );
    c.F( { 3, 4, 5 }, 21 );
}

struct Source {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t s{};
    ~Source() { MeshSource_Shutdown( &s ); }
};

} // namespace

TEST_CASE( "Mesh source builds a cube and round-trips its description exactly", "[geometry][meshsource]" ) {
    Desc c;
    Cube( c );
    // Non-default attributes on every channel.
    for ( common::usize i = 0; i < c.d.corners.nCount; ++i ) {
        c.d.corners.pData[i].attributes.uv0 = math::vec2d_t{ 0.25 * static_cast<double>( i ), 1.0 / 3.0 };
        c.d.corners.pData[i].attributes.colorRgba = 0xFF00FF00u + static_cast<common::u32>( i );
    }
    c.d.faces.pData[3].attributes.material.value = 77u;
    c.d.faces.pData[3].attributes.smoothingGroups = 6u;
    mesh_edge_attributes_t hard{};
    hard.flags = MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM;
    REQUIRE( MeshSourceDescription_TrySetEdge( &c.d, 3, 1, hard, 0.5 ) == geometry_status_t::OK ); // stored as (1, 3)
    REQUIRE( MeshSourceDescription_TrySetEdge( &c.d, 0, 4, mesh_edge_attributes_t{}, 1.0 ) == geometry_status_t::OK );
    // Canonical edge order is by vertex pair.
    std::swap( c.d.edges.pData[0], c.d.edges.pData[1] );

    Source s;
    mesh_source_validation_t fault{};
    REQUIRE( MeshSource_TryBuild( &c.d, &s.allocator, &s.s, &fault ) == geometry_status_t::OK );
    CHECK( fault.fault == mesh_source_fault_t::NONE );
    CHECK( EditableMesh_VertexCount( &s.s.mesh ) == 8u );
    CHECK( EditableMesh_FaceCount( &s.s.mesh ) == 6u );
    CHECK( EditableMesh_EdgeCount( &s.s.mesh ) == 12u );
    CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::NONE );

    Desc out( 999 );
    REQUIRE( MeshSource_TryDescribe( &s.s, &out.d ) == geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &c.d, &out.d ) );

    Source clone;
    REQUIRE( MeshSource_TryClone( &s.s, &clone.allocator, &clone.s ) == geometry_status_t::OK );
    Desc cloned( 998 );
    REQUIRE( MeshSource_TryDescribe( &clone.s, &cloned.d ) == geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &c.d, &cloned.d ) );
}

TEST_CASE( "Mesh source identity lookups and collection", "[geometry][meshsource]" ) {
    Desc c;
    Cube( c );
    Source s;
    REQUIRE( MeshSource_TryBuild( &c.d, &s.allocator, &s.s ) == geometry_status_t::OK );
    for ( common::u64 id = 10; id < 18; ++id ) {
        geometry_mesh_vertex_handle_t h{};
        REQUIRE( MeshSource_TryFindVertex( &s.s, Id( id ), &h ) );
        CHECK( MeshSource_VertexId( &s.s, h ).value == id );
        const common::u64 i = id - 10u;
        CHECK( math::Vec3d_EqualsExact( EditableMesh_GetVertex( &s.s.mesh, h )->position,
                                        Vec3d_Make( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0,
                                                    ( i & 4 ) ? 1.0 : 0.0 ) ) );
    }
    for ( common::u64 id = 20; id < 26; ++id ) {
        geometry_mesh_face_handle_t h{};
        REQUIRE( MeshSource_TryFindFace( &s.s, Id( id ), &h ) );
        CHECK( MeshSource_FaceId( &s.s, h ).value == id );
    }
    geometry_mesh_vertex_handle_t hv{};
    CHECK_FALSE( MeshSource_TryFindVertex( &s.s, Id( 20 ), &hv ) ); // a face ID, not a vertex
    CHECK_FALSE( MeshSource_TryFindVertex( &s.s, GEOMETRY_SOURCE_ID_INVALID, &hv ) );
    CHECK( MeshSource_VertexId( &s.s, geometry_mesh_vertex_handle_t{ 999u, 1u } ).value ==
           GEOMETRY_SOURCE_ID_INVALID.value );

    common::vector_t<geometry_source_id_t> ids{};
    REQUIRE( common::Vector_Init( &ids, &s.allocator ) );
    REQUIRE( MeshSource_TryCollectSourceIds( &s.s, &ids ) == geometry_status_t::OK );
    REQUIRE( ids.nCount == 15u );
    CHECK( ids.pData[0].value == 1u );
    for ( common::usize i = 1; i < ids.nCount; ++i ) { CHECK( ids.pData[i].value > ids.pData[i - 1].value ); }
    common::Vector_Shutdown( &ids );
}

TEST_CASE( "Open meshes are allowed; description order is preserved", "[geometry][meshsource]" ) {
    Desc c;
    Strip( c );
    Source s;
    REQUIRE( MeshSource_TryBuild( &c.d, &s.allocator, &s.s ) == geometry_status_t::OK );
    CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::NONE );
    Desc out( 5 );
    REQUIRE( MeshSource_TryDescribe( &s.s, &out.d ) == geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &c.d, &out.d ) );
}

TEST_CASE( "Mesh source validation rejects broken topology ownership", "[geometry][meshsource][validation]" ) {
    Desc c;
    Strip( c );
    Source s;
    REQUIRE( MeshSource_TryBuild( &c.d, &s.allocator, &s.s ) == geometry_status_t::OK );

    geometry_mesh_face_handle_t hFace20{};
    geometry_mesh_face_handle_t hFace21{};
    REQUIRE( MeshSource_TryFindFace( &s.s, Id( 20 ), &hFace20 ) );
    REQUIRE( MeshSource_TryFindFace( &s.s, Id( 21 ), &hFace21 ) );

    SECTION( "two faces cannot own one loop" ) {
        mesh_face_record_t *pFace20 = common::GenerationPool_Get( &s.s.mesh.faces, hFace20 );
        const mesh_face_record_t *pFace21 = common::GenerationPool_Get( &s.s.mesh.faces, hFace21 );
        REQUIRE( pFace20 != nullptr );
        REQUIRE( pFace21 != nullptr );
        pFace20->hOuterLoop = pFace21->hOuterLoop;
        CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::INVALID_TOPOLOGY );
    }

    SECTION( "shell cached membership must be exact" ) {
        const mesh_face_record_t *pFace = common::GenerationPool_Get( &s.s.mesh.faces, hFace20 );
        REQUIRE( pFace != nullptr );
        mesh_shell_record_t *pShell = common::GenerationPool_Get( &s.s.mesh.shells, pFace->hShell );
        REQUIRE( pShell != nullptr );
        REQUIRE( pShell->cFaces == 2u );
        pShell->cFaces = 1u;
        CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::INVALID_TOPOLOGY );
    }

    SECTION( "twins cannot be split across edge records" ) {
        const mesh_face_record_t *pFace = common::GenerationPool_Get( &s.s.mesh.faces, hFace20 );
        REQUIRE( pFace != nullptr );
        const mesh_loop_record_t *pLoop = common::GenerationPool_Get( &s.s.mesh.loops, pFace->hOuterLoop );
        REQUIRE( pLoop != nullptr );
        geometry_mesh_half_edge_handle_t hShared = pLoop->hFirstHalfEdge;
        for ( common::u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
            const mesh_half_edge_record_t *pHalfEdge =
                common::GenerationPool_Get( &s.s.mesh.halfEdges, hShared );
            REQUIRE( pHalfEdge != nullptr );
            if ( common::GenerationHandle_IsValid( pHalfEdge->hTwin ) ) { break; }
            hShared = pHalfEdge->hNext;
        }
        const mesh_half_edge_record_t *pShared =
            common::GenerationPool_Get( &s.s.mesh.halfEdges, hShared );
        REQUIRE( pShared != nullptr );
        REQUIRE( common::GenerationHandle_IsValid( pShared->hTwin ) );
        const geometry_mesh_half_edge_handle_t hTwin = pShared->hTwin;
        mesh_edge_record_t duplicate{};
        duplicate.hHalfEdge = hTwin;
        const auto inserted = common::GenerationPool_Insert( &s.s.mesh.edges, duplicate );
        REQUIRE( inserted.status == common::generation_pool_status_t::OK );
        mesh_half_edge_record_t *pTwin = common::GenerationPool_Get( &s.s.mesh.halfEdges, hTwin );
        REQUIRE( pTwin != nullptr );
        pTwin->hEdge = inserted.handle;
        CHECK_FALSE( MeshValidation_Validate( &s.s.mesh ).bEdgeLinks );
        CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::INVALID_TOPOLOGY );
    }

    SECTION( "every live half-edge must belong to a loop ring" ) {
        const mesh_face_record_t *pFace = common::GenerationPool_Get( &s.s.mesh.faces, hFace20 );
        REQUIRE( pFace != nullptr );
        const mesh_loop_record_t *pLoop = common::GenerationPool_Get( &s.s.mesh.loops, pFace->hOuterLoop );
        REQUIRE( pLoop != nullptr );
        const mesh_half_edge_record_t *pExisting =
            common::GenerationPool_Get( &s.s.mesh.halfEdges, pLoop->hFirstHalfEdge );
        REQUIRE( pExisting != nullptr );

        mesh_half_edge_record_t orphan{};
        orphan.hOrigin = pExisting->hOrigin;
        orphan.hLoop = pFace->hOuterLoop;
        const auto insertedHalfEdge = common::GenerationPool_Insert( &s.s.mesh.halfEdges, orphan );
        REQUIRE( insertedHalfEdge.status == common::generation_pool_status_t::OK );
        mesh_edge_record_t orphanEdge{};
        orphanEdge.hHalfEdge = insertedHalfEdge.handle;
        const auto insertedEdge = common::GenerationPool_Insert( &s.s.mesh.edges, orphanEdge );
        REQUIRE( insertedEdge.status == common::generation_pool_status_t::OK );
        mesh_half_edge_record_t *pOrphan =
            common::GenerationPool_Get( &s.s.mesh.halfEdges, insertedHalfEdge.handle );
        REQUIRE( pOrphan != nullptr );
        pOrphan->hNext = insertedHalfEdge.handle;
        pOrphan->hPrev = insertedHalfEdge.handle;
        pOrphan->hEdge = insertedEdge.handle;

        CHECK_FALSE( MeshValidation_Validate( &s.s.mesh ).bClosedLoops );
        CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::INVALID_TOPOLOGY );
    }

    SECTION( "non-finite stored face normals cannot pass winding validation" ) {
        mesh_face_record_t *pFace = common::GenerationPool_Get( &s.s.mesh.faces, hFace20 );
        REQUIRE( pFace != nullptr );
        pFace->normal.x = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE( MeshValidation_Validate( &s.s.mesh ).bConsistentWinding );
        CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::INVALID_TOPOLOGY );
    }
}

TEST_CASE( "Mesh source validation rejects disconnected shell membership and vertex fans",
           "[geometry][meshsource][validation][connectivity]" ) {
    Desc c;
    DisconnectedTriangles( c );
    Source s;
    REQUIRE( MeshSource_TryBuild( &c.d, &s.allocator, &s.s ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSource_Validate( &s.s, &s.allocator ).fault ==
             mesh_source_fault_t::NONE );

    geometry_mesh_face_handle_t hFace20{};
    geometry_mesh_face_handle_t hFace21{};
    REQUIRE( MeshSource_TryFindFace( &s.s, Id( 20 ), &hFace20 ) );
    REQUIRE( MeshSource_TryFindFace( &s.s, Id( 21 ), &hFace21 ) );

    SECTION( "disconnected faces cannot masquerade as one shell" ) {
        mesh_face_record_t *pFace20 =
            common::GenerationPool_Get( &s.s.mesh.faces, hFace20 );
        mesh_face_record_t *pFace21 =
            common::GenerationPool_Get( &s.s.mesh.faces, hFace21 );
        REQUIRE( pFace20 != nullptr );
        REQUIRE( pFace21 != nullptr );
        const geometry_mesh_shell_handle_t hShell20 = pFace20->hShell;
        const geometry_mesh_shell_handle_t hShell21 = pFace21->hShell;
        REQUIRE_FALSE( ( hShell20.nSlot == hShell21.nSlot &&
                         hShell20.nGeneration == hShell21.nGeneration ) );
        mesh_shell_record_t *pShell20 =
            common::GenerationPool_Get( &s.s.mesh.shells, hShell20 );
        REQUIRE( pShell20 != nullptr );

        pFace21->hShell = hShell20;
        pShell20->cFaces = 2u;
        REQUIRE( common::GenerationPool_Remove(
                     &s.s.mesh.shells, hShell21 ) ==
                 common::generation_pool_status_t::OK );

        // Local ownership and cached counts are coherent. Only an actual
        // face-adjacency traversal can see that the declared shell consists
        // of two disconnected components.
        CHECK( MeshValidation_Validate( &s.s.mesh ).bShellLinks );
        CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault ==
               mesh_source_fault_t::INVALID_TOPOLOGY );
    }

    SECTION( "one vertex cannot own two disconnected incident fans" ) {
        geometry_mesh_vertex_handle_t hVertex10{};
        geometry_mesh_vertex_handle_t hVertex13{};
        REQUIRE( MeshSource_TryFindVertex( &s.s, Id( 10 ), &hVertex10 ) );
        REQUIRE( MeshSource_TryFindVertex( &s.s, Id( 13 ), &hVertex13 ) );

        (void)common::GenerationPool_ForEach(
            &s.s.mesh.halfEdges,
            [&]( geometry_mesh_half_edge_handle_t hHalfEdge,
                 const mesh_half_edge_record_t &halfEdge ) noexcept -> common::bool_t {
                if ( halfEdge.hOrigin.nSlot == hVertex13.nSlot &&
                     halfEdge.hOrigin.nGeneration == hVertex13.nGeneration ) {
                    mesh_half_edge_record_t *pMutable =
                        common::GenerationPool_Get(
                            &s.s.mesh.halfEdges, hHalfEdge );
                    if ( pMutable != nullptr ) {
                        pMutable->hOrigin = hVertex10;
                    }
                }
                return true;
            } );
        REQUIRE( common::GenerationPool_Remove(
                     &s.s.mesh.vertices, hVertex13 ) ==
                 common::generation_pool_status_t::OK );

        // The basic handle/link validator still sees a referenced vertex;
        // the source-level fan traversal must reject the bow-tie topology.
        CHECK( MeshValidation_Validate( &s.s.mesh ).bAllVerticesReferenced );
        CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault ==
               mesh_source_fault_t::INVALID_TOPOLOGY );
    }
}

TEST_CASE( "Default edge data is kept out of canonical mesh descriptions", "[geometry][meshsource]" ) {
    Desc c;
    Strip( c );
    REQUIRE( MeshSourceDescription_TrySetEdge( &c.d, 0, 1, mesh_edge_attributes_t{}, 0.0 ) ==
             geometry_status_t::OK );
    CHECK( c.d.edges.nCount == 0u );

    mesh_edge_attributes_t hard{};
    hard.flags = MESH_EDGE_FLAG_HARD;
    REQUIRE( MeshSourceDescription_TrySetEdge( &c.d, 0, 1, hard, 0.5 ) == geometry_status_t::OK );
    REQUIRE( c.d.edges.nCount == 1u );
    REQUIRE( MeshSourceDescription_TrySetEdge( &c.d, 0, 1, mesh_edge_attributes_t{}, 0.0 ) ==
             geometry_status_t::OK );
    CHECK( c.d.edges.nCount == 0u );
}

TEST_CASE( "Mesh source build rejects each invalid description and stays empty", "[geometry][meshsource]" ) {
    auto expectReject = [&]( Desc &c, geometry_status_t expected, mesh_source_fault_t expectedFault ) {
        Source s;
        mesh_source_validation_t fault{};
        CHECK( MeshSource_TryBuild( &c.d, &s.allocator, &s.s, &fault ) == expected );
        CHECK( fault.fault == expectedFault );
        CHECK_FALSE( MeshSource_IsInitialized( &s.s ) );
    };
    {
        Desc c;
        Cube( c );
        c.d.faces.pData[2].sourceId = Id( 13 ); // collides with a vertex
        expectReject( c, geometry_status_t::IDENTITY_CONFLICT, mesh_source_fault_t::DUPLICATE_SOURCE_ID );
    }
    {
        Desc c;
        Cube( c );
        c.d.vertices.pData[0].sourceId = Id( 1 ); // collides with the root
        expectReject( c, geometry_status_t::IDENTITY_CONFLICT, mesh_source_fault_t::DUPLICATE_SOURCE_ID );
    }
    {
        Desc c( 0 );
        Cube( c );
        expectReject( c, geometry_status_t::INVALID_ARGUMENT, mesh_source_fault_t::INVALID_ROOT_ID );
    }
    {
        Desc c;
        Cube( c );
        c.V( 5, 5, 5, 99 ); // isolated
        expectReject( c, geometry_status_t::INVALID_TOPOLOGY, mesh_source_fault_t::INVALID_TOPOLOGY );
    }
    {
        Desc c;
        Strip( c );
        c.d.corners.pData[2].iVertex = 0; // face 0 visits vertex 0 twice
        expectReject( c, geometry_status_t::INVALID_TOPOLOGY, mesh_source_fault_t::INVALID_TOPOLOGY );
    }
    {
        Desc c;
        Strip( c );
        c.d.faces.pData[1].iFirstCorner = 3; // overlaps face 0's run
        expectReject( c, geometry_status_t::INVALID_TOPOLOGY, mesh_source_fault_t::INVALID_TOPOLOGY );
    }
    {
        Desc c;
        Strip( c );
        c.d.vertices.pData[4].position.y = std::nan( "" );
        expectReject( c, geometry_status_t::NUMERIC_FAILURE, mesh_source_fault_t::NON_FINITE );
    }
    {
        Desc c;
        Strip( c );
        c.d.corners.pData[3].attributes.uv1.x = std::nan( "" );
        expectReject( c, geometry_status_t::NUMERIC_FAILURE, mesh_source_fault_t::INVALID_ATTRIBUTES );
    }
    {
        Desc c;
        Strip( c );
        c.d.vertices.pData[4].position.y = kMeshSourceCoordinateMax * 2.0;
        expectReject( c, geometry_status_t::NUMERIC_FAILURE, mesh_source_fault_t::COORDINATE_RANGE );
    }
    {
        Desc c;
        Strip( c );
        // Edge (0, 2) exists in no face.
        REQUIRE( MeshSourceDescription_TrySetEdge( &c.d, 0, 2, mesh_edge_attributes_t{}, 1.0 ) ==
                 geometry_status_t::OK );
        expectReject( c, geometry_status_t::INVALID_TOPOLOGY, mesh_source_fault_t::INVALID_ATTRIBUTES );
    }
    {
        Desc c;
        Strip( c );
        CHECK( MeshSourceDescription_TrySetEdge( &c.d, 0, 1, mesh_edge_attributes_t{}, 1.01 ) ==
               geometry_status_t::INVALID_ARGUMENT );
    }
    {
        Desc c;
        Strip( c );
        mesh_source_edge_t e{};
        e.iVertexA = 0;
        e.iVertexB = 1;
        REQUIRE( common::Vector_PushBack( &c.d.edges, e ) );
        REQUIRE( common::Vector_PushBack( &c.d.edges, e ) ); // repeated entry
        expectReject( c, geometry_status_t::INVALID_ARGUMENT, mesh_source_fault_t::INVALID_ATTRIBUTES );
    }
    {
        Desc c;
        Strip( c );
        REQUIRE( common::Vector_Resize( &c.d.edges, c.d.corners.nCount + 1u ) );
        expectReject( c, geometry_status_t::LIMIT_EXCEEDED, mesh_source_fault_t::INVALID_TOPOLOGY );
    }
    {
        Desc c;
        Strip( c );
        // Third face on edge (1, 4) -> non-manifold.
        c.V( 1, 0.5, 1, 16 );
        c.F( { 1, 6, 4 }, 22 );
        Source s;
        mesh_source_validation_t fault{};
        CHECK( MeshSource_TryBuild( &c.d, &s.allocator, &s.s, &fault ) != geometry_status_t::OK );
        CHECK( fault.fault == mesh_source_fault_t::INVALID_TOPOLOGY );
        CHECK_FALSE( MeshSource_IsInitialized( &s.s ) );
    }
    {
        Desc c;
        Strip( c );
        // Second quad wound the other way: shared edge used in the same
        // direction twice.
        std::swap( c.d.corners.pData[5].iVertex, c.d.corners.pData[7].iVertex );
        Source s;
        mesh_source_validation_t fault{};
        CHECK( MeshSource_TryBuild( &c.d, &s.allocator, &s.s, &fault ) != geometry_status_t::OK );
        CHECK( fault.fault == mesh_source_fault_t::INVALID_TOPOLOGY );
        CHECK_FALSE( MeshSource_IsInitialized( &s.s ) );
    }
    {
        Desc c;
        c.V( 0, 0, 0, 10 );
        expectReject( c, geometry_status_t::DEGENERATE, mesh_source_fault_t::INVALID_TOPOLOGY );
    }
}

TEST_CASE( "Topology edits leave new elements unidentified until assigned", "[geometry][meshsource]" ) {
    Desc c;
    Cube( c );
    Source s;
    REQUIRE( MeshSource_TryBuild( &c.d, &s.allocator, &s.s ) == geometry_status_t::OK );

    geometry_mesh_face_handle_t hTop{};
    REQUIRE( MeshSource_TryFindFace( &s.s, Id( 21 ), &hTop ) );
    const mesh_face_record_t *pTop = EditableMesh_GetFace( &s.s.mesh, hTop );
    const mesh_loop_record_t *pLoop = EditableMesh_GetLoop( &s.s.mesh, pTop->hOuterLoop );
    const geometry_mesh_edge_handle_t hEdge = EditableMesh_GetHalfEdge( &s.s.mesh, pLoop->hFirstHalfEdge )->hEdge;
    const mesh_split_edge_result_t split = MeshOps_SplitEdge( &s.s.mesh, hEdge, 0.5 );
    REQUIRE( split.status == geometry_status_t::OK );

    CHECK( MeshSource_VertexId( &s.s, split.hNewVertex ).value == GEOMETRY_SOURCE_ID_INVALID.value );
    CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::MISSING_VERTEX_ID );
    Desc out( 7 );
    CHECK( MeshSource_TryDescribe( &s.s, &out.d ) == geometry_status_t::CORRUPT_STATE );
    CHECK( out.d.vertices.nCount == 0u ); // failure leaves the description empty

    geometry_source_id_allocator_t ids{ Id( 500 ) };
    common::u32 cAssigned = 0;
    REQUIRE( MeshSource_TryAssignMissingIds( &s.s, &ids, &cAssigned ) == geometry_status_t::OK );
    CHECK( cAssigned == 1u );
    CHECK( ids.next.value == 501u );
    CHECK( MeshSource_VertexId( &s.s, split.hNewVertex ).value == 500u );
    CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::NONE );
    REQUIRE( MeshSource_TryAssignMissingIds( &s.s, &ids, &cAssigned ) == geometry_status_t::OK );
    CHECK( cAssigned == 0u );
    CHECK( ids.next.value == 501u );

    // The edited mesh still round-trips: 9 vertices, face 21 now a pentagon.
    REQUIRE( MeshSource_TryDescribe( &s.s, &out.d ) == geometry_status_t::OK );
    CHECK( out.d.vertices.nCount == 9u );
    Source rebuilt;
    REQUIRE( MeshSource_TryBuild( &out.d, &rebuilt.allocator, &rebuilt.s ) == geometry_status_t::OK );
    Desc again( 8 );
    REQUIRE( MeshSource_TryDescribe( &rebuilt.s, &again.d ) == geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &out.d, &again.d ) );

    // Explicit identity writes are checked.
    CHECK( MeshSource_TrySetFaceId( &s.s, hTop, GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshSource_TrySetVertexId( &s.s, geometry_mesh_vertex_handle_t{ 3u, 99u }, Id( 1000 ) ) ==
           geometry_status_t::STALE_HANDLE );
    // A duplicate written by hand is caught by Validate.
    REQUIRE( MeshSource_TrySetVertexId( &s.s, split.hNewVertex, Id( 10 ) ) == geometry_status_t::OK );
    const mesh_source_validation_t v = MeshSource_Validate( &s.s, &s.allocator );
    CHECK( v.fault == mesh_source_fault_t::DUPLICATE_SOURCE_ID );
    CHECK( v.sourceId.value == 10u );
}

TEST_CASE( "Assigning missing IDs is failure-atomic on allocator exhaustion", "[geometry][meshsource]" ) {
    Desc c;
    Cube( c );
    Source s;
    REQUIRE( MeshSource_TryBuild( &c.d, &s.allocator, &s.s ) == geometry_status_t::OK );
    geometry_mesh_face_handle_t hFace{};
    REQUIRE( MeshSource_TryFindFace( &s.s, Id( 20 ), &hFace ) );
    const mesh_extrude_face_result_t ex = MeshOps_ExtrudeFace( &s.s.mesh, hFace, 1.0 );
    REQUIRE( ex.status == geometry_status_t::OK );
    const mesh_validation_result_t topology = MeshValidation_Validate( &s.s.mesh );
    CAPTURE( topology.bClosedLoops,
             topology.bAllVerticesReferenced,
             topology.bEdgeLinks,
             topology.bShellLinks,
             topology.bConsistentWinding );
    // Extrusion adds 4 vertices and 5 faces (4 sides + the moved cap).
    geometry_source_id_allocator_t nearlyDone{ geometry_source_id_t{ ~0ull - 3u } };
    const geometry_source_id_allocator_t before = nearlyDone;
    common::u32 cAssigned = 77;
    CHECK( MeshSource_TryAssignMissingIds( &s.s, &nearlyDone, &cAssigned ) != geometry_status_t::OK );
    CHECK( nearlyDone.next.value == before.next.value );
    CHECK( cAssigned == 0u );
    CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::MISSING_VERTEX_ID );

    geometry_source_id_allocator_t ids{ Id( 100 ) };
    REQUIRE( MeshSource_TryAssignMissingIds( &s.s, &ids, &cAssigned ) == geometry_status_t::OK );
    CHECK( cAssigned == 9u );
    CHECK( MeshSource_Validate( &s.s, &s.allocator ).fault == mesh_source_fault_t::NONE );
}

} // namespace cypher::editor::geometry
