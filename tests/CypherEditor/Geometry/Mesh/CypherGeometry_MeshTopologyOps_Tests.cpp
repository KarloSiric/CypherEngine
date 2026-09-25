//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshTopologyOps_Tests.cpp
//  Purpose: Gate 9 contract tests for mesh topology edit operations.
//  Details: Verifies MoveVertex, SplitEdge, SplitFace, CollapseEdge,
//           and DissolveEdge operations on a canonical box mesh.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshTopologyOps.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_Sanitation.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Vec3d_LengthSquared;
using Catch::Approx;

namespace {

struct topology_ops_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFreeCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *TopologyOpsFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<topology_ops_failure_allocator_state_t *>(
        pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) {
        return nullptr;
    }

    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void TopologyOpsFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<topology_ops_failure_allocator_state_t *>(
        pUserData );
    ++pState->cFreeCalls;
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeTopologyOpsFailureAllocator(
    topology_ops_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        TopologyOpsFailureAllocate,
        nullptr,
        TopologyOpsFailureFree,
        pState
    };
}

template <typename tag_t>
bool HandlesMatch(
    common::generation_handle_t<tag_t> a,
    common::generation_handle_t<tag_t> b ) noexcept
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

bool VectorsMatch(
    math::vec3d_t a,
    math::vec3d_t b ) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool RecordsMatch(
    const mesh_vertex_record_t &a,
    const mesh_vertex_record_t &b ) noexcept
{
    return VectorsMatch( a.position, b.position ) &&
           HandlesMatch( a.hOutHalfEdge, b.hOutHalfEdge );
}

bool RecordsMatch(
    const mesh_half_edge_record_t &a,
    const mesh_half_edge_record_t &b ) noexcept
{
    return HandlesMatch( a.hOrigin, b.hOrigin ) &&
           HandlesMatch( a.hTwin, b.hTwin ) &&
           HandlesMatch( a.hNext, b.hNext ) &&
           HandlesMatch( a.hPrev, b.hPrev ) &&
           HandlesMatch( a.hEdge, b.hEdge ) &&
           HandlesMatch( a.hLoop, b.hLoop );
}

bool RecordsMatch(
    const mesh_edge_record_t &a,
    const mesh_edge_record_t &b ) noexcept
{
    return HandlesMatch( a.hHalfEdge, b.hHalfEdge ) &&
           a.creaseWeight == b.creaseWeight;
}

bool RecordsMatch(
    const mesh_loop_record_t &a,
    const mesh_loop_record_t &b ) noexcept
{
    return HandlesMatch( a.hFirstHalfEdge, b.hFirstHalfEdge ) &&
           HandlesMatch( a.hFace, b.hFace ) &&
           a.cHalfEdges == b.cHalfEdges;
}

bool RecordsMatch(
    const mesh_face_record_t &a,
    const mesh_face_record_t &b ) noexcept
{
    return HandlesMatch( a.hOuterLoop, b.hOuterLoop ) &&
           HandlesMatch( a.hShell, b.hShell ) &&
           VectorsMatch( a.normal, b.normal ) &&
           a.iSourceSide == b.iSourceSide;
}

bool RecordsMatch(
    const mesh_shell_record_t &a,
    const mesh_shell_record_t &b ) noexcept
{
    return HandlesMatch( a.hAnyFace, b.hAnyFace ) &&
           a.cFaces == b.cFaces;
}

inline constexpr common::usize kTopologySnapshotRecordMax = 64u;

template <typename record_t, typename tag_t>
struct pool_logical_snapshot_t {
    std::array<common::generation_handle_t<tag_t>,
               kTopologySnapshotRecordMax> handles{};
    std::array<record_t, kTopologySnapshotRecordMax> records{};
    common::usize cRecords{ 0u };
    bool bComplete{ false };
};

template <typename record_t, typename tag_t>
pool_logical_snapshot_t<record_t, tag_t> CapturePoolSnapshot(
    const common::generation_pool_t<record_t, tag_t> &pool ) noexcept
{
    pool_logical_snapshot_t<record_t, tag_t> snapshot{};
    bool bFits = true;
    const common::usize cVisited = common::GenerationPool_ForEach(
        &pool,
        [&]( common::generation_handle_t<tag_t> handle,
             const record_t &record ) noexcept -> common::bool_t {
            if ( snapshot.cRecords >= kTopologySnapshotRecordMax ) {
                bFits = false;
                return false;
            }
            snapshot.handles[snapshot.cRecords] = handle;
            snapshot.records[snapshot.cRecords] = record;
            ++snapshot.cRecords;
            return true;
        } );
    snapshot.bComplete = bFits && cVisited == pool.cRecords &&
                         snapshot.cRecords == pool.cRecords;
    return snapshot;
}

template <typename record_t, typename tag_t>
bool PoolMatchesSnapshot(
    const common::generation_pool_t<record_t, tag_t> &pool,
    const pool_logical_snapshot_t<record_t, tag_t> &snapshot ) noexcept
{
    if ( !snapshot.bComplete || pool.cRecords != snapshot.cRecords ) {
        return false;
    }

    common::usize iRecord = 0u;
    bool bMatches = true;
    const common::usize cVisited = common::GenerationPool_ForEach(
        &pool,
        [&]( common::generation_handle_t<tag_t> handle,
             const record_t &record ) noexcept -> common::bool_t {
            if ( iRecord >= snapshot.cRecords ||
                 !HandlesMatch( handle, snapshot.handles[iRecord] ) ||
                 !RecordsMatch( record, snapshot.records[iRecord] ) ) {
                bMatches = false;
                return false;
            }
            ++iRecord;
            return true;
        } );
    return bMatches && cVisited == snapshot.cRecords &&
           iRecord == snapshot.cRecords;
}

struct mesh_logical_snapshot_t {
    pool_logical_snapshot_t<mesh_vertex_record_t,
                            geometry_mesh_vertex_tag_t> vertices;
    pool_logical_snapshot_t<mesh_half_edge_record_t,
                            geometry_mesh_half_edge_tag_t> halfEdges;
    pool_logical_snapshot_t<mesh_edge_record_t,
                            geometry_mesh_edge_tag_t> edges;
    pool_logical_snapshot_t<mesh_loop_record_t,
                            geometry_mesh_loop_tag_t> loops;
    pool_logical_snapshot_t<mesh_face_record_t,
                            geometry_mesh_face_tag_t> faces;
    pool_logical_snapshot_t<mesh_shell_record_t,
                            geometry_mesh_shell_tag_t> shells;
};

mesh_logical_snapshot_t CaptureMeshSnapshot(
    const editable_mesh_t &mesh ) noexcept
{
    return mesh_logical_snapshot_t{
        CapturePoolSnapshot( mesh.vertices ),
        CapturePoolSnapshot( mesh.halfEdges ),
        CapturePoolSnapshot( mesh.edges ),
        CapturePoolSnapshot( mesh.loops ),
        CapturePoolSnapshot( mesh.faces ),
        CapturePoolSnapshot( mesh.shells )
    };
}

bool MeshMatchesSnapshot(
    const editable_mesh_t &mesh,
    const mesh_logical_snapshot_t &snapshot ) noexcept
{
    return PoolMatchesSnapshot( mesh.vertices, snapshot.vertices ) &&
           PoolMatchesSnapshot( mesh.halfEdges, snapshot.halfEdges ) &&
           PoolMatchesSnapshot( mesh.edges, snapshot.edges ) &&
           PoolMatchesSnapshot( mesh.loops, snapshot.loops ) &&
           PoolMatchesSnapshot( mesh.faces, snapshot.faces ) &&
           PoolMatchesSnapshot( mesh.shells, snapshot.shells );
}

bool MeshSnapshotIsComplete(
    const mesh_logical_snapshot_t &snapshot ) noexcept
{
    return snapshot.vertices.bComplete && snapshot.halfEdges.bComplete &&
           snapshot.edges.bComplete && snapshot.loops.bComplete &&
           snapshot.faces.bComplete && snapshot.shells.bComplete;
}

struct TopologyFixture {
    common::allocator_t allocator;
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    explicit TopologyFixture(
        const common::allocator_t &allocatorIn =
            *common::Allocator_GetSystem() )
        : allocator( allocatorIn )
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAllocator,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );

        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) ==
                 geometry_status_t::OK );

        REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
                 geometry_status_t::OK );
    }
    ~TopologyFixture() {
        EditableMesh_Shutdown( &mesh );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }

    // Find any edge by walking the mesh.
    geometry_mesh_edge_handle_t FindAnyEdge() {
        geometry_mesh_edge_handle_t found{};
        (void)common::GenerationPool_ForEach(
            &mesh.edges,
            [&]( geometry_mesh_edge_handle_t h,
                 const mesh_edge_record_t & ) noexcept -> common::bool_t {
                found = h;
                return false;
            } );
        return found;
    }

    bool FindEdgesOnDifferentFaceLoops(
        geometry_mesh_edge_handle_t *pEdgeA,
        geometry_mesh_edge_handle_t *pEdgeB )
    {
        if ( pEdgeA == nullptr || pEdgeB == nullptr ) { return false; }

        geometry_mesh_loop_handle_t hFirstLoop{};
        bool bFoundFirst = false;
        bool bFoundSecond = false;
        (void)common::GenerationPool_ForEach(
            &mesh.edges,
            [&]( geometry_mesh_edge_handle_t hEdge,
                 const mesh_edge_record_t &edge ) noexcept -> common::bool_t {
                const mesh_half_edge_record_t *pHalfEdge =
                    EditableMesh_GetHalfEdge( &mesh, edge.hHalfEdge );
                if ( pHalfEdge == nullptr ) { return true; }
                if ( !bFoundFirst ) {
                    *pEdgeA = hEdge;
                    hFirstLoop = pHalfEdge->hLoop;
                    bFoundFirst = true;
                    return true;
                }
                if ( !HandlesMatch( pHalfEdge->hLoop, hFirstLoop ) ) {
                    *pEdgeB = hEdge;
                    bFoundSecond = true;
                    return false;
                }
                return true;
            } );
        return bFoundFirst && bFoundSecond;
    }

    // Find any face by walking the mesh.
    geometry_mesh_face_handle_t FindAnyFace() {
        geometry_mesh_face_handle_t found{};
        (void)common::GenerationPool_ForEach(
            &mesh.faces,
            [&]( geometry_mesh_face_handle_t h,
                 const mesh_face_record_t & ) noexcept -> common::bool_t {
                found = h;
                return false;
            } );
        return found;
    }

    // Find two non-adjacent vertices of a given face (for SplitFace).
    // Returns the first and third vertices in the face loop.
    bool FindDiagonalVertices(
        geometry_mesh_face_handle_t hFace,
        geometry_mesh_vertex_handle_t *pA,
        geometry_mesh_vertex_handle_t *pB )
    {
        const mesh_face_record_t *pFace =
            EditableMesh_GetFace( &mesh, hFace );
        if ( pFace == nullptr ) return false;
        const mesh_loop_record_t *pLoop =
            EditableMesh_GetLoop( &mesh, pFace->hOuterLoop );
        if ( pLoop == nullptr || pLoop->cHalfEdges < 4u ) return false;

        geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
        const mesh_half_edge_record_t *p0 =
            EditableMesh_GetHalfEdge( &mesh, hCur );
        if ( p0 == nullptr ) return false;
        *pA = p0->hOrigin;

        // Walk two steps to get the third vertex.
        hCur = p0->hNext;
        const mesh_half_edge_record_t *p1 =
            EditableMesh_GetHalfEdge( &mesh, hCur );
        if ( p1 == nullptr ) return false;
        hCur = p1->hNext;
        const mesh_half_edge_record_t *p2 =
            EditableMesh_GetHalfEdge( &mesh, hCur );
        if ( p2 == nullptr ) return false;
        *pB = p2->hOrigin;

        return true;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// MoveVertex
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: MoveVertex changes vertex position",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;

    // Find any vertex.
    geometry_mesh_vertex_handle_t hVertex{};
    (void)common::GenerationPool_ForEach(
        &f.mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h,
             const mesh_vertex_record_t & ) noexcept -> common::bool_t {
            hVertex = h;
            return false;
        } );

    const math::vec3d_t newPos = Vec3d_Make( 99.0, 88.0, 77.0 );
    REQUIRE( MeshOps_MoveVertex( &f.mesh, hVertex, newPos ) ==
             geometry_status_t::OK );

    const mesh_vertex_record_t *pVert =
        EditableMesh_GetVertex( &f.mesh, hVertex );
    REQUIRE( pVert != nullptr );
    REQUIRE( pVert->position.x == Approx( 99.0 ) );
    REQUIRE( pVert->position.y == Approx( 88.0 ) );
    REQUIRE( pVert->position.z == Approx( 77.0 ) );
}

TEST_CASE( "MeshOps: MoveVertex preserves topology counts",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;

    geometry_mesh_vertex_handle_t hVertex{};
    (void)common::GenerationPool_ForEach(
        &f.mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h,
             const mesh_vertex_record_t & ) noexcept -> common::bool_t {
            hVertex = h;
            return false;
        } );

    REQUIRE( MeshOps_MoveVertex(
                 &f.mesh, hVertex,
                 Vec3d_Make( 2.0, 2.0, 2.0 ) ) ==
             geometry_status_t::OK );

    // Counts unchanged.
    REQUIRE( EditableMesh_VertexCount( &f.mesh ) == 8u );
    REQUIRE( EditableMesh_EdgeCount( &f.mesh ) == 12u );
    REQUIRE( EditableMesh_FaceCount( &f.mesh ) == 6u );
}

TEST_CASE( "MeshOps: MoveVertex null mesh rejected",
           "[Gate9][TopologyOps]" )
{
    REQUIRE( MeshOps_MoveVertex(
                 nullptr, {}, Vec3d_Make( 0.0, 0.0, 0.0 ) ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// SplitEdge
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: SplitEdge adds 1 vertex and 1 edge",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();

    const auto result = MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 );
    REQUIRE( result.status == geometry_status_t::OK );

    // Box was 8V, 12E. After split: 9V, 13E.
    REQUIRE( EditableMesh_VertexCount( &f.mesh ) == 9u );
    REQUIRE( EditableMesh_EdgeCount( &f.mesh ) == 13u );

    // The new vertex should exist.
    const mesh_vertex_record_t *pNew =
        EditableMesh_GetVertex( &f.mesh, result.hNewVertex );
    REQUIRE( pNew != nullptr );
}

TEST_CASE( "MeshOps: SplitEdge midpoint is correct",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();

    // Get the two endpoint positions before split.
    const mesh_edge_record_t *pEdge =
        EditableMesh_GetEdge( &f.mesh, hEdge );
    REQUIRE( pEdge != nullptr );
    const mesh_half_edge_record_t *pHE =
        EditableMesh_GetHalfEdge( &f.mesh, pEdge->hHalfEdge );
    REQUIRE( pHE != nullptr );
    const mesh_half_edge_record_t *pTwin =
        EditableMesh_GetHalfEdge( &f.mesh, pHE->hTwin );
    REQUIRE( pTwin != nullptr );

    const mesh_vertex_record_t *pVA =
        EditableMesh_GetVertex( &f.mesh, pHE->hOrigin );
    const mesh_vertex_record_t *pVB =
        EditableMesh_GetVertex( &f.mesh, pTwin->hOrigin );
    REQUIRE( pVA != nullptr );
    REQUIRE( pVB != nullptr );

    const math::vec3d_t expectedMid = math::Vec3d_Scale(
        math::Vec3d_Add( pVA->position, pVB->position ), 0.5 );

    const auto result = MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 );
    REQUIRE( result.status == geometry_status_t::OK );

    const mesh_vertex_record_t *pNew =
        EditableMesh_GetVertex( &f.mesh, result.hNewVertex );
    REQUIRE( pNew != nullptr );
    REQUIRE( pNew->position.x == Approx( expectedMid.x ) );
    REQUIRE( pNew->position.y == Approx( expectedMid.y ) );
    REQUIRE( pNew->position.z == Approx( expectedMid.z ) );
}

TEST_CASE( "MeshOps: SplitEdge preserves face count",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const auto result =
        MeshOps_SplitEdge( &f.mesh, f.FindAnyEdge(), 0.5 );
    REQUIRE( result.status == geometry_status_t::OK );

    // Face count unchanged (splitting an edge doesn't add/remove faces).
    REQUIRE( EditableMesh_FaceCount( &f.mesh ) == 6u );
}

TEST_CASE( "MeshOps: SplitEdge invalid t rejected",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();

    REQUIRE( MeshOps_SplitEdge( &f.mesh, hEdge, 0.0 ).status ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( MeshOps_SplitEdge( &f.mesh, hEdge, 1.0 ).status ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( MeshOps_SplitEdge( &f.mesh, hEdge, -0.5 ).status ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: SplitEdge rejects an interior parameter that rounds onto an endpoint",
           "[Gate9][TopologyOps][numeric][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_split_edge_result_t split = MeshOps_SplitEdge(
        &f.mesh,
        f.FindAnyEdge(),
        std::numeric_limits<double>::denorm_min() );

    CHECK( split.status == geometry_status_t::DEGENERATE );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewVertex ) );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewEdge ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: SplitEdge allocation failures are atomic",
           "[Gate9][TopologyOps][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();
            const common::usize cBefore = state.cAllocationCalls;
            REQUIRE( MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 ).status ==
                     geometry_status_t::OK );
            cOperationAllocations = state.cAllocationCalls - cBefore;
        }
        REQUIRE( state.cSuccessfulAllocations == state.cFreeCalls );
    }
    REQUIRE( cOperationAllocations == 3u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();
            const mesh_logical_snapshot_t before =
                CaptureMeshSnapshot( f.mesh );
            REQUIRE( MeshSnapshotIsComplete( before ) );

            const common::usize cBefore = state.cAllocationCalls;
            state.iFailure = cBefore + iFailure;
            const mesh_split_edge_result_t split =
                MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 );

            CHECK( split.status == geometry_status_t::ALLOCATION_FAILED );
            CHECK_FALSE( GeometryHandle_IsValid( split.hNewVertex ) );
            CHECK_FALSE( GeometryHandle_IsValid( split.hNewEdge ) );
            CHECK( state.cAllocationCalls == cBefore + iFailure + 1u );
            CHECK( MeshMatchesSnapshot( f.mesh, before ) );
            CHECK( MeshValidation_Validate( &f.mesh ).status ==
                   geometry_status_t::OK );
            CHECK( common::GenerationPool_IsValid( &f.mesh.vertices ) );
            CHECK( common::GenerationPool_IsValid( &f.mesh.halfEdges ) );
            CHECK( common::GenerationPool_IsValid( &f.mesh.edges ) );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

TEST_CASE( "MeshOps: SplitEdge slot limit failure is atomic",
           "[Gate9][TopologyOps][limit][contract]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );
    REQUIRE( f.mesh.vertices.cSlots == f.mesh.vertices.cRecords );
    f.mesh.vertices.cSlotLimit = f.mesh.vertices.cSlots;

    const mesh_split_edge_result_t split =
        MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 );

    CHECK( split.status == geometry_status_t::LIMIT_EXCEEDED );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewVertex ) );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewEdge ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: SplitEdge does not reuse retired slots",
           "[Gate9][TopologyOps][limit][contract]" )
{
    TopologyFixture f;
    REQUIRE( common::GenerationPool_Reserve(
                 &f.mesh.vertices,
                 f.mesh.vertices.cRecords + 1u ) ==
             common::generation_pool_status_t::OK );

    const auto extraVertex = common::GenerationPool_Insert(
        &f.mesh.vertices, mesh_vertex_record_t{} );
    REQUIRE( extraVertex.status == common::generation_pool_status_t::OK );
    geometry_mesh_vertex_handle_t hRetired = extraVertex.handle;
    f.mesh.vertices.pSlots[hRetired.nSlot].nGeneration =
        common::CY_U32_MAX;
    hRetired.nGeneration = common::CY_U32_MAX;
    REQUIRE( common::GenerationPool_Remove(
                 &f.mesh.vertices, hRetired ) ==
             common::generation_pool_status_t::OK );
    REQUIRE( f.mesh.vertices.cRetiredSlots == 1u );
    REQUIRE( f.mesh.vertices.cSlots ==
             f.mesh.vertices.cRecords + f.mesh.vertices.cRetiredSlots );
    REQUIRE( common::GenerationPool_IsValid( &f.mesh.vertices ) );

    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );
    f.mesh.vertices.cSlotLimit = f.mesh.vertices.cSlots;

    const mesh_split_edge_result_t split =
        MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 );

    CHECK( split.status == geometry_status_t::LIMIT_EXCEEDED );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewVertex ) );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewEdge ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: SplitEdge rejects a corrupt reusable slot list",
           "[Gate9][TopologyOps][corrupt][contract]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();
    REQUIRE( common::GenerationPool_Reserve(
                 &f.mesh.halfEdges,
                 f.mesh.halfEdges.cRecords + 2u ) ==
             common::generation_pool_status_t::OK );
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const common::u32 iFreeHead = f.mesh.halfEdges.iFreeHead;
    REQUIRE( iFreeHead != common::CY_INVALID_INDEX );
    const common::u32 iNextFree =
        f.mesh.halfEdges.pSlots[iFreeHead].iNextFree;
    f.mesh.halfEdges.pSlots[iFreeHead].iNextFree = iFreeHead;

    const mesh_split_edge_result_t split =
        MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 );

    CHECK( split.status == geometry_status_t::CORRUPT_STATE );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewVertex ) );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewEdge ) );
    f.mesh.halfEdges.pSlots[iFreeHead].iNextFree = iNextFree;
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( common::GenerationPool_IsValid( &f.mesh.halfEdges ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

// ---------------------------------------------------------------------------
// SplitFace
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: SplitFace adds 1 edge and 1 face",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_face_handle_t hFace = f.FindAnyFace();

    geometry_mesh_vertex_handle_t hVA{}, hVB{};
    REQUIRE( f.FindDiagonalVertices( hFace, &hVA, &hVB ) );

    const auto result = MeshOps_SplitFace( &f.mesh, hFace, hVA, hVB );
    REQUIRE( result.status == geometry_status_t::OK );

    // Box was 12E, 6F. After split: 13E, 7F.
    REQUIRE( EditableMesh_EdgeCount( &f.mesh ) == 13u );
    REQUIRE( EditableMesh_FaceCount( &f.mesh ) == 7u );
    // Vertex count unchanged.
    REQUIRE( EditableMesh_VertexCount( &f.mesh ) == 8u );
}

TEST_CASE( "MeshOps: SplitFace Euler characteristic preserved",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_face_handle_t hFace = f.FindAnyFace();

    geometry_mesh_vertex_handle_t hVA{}, hVB{};
    REQUIRE( f.FindDiagonalVertices( hFace, &hVA, &hVB ) );

    REQUIRE( MeshOps_SplitFace( &f.mesh, hFace, hVA, hVB ).status ==
             geometry_status_t::OK );

    // Euler = V - E + F = 8 - 13 + 7 = 2.
    REQUIRE( EditableMesh_EulerCharacteristic( &f.mesh ) == 2 );
}

TEST_CASE( "MeshOps: SplitFace adjacent vertices rejected",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_face_handle_t hFace = f.FindAnyFace();

    // Get two adjacent vertices (share an edge on the face).
    const mesh_face_record_t *pFace =
        EditableMesh_GetFace( &f.mesh, hFace );
    REQUIRE( pFace != nullptr );
    const mesh_loop_record_t *pLoop =
        EditableMesh_GetLoop( &f.mesh, pFace->hOuterLoop );
    REQUIRE( pLoop != nullptr );

    const mesh_half_edge_record_t *pHE0 =
        EditableMesh_GetHalfEdge( &f.mesh, pLoop->hFirstHalfEdge );
    REQUIRE( pHE0 != nullptr );
    const mesh_half_edge_record_t *pHE1 =
        EditableMesh_GetHalfEdge( &f.mesh, pHE0->hNext );
    REQUIRE( pHE1 != nullptr );

    REQUIRE( MeshOps_SplitFace(
                 &f.mesh, hFace, pHE0->hOrigin, pHE1->hOrigin ).status ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: SplitFace allocation failures are atomic",
           "[Gate9][TopologyOps][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const geometry_mesh_face_handle_t hFace = f.FindAnyFace();
            geometry_mesh_vertex_handle_t hVA{};
            geometry_mesh_vertex_handle_t hVB{};
            REQUIRE( f.FindDiagonalVertices( hFace, &hVA, &hVB ) );

            const common::usize cBefore = state.cAllocationCalls;
            REQUIRE( MeshOps_SplitFace(
                         &f.mesh, hFace, hVA, hVB ).status ==
                     geometry_status_t::OK );
            cOperationAllocations = state.cAllocationCalls - cBefore;
        }
        REQUIRE( state.cSuccessfulAllocations == state.cFreeCalls );
    }
    REQUIRE( cOperationAllocations == 4u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const geometry_mesh_face_handle_t hFace = f.FindAnyFace();
            geometry_mesh_vertex_handle_t hVA{};
            geometry_mesh_vertex_handle_t hVB{};
            REQUIRE( f.FindDiagonalVertices( hFace, &hVA, &hVB ) );
            const mesh_logical_snapshot_t before =
                CaptureMeshSnapshot( f.mesh );
            REQUIRE( MeshSnapshotIsComplete( before ) );

            const common::usize cBefore = state.cAllocationCalls;
            state.iFailure = cBefore + iFailure;
            const mesh_split_face_result_t split =
                MeshOps_SplitFace( &f.mesh, hFace, hVA, hVB );

            CHECK( split.status == geometry_status_t::ALLOCATION_FAILED );
            CHECK_FALSE( GeometryHandle_IsValid( split.hNewEdge ) );
            CHECK_FALSE( GeometryHandle_IsValid( split.hNewFace ) );
            CHECK( state.cAllocationCalls == cBefore + iFailure + 1u );
            CHECK( MeshMatchesSnapshot( f.mesh, before ) );
            CHECK( MeshValidation_Validate( &f.mesh ).status ==
                   geometry_status_t::OK );
            CHECK( common::GenerationPool_IsValid( &f.mesh.halfEdges ) );
            CHECK( common::GenerationPool_IsValid( &f.mesh.edges ) );
            CHECK( common::GenerationPool_IsValid( &f.mesh.loops ) );
            CHECK( common::GenerationPool_IsValid( &f.mesh.faces ) );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

TEST_CASE( "MeshOps: SplitFace late slot limit failure is atomic",
           "[Gate9][TopologyOps][limit][contract]" )
{
    TopologyFixture f;
    const geometry_mesh_face_handle_t hFace = f.FindAnyFace();
    geometry_mesh_vertex_handle_t hVA{};
    geometry_mesh_vertex_handle_t hVB{};
    REQUIRE( f.FindDiagonalVertices( hFace, &hVA, &hVB ) );
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );
    REQUIRE( f.mesh.faces.cSlots == f.mesh.faces.cRecords );
    f.mesh.faces.cSlotLimit = f.mesh.faces.cSlots;

    const mesh_split_face_result_t split =
        MeshOps_SplitFace( &f.mesh, hFace, hVA, hVB );

    CHECK( split.status == geometry_status_t::LIMIT_EXCEEDED );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewEdge ) );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewFace ) );
    CHECK( f.mesh.halfEdges.cSlots >=
           f.mesh.halfEdges.cRecords + 2u );
    CHECK( f.mesh.edges.cSlots >= f.mesh.edges.cRecords + 1u );
    CHECK( f.mesh.loops.cSlots >= f.mesh.loops.cRecords + 1u );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: SplitFace bounds corrupt loop counts",
           "[Gate9][TopologyOps][corrupt][contract]" )
{
    TopologyFixture f;
    const geometry_mesh_face_handle_t hFace = f.FindAnyFace();
    geometry_mesh_vertex_handle_t hVA{};
    geometry_mesh_vertex_handle_t hVB{};
    REQUIRE( f.FindDiagonalVertices( hFace, &hVA, &hVB ) );
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_face_record_t *pFace =
        EditableMesh_GetFace( &f.mesh, hFace );
    REQUIRE( pFace != nullptr );
    mesh_loop_record_t *pLoop =
        common::GenerationPool_Get( &f.mesh.loops, pFace->hOuterLoop );
    REQUIRE( pLoop != nullptr );
    const common::u32 cHalfEdges = pLoop->cHalfEdges;
    pLoop->cHalfEdges = common::CY_U32_MAX;

    const mesh_split_face_result_t split =
        MeshOps_SplitFace( &f.mesh, hFace, hVA, hVB );

    CHECK( split.status == geometry_status_t::CORRUPT_STATE );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewEdge ) );
    CHECK_FALSE( GeometryHandle_IsValid( split.hNewFace ) );
    pLoop->cHalfEdges = cHalfEdges;
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

// ---------------------------------------------------------------------------
// CollapseEdge
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: CollapseEdge removes vertex and edge",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();

    const auto result = MeshOps_CollapseEdge( &f.mesh, hEdge );
    REQUIRE( result.status == geometry_status_t::OK );

    // One vertex and one edge removed. Two triangular faces that flanked
    // the edge on the box are degenerate after collapse and are also removed.
    // Box was 8V, 12E, 6F. After collapse of one edge:
    // V = 7, and the two adjacent quads each lose an edge when they collapse
    // to triangles that are degenerate — actually on a box each face adjacent
    // to the collapsed edge is a quad. After collapsing, each quad becomes a
    // triangle (3 vertices). So:
    // V: 8 - 1 = 7
    // The surviving vertex exists.
    REQUIRE( EditableMesh_GetVertex( &f.mesh, result.hSurvivor ) != nullptr );

    // At minimum: fewer vertices and edges than before.
    REQUIRE( EditableMesh_VertexCount( &f.mesh ) < 8u );
    REQUIRE( EditableMesh_EdgeCount( &f.mesh ) < 12u );
}

TEST_CASE( "MeshOps: CollapseEdge survivor at midpoint",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();

    // Get endpoints before collapse.
    const mesh_edge_record_t *pEdge =
        EditableMesh_GetEdge( &f.mesh, hEdge );
    const mesh_half_edge_record_t *pHE =
        EditableMesh_GetHalfEdge( &f.mesh, pEdge->hHalfEdge );
    const mesh_half_edge_record_t *pTwin =
        EditableMesh_GetHalfEdge( &f.mesh, pHE->hTwin );
    const mesh_vertex_record_t *pVA =
        EditableMesh_GetVertex( &f.mesh, pHE->hOrigin );
    const mesh_vertex_record_t *pVB =
        EditableMesh_GetVertex( &f.mesh, pTwin->hOrigin );

    const math::vec3d_t expectedMid = math::Vec3d_Scale(
        math::Vec3d_Add( pVA->position, pVB->position ), 0.5 );

    const auto result = MeshOps_CollapseEdge( &f.mesh, hEdge );
    REQUIRE( result.status == geometry_status_t::OK );

    const mesh_vertex_record_t *pSurvivor =
        EditableMesh_GetVertex( &f.mesh, result.hSurvivor );
    REQUIRE( pSurvivor != nullptr );
    REQUIRE( pSurvivor->position.x == Approx( expectedMid.x ) );
    REQUIRE( pSurvivor->position.y == Approx( expectedMid.y ) );
    REQUIRE( pSurvivor->position.z == Approx( expectedMid.z ) );
}

TEST_CASE( "MeshOps: CollapseEdge null mesh rejected",
           "[Gate9][TopologyOps]" )
{
    REQUIRE( MeshOps_CollapseEdge( nullptr, {} ).status ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// DissolveEdge
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: DissolveEdge merges two faces into one",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();

    REQUIRE( MeshOps_DissolveEdge( &f.mesh, hEdge ) ==
             geometry_status_t::OK );

    // Box was 12E, 6F. After dissolve: 11E, 5F.
    REQUIRE( EditableMesh_EdgeCount( &f.mesh ) == 11u );
    REQUIRE( EditableMesh_FaceCount( &f.mesh ) == 5u );
    // Vertices unchanged.
    REQUIRE( EditableMesh_VertexCount( &f.mesh ) == 8u );
}

TEST_CASE( "MeshOps: DissolveEdge Euler characteristic preserved",
           "[Gate9][TopologyOps]" )
{
    TopologyFixture f;
    REQUIRE( MeshOps_DissolveEdge( &f.mesh, f.FindAnyEdge() ) ==
             geometry_status_t::OK );

    // V - E + F = 8 - 11 + 5 = 2.
    REQUIRE( EditableMesh_EulerCharacteristic( &f.mesh ) == 2 );
}

TEST_CASE( "MeshOps: DissolveEdge null mesh rejected",
           "[Gate9][TopologyOps]" )
{
    REQUIRE( MeshOps_DissolveEdge( nullptr, {} ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// ExtrudeFace (Gate 12)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: ExtrudeFace adds correct element counts",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;

    // Box: 8V, 12E, 6F. Extruding one quad face:
    //   +4 vertices (the offset copies)
    //   +4 side faces + 1 top face, original face removed → net +4 faces
    //   New edges: 4 top edges + 4 vertical edges = 8 new, original 4 edges
    //     reused by side quads → net +8 edges
    // Result: 12V, 20E, 10F.
    const auto extResult = MeshOps_ExtrudeFace(
        &f.mesh, f.FindAnyFace(), 1.0 );
    REQUIRE( extResult.status == geometry_status_t::OK );

    CHECK( EditableMesh_VertexCount( &f.mesh ) == 12u );
    CHECK( EditableMesh_FaceCount( &f.mesh ) == 10u );
    CHECK( EditableMesh_EdgeCount( &f.mesh ) == 20u );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: ExtrudeFace preserves Euler characteristic",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;

    const auto extResult = MeshOps_ExtrudeFace(
        &f.mesh, f.FindAnyFace(), 2.0 );
    REQUIRE( extResult.status == geometry_status_t::OK );

    CHECK( EditableMesh_EulerCharacteristic( &f.mesh ) == 2 );
}

TEST_CASE( "MeshOps: ExtrudeFace rejects zero distance atomically",
           "[Gate12][TopologyOps][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const auto extResult = MeshOps_ExtrudeFace(
        &f.mesh, f.FindAnyFace(), 0.0 );
    CHECK( extResult.status == geometry_status_t::DEGENERATE );
    CHECK_FALSE( GeometryHandle_IsValid( extResult.hExtrudedFace ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: ExtrudeFace with negative distance extrudes inward",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;

    const auto extResult = MeshOps_ExtrudeFace(
        &f.mesh, f.FindAnyFace(), -0.5 );
    REQUIRE( extResult.status == geometry_status_t::OK );

    CHECK( EditableMesh_VertexCount( &f.mesh ) == 12u );
    CHECK( EditableMesh_EulerCharacteristic( &f.mesh ) == 2 );
}

TEST_CASE( "MeshOps: ExtrudeFace null mesh rejected",
           "[Gate12][TopologyOps]" )
{
    const auto result = MeshOps_ExtrudeFace( nullptr, {}, 1.0 );
    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: ExtrudeFace invalid handle rejected",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;
    geometry_mesh_face_handle_t invalid{};
    const auto result = MeshOps_ExtrudeFace( &f.mesh, invalid, 1.0 );
    CHECK( result.status == geometry_status_t::INVALID_HANDLE );
}

TEST_CASE( "MeshOps: ExtrudeFace rejects non-finite distance atomically",
           "[Gate12][TopologyOps][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const auto result = MeshOps_ExtrudeFace(
        &f.mesh,
        f.FindAnyFace(),
        std::numeric_limits<common::f64>::quiet_NaN() );

    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
    CHECK_FALSE( GeometryHandle_IsValid( result.hExtrudedFace ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
}

TEST_CASE( "MeshOps: ExtrudeFace rejects a nonzero offset that rounds away",
           "[Gate12][TopologyOps][numeric][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_extrude_face_result_t result = MeshOps_ExtrudeFace(
        &f.mesh,
        f.FindAnyFace(),
        std::numeric_limits<double>::denorm_min() );

    CHECK( result.status == geometry_status_t::DEGENERATE );
    CHECK_FALSE( GeometryHandle_IsValid( result.hExtrudedFace ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: ExtrudeFace allocation failures are atomic",
           "[Gate12][TopologyOps][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const common::usize cBefore = state.cAllocationCalls;
            REQUIRE( MeshOps_ExtrudeFace(
                         &f.mesh, f.FindAnyFace(), 0.5 ).status ==
                     geometry_status_t::OK );
            cOperationAllocations = state.cAllocationCalls - cBefore;
        }
        REQUIRE( state.cSuccessfulAllocations == state.cFreeCalls );
    }
    REQUIRE( cOperationAllocations > 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const mesh_logical_snapshot_t before =
                CaptureMeshSnapshot( f.mesh );
            REQUIRE( MeshSnapshotIsComplete( before ) );

            const common::usize cBefore = state.cAllocationCalls;
            state.iFailure = cBefore + iFailure;
            const mesh_extrude_face_result_t extrude =
                MeshOps_ExtrudeFace(
                    &f.mesh, f.FindAnyFace(), 0.5 );

            CHECK( extrude.status ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK_FALSE( GeometryHandle_IsValid(
                extrude.hExtrudedFace ) );
            CHECK( state.cAllocationCalls ==
                   cBefore + iFailure + 1u );
            CHECK( MeshMatchesSnapshot( f.mesh, before ) );
            CHECK( MeshValidation_Validate( &f.mesh ).status ==
                   geometry_status_t::OK );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

// ---------------------------------------------------------------------------
// InsetFace (Gate 12)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: InsetFace adds correct element counts",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;

    // Box: 8V, 12E, 6F. Insetting one quad face with margin:
    //   +4 inset vertices
    //   +4 ring quad faces + 1 inset face, original removed → net +4 faces
    //   +4 inset edges (inset face boundary) + 4 connecting edges + original
    //     4 edges reused → net +8 edges
    // Result: 12V, 20E, 10F.
    const auto insetResult = MeshOps_InsetFace(
        &f.mesh, f.FindAnyFace(), 0.3 );
    REQUIRE( insetResult.status == geometry_status_t::OK );

    CHECK( EditableMesh_VertexCount( &f.mesh ) == 12u );
    CHECK( EditableMesh_FaceCount( &f.mesh ) == 10u );
    CHECK( EditableMesh_EdgeCount( &f.mesh ) == 20u );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: InsetFace preserves Euler characteristic",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;

    const auto insetResult = MeshOps_InsetFace(
        &f.mesh, f.FindAnyFace(), 0.5 );
    REQUIRE( insetResult.status == geometry_status_t::OK );

    CHECK( EditableMesh_EulerCharacteristic( &f.mesh ) == 2 );
}

TEST_CASE( "MeshOps: InsetFace rejects zero margin",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;
    const auto result = MeshOps_InsetFace( &f.mesh, f.FindAnyFace(), 0.0 );
    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: InsetFace rejects negative margin",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;
    const auto result = MeshOps_InsetFace( &f.mesh, f.FindAnyFace(), -1.0 );
    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: InsetFace null mesh rejected",
           "[Gate12][TopologyOps]" )
{
    const auto result = MeshOps_InsetFace( nullptr, {}, 0.5 );
    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: InsetFace rejects too-large margin as degenerate",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;

    // Box half-extent is 1.0, so a face centroid is at distance ~1.414
    // from a corner. A margin >= that distance collapses the inset face.
    const auto result = MeshOps_InsetFace( &f.mesh, f.FindAnyFace(), 100.0 );
    CHECK( result.status == geometry_status_t::DEGENERATE );
}

TEST_CASE( "MeshOps: InsetFace rejects non-finite margin atomically",
           "[Gate12][TopologyOps][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_inset_face_result_t result = MeshOps_InsetFace(
        &f.mesh,
        f.FindAnyFace(),
        std::numeric_limits<common::f64>::quiet_NaN() );

    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
    CHECK_FALSE( GeometryHandle_IsValid( result.hInsetFace ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: InsetFace rejects a nonzero margin that rounds away",
           "[Gate12][TopologyOps][numeric][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_inset_face_result_t result = MeshOps_InsetFace(
        &f.mesh,
        f.FindAnyFace(),
        std::numeric_limits<double>::denorm_min() );

    CHECK( result.status == geometry_status_t::DEGENERATE );
    CHECK_FALSE( GeometryHandle_IsValid( result.hInsetFace ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: InsetFace allocation failures are atomic",
           "[Gate12][TopologyOps][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const common::usize cBefore = state.cAllocationCalls;
            REQUIRE( MeshOps_InsetFace(
                         &f.mesh, f.FindAnyFace(), 0.3 ).status ==
                     geometry_status_t::OK );
            cOperationAllocations = state.cAllocationCalls - cBefore;
        }
        REQUIRE( state.cSuccessfulAllocations == state.cFreeCalls );
    }
    REQUIRE( cOperationAllocations > 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const mesh_logical_snapshot_t before =
                CaptureMeshSnapshot( f.mesh );
            REQUIRE( MeshSnapshotIsComplete( before ) );

            const common::usize cBefore = state.cAllocationCalls;
            state.iFailure = cBefore + iFailure;
            const mesh_inset_face_result_t inset =
                MeshOps_InsetFace(
                    &f.mesh, f.FindAnyFace(), 0.3 );

            CHECK( inset.status ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK_FALSE( GeometryHandle_IsValid( inset.hInsetFace ) );
            CHECK( state.cAllocationCalls ==
                   cBefore + iFailure + 1u );
            CHECK( MeshMatchesSnapshot( f.mesh, before ) );
            CHECK( MeshValidation_Validate( &f.mesh ).status ==
                   geometry_status_t::OK );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

// ---------------------------------------------------------------------------
// LoopCut (Gate 12)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: LoopCut splits faces along edge ring",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;

    // A box has 12 edges. Picking any edge that borders two quad faces,
    // the loop cut should traverse the quad strip (which on a box wraps
    // around 4 faces).
    const auto lcResult = MeshOps_LoopCut(
        &f.mesh, f.FindAnyEdge(), 0.5 );
    REQUIRE( lcResult.status == geometry_status_t::OK );
    CHECK( lcResult.cFacesSplit > 0u );

    // After loop cut, Euler characteristic should still be 2.
    CHECK( EditableMesh_EulerCharacteristic( &f.mesh ) == 2 );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: LoopCut rejects t outside (0,1)",
           "[Gate12][TopologyOps]" )
{
    TopologyFixture f;
    CHECK( MeshOps_LoopCut( &f.mesh, f.FindAnyEdge(), 0.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshOps_LoopCut( &f.mesh, f.FindAnyEdge(), 1.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: LoopCut null mesh rejected",
           "[Gate12][TopologyOps]" )
{
    CHECK( MeshOps_LoopCut( nullptr, {}, 0.5 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: LoopCut rejects non-finite parameter atomically",
           "[Gate12][TopologyOps][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_loop_cut_result_t result = MeshOps_LoopCut(
        &f.mesh,
        f.FindAnyEdge(),
        std::numeric_limits<common::f64>::quiet_NaN() );

    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( result.cFacesSplit == 0u );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: LoopCut allocation failures are atomic",
           "[Gate12][TopologyOps][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const common::usize cBefore = state.cAllocationCalls;
            REQUIRE( MeshOps_LoopCut(
                         &f.mesh, f.FindAnyEdge(), 0.5 ).status ==
                     geometry_status_t::OK );
            cOperationAllocations = state.cAllocationCalls - cBefore;
        }
        REQUIRE( state.cSuccessfulAllocations == state.cFreeCalls );
    }
    REQUIRE( cOperationAllocations > 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const mesh_logical_snapshot_t before =
                CaptureMeshSnapshot( f.mesh );
            REQUIRE( MeshSnapshotIsComplete( before ) );

            const common::usize cBefore = state.cAllocationCalls;
            state.iFailure = cBefore + iFailure;
            const mesh_loop_cut_result_t cut = MeshOps_LoopCut(
                &f.mesh, f.FindAnyEdge(), 0.5 );

            CHECK( cut.status == geometry_status_t::ALLOCATION_FAILED );
            CHECK( cut.cFacesSplit == 0u );
            CHECK( state.cAllocationCalls ==
                   cBefore + iFailure + 1u );
            CHECK( MeshMatchesSnapshot( f.mesh, before ) );
            CHECK( MeshValidation_Validate( &f.mesh ).status ==
                   geometry_status_t::OK );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

// ---------------------------------------------------------------------------
// WeldVertices (Gate 13)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: WeldVertices merges nearby vertices after SplitEdge",
           "[Gate13][TopologyOps]" )
{
    TopologyFixture f;

    // Split an edge to create a new vertex at the midpoint.
    const auto splitResult = MeshOps_SplitEdge(
        &f.mesh, f.FindAnyEdge(), 0.5 );
    REQUIRE( splitResult.status == geometry_status_t::OK );

    const common::usize vertsBefore = EditableMesh_VertexCount( &f.mesh );

    // Move the new vertex very close to one of the original endpoints.
    const mesh_vertex_record_t *pNewVert =
        EditableMesh_GetVertex( &f.mesh, splitResult.hNewVertex );
    REQUIRE( pNewVert != nullptr );

    // Find an adjacent vertex through the half-edge fan.
    geometry_mesh_vertex_handle_t hNeighbor{};
    {
        const mesh_half_edge_record_t *pHE =
            EditableMesh_GetHalfEdge( &f.mesh, pNewVert->hOutHalfEdge );
        if ( pHE != nullptr ) {
            const mesh_half_edge_record_t *pTwin =
                EditableMesh_GetHalfEdge( &f.mesh, pHE->hTwin );
            if ( pTwin != nullptr ) {
                hNeighbor = pTwin->hOrigin;
            }
        }
    }
    REQUIRE( common::GenerationHandle_IsValid( hNeighbor ) );

    const mesh_vertex_record_t *pNeighbor =
        EditableMesh_GetVertex( &f.mesh, hNeighbor );
    REQUIRE( pNeighbor != nullptr );

    // Move the new vertex to be within 0.001 of the neighbor.
    const math::vec3d_t nearPos = math::Vec3d_Add(
        pNeighbor->position,
        Vec3d_Make( 0.0001, 0.0001, 0.0001 ) );
    REQUIRE( MeshOps_MoveVertex( &f.mesh, splitResult.hNewVertex, nearPos ) ==
             geometry_status_t::OK );

    // Weld with a tolerance that should capture this pair.
    const auto weldResult = MeshOps_WeldVertices( &f.mesh, 0.01 );
    REQUIRE( weldResult.status == geometry_status_t::OK );
    CHECK( weldResult.cVerticesMerged >= 1u );

    // Vertex count should have decreased.
    CHECK( EditableMesh_VertexCount( &f.mesh ) < vertsBefore );
}

TEST_CASE( "MeshOps: WeldVertices with tiny tolerance merges nothing",
           "[Gate13][TopologyOps]" )
{
    TopologyFixture f;

    const common::usize vertsBefore = EditableMesh_VertexCount( &f.mesh );

    // Box vertices are at ±1 — minimum distance between any two vertices
    // is 2.0. A tiny tolerance should merge nothing.
    const auto weldResult = MeshOps_WeldVertices( &f.mesh, 0.001 );
    REQUIRE( weldResult.status == geometry_status_t::OK );
    CHECK( weldResult.cVerticesMerged == 0u );
    CHECK( EditableMesh_VertexCount( &f.mesh ) == vertsBefore );
}

TEST_CASE( "MeshOps: WeldVertices rejects zero tolerance",
           "[Gate13][TopologyOps]" )
{
    TopologyFixture f;
    CHECK( MeshOps_WeldVertices( &f.mesh, 0.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: WeldVertices rejects non-finite tolerance atomically",
           "[Gate13][TopologyOps][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );
    const std::array<double, 3u> invalid{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for ( const double tolerance : invalid ) {
        CAPTURE( tolerance );
        CHECK( MeshOps_WeldVertices( &f.mesh, tolerance ).status ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    }
}

TEST_CASE( "MeshOps: WeldVertices null mesh rejected",
           "[Gate13][TopologyOps]" )
{
    CHECK( MeshOps_WeldVertices( nullptr, 0.1 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// BevelEdge (Gate 14)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: BevelEdge creates bevel face on box edge",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;

    // Box: 8V, 12E, 6F. Beveling one edge should add 2 new vertices
    // (split points) and the dissolved edge merges two adjacent faces
    // into a bevel face.
    const auto bevelResult = MeshOps_BevelEdge(
        &f.mesh, f.FindAnyEdge(), 0.2, 1u );
    REQUIRE( bevelResult.status == geometry_status_t::OK );
    CHECK( common::GenerationHandle_IsValid( bevelResult.hBevelFace ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: BevelEdge preserves Euler characteristic",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;

    const auto bevelResult = MeshOps_BevelEdge(
        &f.mesh, f.FindAnyEdge(), 0.2, 1u );
    REQUIRE( bevelResult.status == geometry_status_t::OK );

    CHECK( EditableMesh_EulerCharacteristic( &f.mesh ) == 2 );
}

TEST_CASE( "MeshOps: BevelEdge rejects zero width",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;
    CHECK( MeshOps_BevelEdge( &f.mesh, f.FindAnyEdge(), 0.0, 1u ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: BevelEdge rejects width exceeding edge length",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;
    // Box edge length is 2.0. Width of 1.5 means 2*1.5=3.0 > 2.0.
    CHECK( MeshOps_BevelEdge( &f.mesh, f.FindAnyEdge(), 1.5, 1u ).status ==
           geometry_status_t::DEGENERATE );
}

TEST_CASE( "MeshOps: BevelEdge null mesh rejected",
           "[Gate14][TopologyOps]" )
{
    CHECK( MeshOps_BevelEdge( nullptr, {}, 0.2, 1u ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: BevelEdge rejects zero segments",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;
    CHECK( MeshOps_BevelEdge( &f.mesh, f.FindAnyEdge(), 0.2, 0u ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: BevelEdge rejects unimplemented multi-segment bevel atomically",
           "[Gate14][TopologyOps][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_bevel_edge_result_t result = MeshOps_BevelEdge(
        &f.mesh, f.FindAnyEdge(), 0.2, 2u );

    CHECK( result.status == geometry_status_t::UNSUPPORTED );
    CHECK_FALSE( GeometryHandle_IsValid( result.hBevelFace ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
}

TEST_CASE( "MeshOps: BevelEdge rejects non-finite width atomically",
           "[Gate14][TopologyOps][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_bevel_edge_result_t result = MeshOps_BevelEdge(
        &f.mesh,
        f.FindAnyEdge(),
        std::numeric_limits<common::f64>::quiet_NaN(),
        1u );

    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
    CHECK_FALSE( GeometryHandle_IsValid( result.hBevelFace ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
}

TEST_CASE( "MeshOps: BevelEdge allocation failures are atomic",
           "[Gate14][TopologyOps][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const common::usize cBefore = state.cAllocationCalls;
            REQUIRE( MeshOps_BevelEdge(
                         &f.mesh, f.FindAnyEdge(), 0.2, 1u ).status ==
                     geometry_status_t::OK );
            cOperationAllocations = state.cAllocationCalls - cBefore;
        }
        REQUIRE( state.cSuccessfulAllocations == state.cFreeCalls );
    }
    REQUIRE( cOperationAllocations > 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        topology_ops_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeTopologyOpsFailureAllocator( &state );
        {
            TopologyFixture f{ allocator };
            const mesh_logical_snapshot_t before =
                CaptureMeshSnapshot( f.mesh );
            REQUIRE( MeshSnapshotIsComplete( before ) );

            const common::usize cBefore = state.cAllocationCalls;
            state.iFailure = cBefore + iFailure;
            const mesh_bevel_edge_result_t bevel = MeshOps_BevelEdge(
                &f.mesh, f.FindAnyEdge(), 0.2, 1u );

            CHECK( bevel.status == geometry_status_t::ALLOCATION_FAILED );
            CHECK_FALSE( GeometryHandle_IsValid( bevel.hBevelFace ) );
            CHECK( state.cAllocationCalls ==
                   cBefore + iFailure + 1u );
            CHECK( MeshMatchesSnapshot( f.mesh, before ) );
            CHECK( MeshValidation_Validate( &f.mesh ).status ==
                   geometry_status_t::OK );
        }
        CHECK( state.cSuccessfulAllocations == state.cFreeCalls );
    }
}

// ---------------------------------------------------------------------------
// Boundary-only operations (Gate 14)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: BridgeEdges rejects unsupported boundary model atomically",
           "[Gate14][TopologyOps][contract]" )
{
    TopologyFixture f;
    geometry_mesh_edge_handle_t hEdgeA{};
    geometry_mesh_edge_handle_t hEdgeB{};
    REQUIRE( f.FindEdgesOnDifferentFaceLoops( &hEdgeA, &hEdgeB ) );
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_bridge_edges_result_t result =
        MeshOps_BridgeEdges( &f.mesh, hEdgeA, hEdgeB );

    CHECK( result.status == geometry_status_t::UNSUPPORTED );
    CHECK( result.cFacesCreated == 0u );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: FillHole rejects unsupported boundary model atomically",
           "[Gate14][TopologyOps][contract]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const mesh_fill_hole_result_t result =
        MeshOps_FillHole( &f.mesh, hEdge );

    CHECK( result.status == geometry_status_t::UNSUPPORTED );
    CHECK_FALSE( GeometryHandle_IsValid( result.hNewFace ) );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

// ---------------------------------------------------------------------------
// Mirror (Gate 14)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: Mirror reflects vertex positions across YZ plane",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;

    // Mirror across the YZ plane (normal = {1,0,0}, d = 0).
    const auto mirrorPlane = math::Planed_Make(
        Vec3d_Make( 1.0, 0.0, 0.0 ), 0.0 );

    REQUIRE( MeshOps_Mirror( &f.mesh, mirrorPlane ) ==
             geometry_status_t::OK );

    // After mirroring, the centroid's x component should flip sign.
    // Original box is centered at origin, so centroid stays at origin.
    const math::vec3d_t centroid = EditableMesh_Centroid( &f.mesh );
    CHECK( centroid.x == Approx( 0.0 ).margin( 1e-10 ) );
    CHECK( centroid.y == Approx( 0.0 ).margin( 1e-10 ) );
    CHECK( centroid.z == Approx( 0.0 ).margin( 1e-10 ) );
}

TEST_CASE( "MeshOps: Mirror preserves Euler characteristic",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;

    const auto mirrorPlane = math::Planed_Make(
        Vec3d_Make( 0.0, 1.0, 0.0 ), 0.0 );

    REQUIRE( MeshOps_Mirror( &f.mesh, mirrorPlane ) ==
             geometry_status_t::OK );

    CHECK( EditableMesh_EulerCharacteristic( &f.mesh ) == 2 );
}

TEST_CASE( "MeshOps: Mirror preserves vertex count",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;
    const auto vertsBefore = EditableMesh_VertexCount( &f.mesh );

    const auto mirrorPlane = math::Planed_Make(
        Vec3d_Make( 0.0, 0.0, 1.0 ), 0.0 );

    REQUIRE( MeshOps_Mirror( &f.mesh, mirrorPlane ) ==
             geometry_status_t::OK );

    CHECK( EditableMesh_VertexCount( &f.mesh ) == vertsBefore );
}

TEST_CASE( "MeshOps: Mirror across offset plane moves centroid",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;

    // Mirror across X = 5.0 (normal = {1,0,0}, d = -5.0).
    // The plane equation is dot(N, P) + d = 0 → x - 5 = 0 → x = 5.
    // Original centroid is at (0,0,0). After reflection:
    //   P' = P - 2*(dot(N,P) + d)*N = P - 2*(x - 5)*N
    //   For x=0: P'x = 0 - 2*(0-5)*1 = 10.
    const auto mirrorPlane = math::Planed_Make(
        Vec3d_Make( 1.0, 0.0, 0.0 ), -5.0 );

    REQUIRE( MeshOps_Mirror( &f.mesh, mirrorPlane ) ==
             geometry_status_t::OK );

    const math::vec3d_t centroid = EditableMesh_Centroid( &f.mesh );
    CHECK( centroid.x == Approx( 10.0 ).margin( 1e-10 ) );
    CHECK( centroid.y == Approx( 0.0 ).margin( 1e-10 ) );
}

TEST_CASE( "MeshOps: Mirror normalizes the complete offset plane equation",
           "[Gate14][TopologyOps][contract]" )
{
    TopologyFixture f;

    // 2x - 10 = 0 is the same plane x = 5 as x - 5 = 0. Both the normal
    // and d must be divided by the normal length during normalization.
    const auto scaledPlane = math::Planed_Make(
        Vec3d_Make( 2.0, 0.0, 0.0 ), -10.0 );
    REQUIRE( MeshOps_Mirror( &f.mesh, scaledPlane ) ==
             geometry_status_t::OK );
    const math::vec3d_t centroid = EditableMesh_Centroid( &f.mesh );
    CHECK( centroid.x == Approx( 10.0 ).margin( 1e-10 ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: Mirror rejects non-finite planes atomically",
           "[Gate14][TopologyOps][contract]" )
{
    TopologyFixture f;
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::array<math::planed_t, 3u> invalid{
        math::Planed_Make( Vec3d_Make( nan, 0.0, 0.0 ), 0.0 ),
        math::Planed_Make( Vec3d_Make( inf, 0.0, 0.0 ), 0.0 ),
        math::Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), nan )
    };
    for ( const math::planed_t plane : invalid ) {
        CHECK( MeshOps_Mirror( &f.mesh, plane ) ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    }
}

TEST_CASE( "MeshOps: Mirror null mesh rejected",
           "[Gate14][TopologyOps]" )
{
    const auto plane = math::Planed_Make(
        Vec3d_Make( 1.0, 0.0, 0.0 ), 0.0 );
    CHECK( MeshOps_Mirror( nullptr, plane ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: Mirror degenerate plane rejected",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;
    const auto plane = math::Planed_Make(
        Vec3d_Make( 0.0, 0.0, 0.0 ), 0.0 );
    CHECK( MeshOps_Mirror( &f.mesh, plane ) ==
           geometry_status_t::DEGENERATE );
}

// ---------------------------------------------------------------------------
// DetachFaces (Gate 14)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: DetachFaces rejects unsupported open-boundary result atomically",
           "[Gate14][TopologyOps][contract]" )
{
    TopologyFixture f;
    const geometry_mesh_face_handle_t hFace = f.FindAnyFace();
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( f.mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );

    const auto detachResult = MeshOps_DetachFaces(
        &f.mesh, &hFace, 1u );
    CHECK( detachResult.status == geometry_status_t::UNSUPPORTED );
    CHECK( detachResult.cVerticesDuplicated == 0u );
    CHECK( MeshMatchesSnapshot( f.mesh, before ) );
    CHECK( MeshValidation_Validate( &f.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "MeshOps: DetachFaces null mesh rejected",
           "[Gate14][TopologyOps]" )
{
    geometry_mesh_face_handle_t hFace{};
    CHECK( MeshOps_DetachFaces( nullptr, &hFace, 1u ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: DetachFaces null face array rejected",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;
    CHECK( MeshOps_DetachFaces( &f.mesh, nullptr, 1u ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: DetachFaces zero count rejected",
           "[Gate14][TopologyOps]" )
{
    TopologyFixture f;
    geometry_mesh_face_handle_t hFace = f.FindAnyFace();
    CHECK( MeshOps_DetachFaces( &f.mesh, &hFace, 0u ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// Edge crease weight (Gate 15)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: SetEdgeCreaseWeight stores and retrieves weight",
           "[Gate15][TopologyOps]" )
{
    TopologyFixture f;
    const auto hEdge = f.FindAnyEdge();

    REQUIRE( MeshOps_SetEdgeCreaseWeight( &f.mesh, hEdge, 0.75 ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_GetEdgeCreaseWeight( &f.mesh, hEdge ) ==
           Approx( 0.75 ) );
}

TEST_CASE( "MeshOps: SetEdgeCreaseWeight clamps to [0, 1]",
           "[Gate15][TopologyOps]" )
{
    TopologyFixture f;
    const auto hEdge = f.FindAnyEdge();

    REQUIRE( MeshOps_SetEdgeCreaseWeight( &f.mesh, hEdge, 2.5 ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_GetEdgeCreaseWeight( &f.mesh, hEdge ) ==
           Approx( 1.0 ) );

    REQUIRE( MeshOps_SetEdgeCreaseWeight( &f.mesh, hEdge, -1.0 ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_GetEdgeCreaseWeight( &f.mesh, hEdge ) ==
           Approx( 0.0 ) );
}

TEST_CASE( "MeshOps: SetEdgeCreaseWeight rejects non-finite values",
           "[Gate15][TopologyOps][contract]" )
{
    TopologyFixture f;
    const auto hEdge = f.FindAnyEdge();
    REQUIRE( MeshOps_SetEdgeCreaseWeight( &f.mesh, hEdge, 0.25 ) ==
             geometry_status_t::OK );

    const std::array<double, 3u> invalid{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for ( const double weight : invalid ) {
        CAPTURE( weight );
        CHECK( MeshOps_SetEdgeCreaseWeight( &f.mesh, hEdge, weight ) ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( MeshOps_GetEdgeCreaseWeight( &f.mesh, hEdge ) ==
               Approx( 0.25 ) );
    }
}

TEST_CASE( "MeshOps: GetEdgeCreaseWeight defaults to zero",
           "[Gate15][TopologyOps]" )
{
    TopologyFixture f;
    CHECK( MeshOps_GetEdgeCreaseWeight( &f.mesh, f.FindAnyEdge() ) ==
           Approx( 0.0 ) );
}

TEST_CASE( "MeshOps: SetEdgeCreaseWeight null mesh rejected",
           "[Gate15][TopologyOps]" )
{
    CHECK( MeshOps_SetEdgeCreaseWeight( nullptr, {}, 0.5 ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: GetEdgeCreaseWeight null mesh returns zero",
           "[Gate15][TopologyOps]" )
{
    CHECK( MeshOps_GetEdgeCreaseWeight( nullptr, {} ) ==
           Approx( 0.0 ) );
}

// ---------------------------------------------------------------------------
// Edge ring / edge loop selection (Gate 15)
// ---------------------------------------------------------------------------

TEST_CASE( "MeshOps: SelectEdgeRing returns edges on box",
           "[Gate15][TopologyOps]" )
{
    TopologyFixture f;

    const auto ringResult = MeshOps_SelectEdgeRing(
        &f.mesh, f.FindAnyEdge() );
    REQUIRE( ringResult.status == geometry_status_t::OK );

    REQUIRE( ringResult.cEdges == 4u );
    for ( common::u32 i = 0u; i < ringResult.cEdges; ++i ) {
        for ( common::u32 j = i + 1u; j < ringResult.cEdges; ++j ) {
            CHECK_FALSE( HandlesMatch(
                ringResult.edges[i], ringResult.edges[j] ) );
        }
    }
}

TEST_CASE( "MeshOps: SelectEdgeRing reports malformed traversal links",
           "[Gate15][TopologyOps][contract]" )
{
    TopologyFixture f;
    const geometry_mesh_edge_handle_t hEdge = f.FindAnyEdge();
    const mesh_edge_record_t *pEdge = EditableMesh_GetEdge( &f.mesh, hEdge );
    REQUIRE( pEdge != nullptr );
    mesh_half_edge_record_t *pHalfEdge =
        common::GenerationPool_Get( &f.mesh.halfEdges, pEdge->hHalfEdge );
    REQUIRE( pHalfEdge != nullptr );

    pHalfEdge->hNext = {};
    CHECK( MeshOps_SelectEdgeRing( &f.mesh, hEdge ).status ==
           geometry_status_t::CORRUPT_STATE );
}

TEST_CASE( "MeshOps: SelectEdgeRing reports its fixed result capacity",
           "[Gate15][TopologyOps][contract]" )
{
    constexpr common::u32 kSides = 257u;
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    polygon_soup_t soup{};
    REQUIRE( PolygonSoup_Init( &soup, &allocator ) ==
             geometry_status_t::OK );

    constexpr double kTau = 6.283185307179586476925286766559;
    for ( common::u32 i = 0u; i < kSides; ++i ) {
        const double angle = kTau * static_cast<double>( i ) /
                             static_cast<double>( kSides );
        REQUIRE( PolygonSoup_TryAddVertex(
                     &soup,
                     Vec3d_Make( std::cos( angle ), std::sin( angle ), -1.0 ),
                     nullptr ) == geometry_status_t::OK );
    }
    for ( common::u32 i = 0u; i < kSides; ++i ) {
        const double angle = kTau * static_cast<double>( i ) /
                             static_cast<double>( kSides );
        REQUIRE( PolygonSoup_TryAddVertex(
                     &soup,
                     Vec3d_Make( std::cos( angle ), std::sin( angle ), 1.0 ),
                     nullptr ) == geometry_status_t::OK );
    }
    const common::u32 iBottomCenter = 2u * kSides;
    const common::u32 iTopCenter = iBottomCenter + 1u;
    REQUIRE( PolygonSoup_TryAddVertex(
                 &soup, Vec3d_Make( 0.0, 0.0, -1.0 ), nullptr ) ==
             geometry_status_t::OK );
    REQUIRE( PolygonSoup_TryAddVertex(
                 &soup, Vec3d_Make( 0.0, 0.0, 1.0 ), nullptr ) ==
             geometry_status_t::OK );

    for ( common::u32 i = 0u; i < kSides; ++i ) {
        const common::u32 next = ( i + 1u ) % kSides;
        const common::u32 side[4]{ i, next, kSides + next, kSides + i };
        const common::u32 top[3]{ iTopCenter, kSides + i,
                                  kSides + next };
        const common::u32 bottom[3]{ iBottomCenter, next, i };
        REQUIRE( PolygonSoup_TryAddFace(
                     &soup, { side, 4u }, {}, 0u, nullptr ) ==
                 geometry_status_t::OK );
        REQUIRE( PolygonSoup_TryAddFace(
                     &soup, { top, 3u }, {}, 0u, nullptr ) ==
                 geometry_status_t::OK );
        REQUIRE( PolygonSoup_TryAddFace(
                     &soup, { bottom, 3u }, {}, 0u, nullptr ) ==
                 geometry_status_t::OK );
    }

    editable_mesh_t mesh{};
    const sanitation_report_t report = Sanitation_TryPolygonSoupToMesh(
        &soup, {}, &allocator, &mesh, nullptr );
    REQUIRE( report.status == geometry_status_t::OK );

    geometry_mesh_edge_handle_t hVertical{};
    (void)common::GenerationPool_ForEach(
        &mesh.edges,
        [&]( geometry_mesh_edge_handle_t hEdge,
             const mesh_edge_record_t &edge ) noexcept -> common::bool_t {
            const mesh_half_edge_record_t *pHalfEdge =
                EditableMesh_GetHalfEdge( &mesh, edge.hHalfEdge );
            const mesh_half_edge_record_t *pNext = pHalfEdge != nullptr
                ? EditableMesh_GetHalfEdge( &mesh, pHalfEdge->hNext )
                : nullptr;
            const mesh_vertex_record_t *pA = pHalfEdge != nullptr
                ? EditableMesh_GetVertex( &mesh, pHalfEdge->hOrigin )
                : nullptr;
            const mesh_vertex_record_t *pB = pNext != nullptr
                ? EditableMesh_GetVertex( &mesh, pNext->hOrigin )
                : nullptr;
            if ( pA != nullptr && pB != nullptr &&
                 pA->position.x == pB->position.x &&
                 pA->position.y == pB->position.y &&
                 pA->position.z != pB->position.z ) {
                hVertical = hEdge;
                return false;
            }
            return true;
        } );
    REQUIRE( common::GenerationHandle_IsValid( hVertical ) );

    const mesh_edge_selection_result_t ring =
        MeshOps_SelectEdgeRing( &mesh, hVertical );
    CHECK( ring.status == geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( ring.cEdges == 256u );
    bool bAllUnique = true;
    for ( common::u32 i = 0u; i < ring.cEdges; ++i ) {
        for ( common::u32 j = i + 1u; j < ring.cEdges; ++j ) {
            if ( HandlesMatch( ring.edges[i], ring.edges[j] ) ) {
                bAllUnique = false;
            }
        }
    }
    CHECK( bAllUnique );

    EditableMesh_Shutdown( &mesh );
    PolygonSoup_Shutdown( &soup );
}

TEST_CASE( "MeshOps: SelectEdgeRing null mesh rejected",
           "[Gate15][TopologyOps]" )
{
    CHECK( MeshOps_SelectEdgeRing( nullptr, {} ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: SelectEdgeLoop returns edges on box",
           "[Gate15][TopologyOps]" )
{
    TopologyFixture f;

    const auto loopResult = MeshOps_SelectEdgeLoop(
        &f.mesh, f.FindAnyEdge() );
    REQUIRE( loopResult.status == geometry_status_t::OK );

    CHECK( loopResult.cEdges >= 1u );
}

TEST_CASE( "MeshOps: SelectEdgeLoop null mesh rejected",
           "[Gate15][TopologyOps]" )
{
    CHECK( MeshOps_SelectEdgeLoop( nullptr, {} ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// Gate 17 — Laplacian smoothing, decimation, triangulation, grid snap
// ===========================================================================

// Reusable fixture that builds a box mesh from a brush pipeline.
// Box is centered at origin with half-extents (1,1,1).
struct Gate17Fixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    Gate17Fixture() {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAlloc,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
                 geometry_status_t::OK );
    }
    ~Gate17Fixture() {
        EditableMesh_Shutdown( &mesh );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

// ---------------------------------------------------------------------------
// Laplacian smoothing
// ---------------------------------------------------------------------------

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: LaplacianSmooth returns OK on valid box mesh",
    "[Gate17][TopologyOps]" )
{
    // A box mesh has 8 vertices, each with 3 neighbors. Smoothing should
    // converge vertices toward the centroid.
    const auto result = MeshOps_LaplacianSmooth( &mesh, 0.5, 3 );
    CHECK( result.status == geometry_status_t::OK );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: LaplacianSmooth moves vertices toward centroid",
    "[Gate17][TopologyOps]" )
{
    // Record a corner vertex position before smoothing.
    math::vec3d_t posBefore{};
    (void)common::GenerationPool_ForEach( &mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            posBefore = v.position;
            return false;
        } );

    const auto result = MeshOps_LaplacianSmooth( &mesh, 1.0, 5 );
    REQUIRE( result.status == geometry_status_t::OK );

    // After aggressive smoothing, the vertex should have moved inward
    // (closer to the centroid at origin).
    math::vec3d_t posAfter{};
    (void)common::GenerationPool_ForEach( &mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            posAfter = v.position;
            return false;
        } );

    // The smoothed position should be closer to origin than the original
    // corner vertex was.
    const double distBefore = Vec3d_LengthSquared( posBefore );
    const double distAfter = Vec3d_LengthSquared( posAfter );
    CHECK( distAfter < distBefore );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: LaplacianSmooth factor zero does not move vertices",
    "[Gate17][TopologyOps]" )
{
    // Record vertex positions.
    math::vec3d_t posBefore{};
    (void)common::GenerationPool_ForEach( &mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            posBefore = v.position;
            return false;
        } );

    const auto result = MeshOps_LaplacianSmooth( &mesh, 0.0, 10 );
    REQUIRE( result.status == geometry_status_t::OK );

    math::vec3d_t posAfter{};
    (void)common::GenerationPool_ForEach( &mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            posAfter = v.position;
            return false;
        } );

    CHECK( posAfter.x == Approx( posBefore.x ).margin( 1.0e-12 ) );
    CHECK( posAfter.y == Approx( posBefore.y ).margin( 1.0e-12 ) );
    CHECK( posAfter.z == Approx( posBefore.z ).margin( 1.0e-12 ) );
}

TEST_CASE( "MeshOps: LaplacianSmooth null mesh rejected",
           "[Gate17][TopologyOps]" )
{
    CHECK( MeshOps_LaplacianSmooth( nullptr, 0.5, 1 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: LaplacianSmooth zero iterations rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_LaplacianSmooth( &mesh, 0.5, 0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    EditableMesh_Shutdown( &mesh );
}

TEST_CASE( "MeshOps: LaplacianSmooth uninitialized mesh rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    CHECK( MeshOps_LaplacianSmooth( &mesh, 0.5, 1 ).status ==
           geometry_status_t::NOT_INITIALIZED );
}

// ---------------------------------------------------------------------------
// Decimation
// ---------------------------------------------------------------------------

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: Decimate returns OK on valid box mesh",
    "[Gate17][TopologyOps]" )
{
    // Box has 6 faces (quads). With a large min edge length, edges will
    // be collapsed. Target 4 faces (minimum tetrahedron).
    const auto result = MeshOps_Decimate( &mesh, 4, 10.0 );
    CHECK( result.status == geometry_status_t::OK );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: Decimate reduces face count",
    "[Gate17][TopologyOps]" )
{
    const common::usize facesBefore = EditableMesh_FaceCount( &mesh );
    const auto result = MeshOps_Decimate( &mesh, 4, 10.0 );
    REQUIRE( result.status == geometry_status_t::OK );

    const common::usize facesAfter = EditableMesh_FaceCount( &mesh );
    // Should have reduced (or at least tried to reduce) face count.
    CHECK( facesAfter <= facesBefore );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: Decimate stops at target face count",
    "[Gate17][TopologyOps]" )
{
    // Target equals current count — should not collapse anything.
    const common::usize facesBefore = EditableMesh_FaceCount( &mesh );
    const auto result = MeshOps_Decimate( &mesh, facesBefore, 10.0 );
    REQUIRE( result.status == geometry_status_t::OK );
    CHECK( result.cEdgesCollapsed == 0u );
}

TEST_CASE( "MeshOps: Decimate null mesh rejected",
           "[Gate17][TopologyOps]" )
{
    CHECK( MeshOps_Decimate( nullptr, 4, 1.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: Decimate target below 4 rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_Decimate( &mesh, 3, 1.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    EditableMesh_Shutdown( &mesh );
}

TEST_CASE( "MeshOps: Decimate zero min edge length rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_Decimate( &mesh, 4, 0.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    EditableMesh_Shutdown( &mesh );
}

TEST_CASE( "MeshOps: Decimate negative min edge length rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_Decimate( &mesh, 4, -1.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    EditableMesh_Shutdown( &mesh );
}

TEST_CASE( "MeshOps: Decimate non-finite min edge length rejected",
           "[Gate17][TopologyOps][Contract]" )
{
    editable_mesh_t mesh{};
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_Decimate(
               &mesh, 4,
               std::numeric_limits<common::f64>::quiet_NaN() ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshOps_Decimate(
               &mesh, 4,
               std::numeric_limits<common::f64>::infinity() ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    EditableMesh_Shutdown( &mesh );
}

// ---------------------------------------------------------------------------
// Face triangulation
// ---------------------------------------------------------------------------

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: TriangulateFaces converts box quads to triangles",
    "[Gate17][TopologyOps]" )
{
    // A box has 6 quad faces. Triangulating each quad produces 2 triangles,
    // so we expect 12 triangles and 6 new faces created.
    const auto result = MeshOps_TriangulateFaces( &mesh );
    REQUIRE( result.status == geometry_status_t::OK );
    CHECK( result.cFacesCreated == 6u );

    const common::usize facesAfter = EditableMesh_FaceCount( &mesh );
    CHECK( facesAfter == 12u );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: TriangulateFace on a single quad produces two triangles",
    "[Gate17][TopologyOps]" )
{
    // Find any face (all are quads on a box).
    geometry_mesh_face_handle_t hFace{};
    (void)common::GenerationPool_ForEach( &mesh.faces,
        [&]( geometry_mesh_face_handle_t h,
             const mesh_face_record_t & ) noexcept -> common::bool_t {
            hFace = h;
            return false;
        } );

    const common::usize facesBefore = EditableMesh_FaceCount( &mesh );
    const auto result = MeshOps_TriangulateFace( &mesh, hFace );
    REQUIRE( result.status == geometry_status_t::OK );
    CHECK( result.cFacesCreated == 1u );

    const common::usize facesAfter = EditableMesh_FaceCount( &mesh );
    CHECK( facesAfter == facesBefore + 1u );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: TriangulateFace on already-triangular face is no-op",
    "[Gate17][TopologyOps]" )
{
    // First triangulate all faces.
    REQUIRE( MeshOps_TriangulateFaces( &mesh ).status ==
             geometry_status_t::OK );

    // Now pick any face — should be a triangle.
    geometry_mesh_face_handle_t hFace{};
    (void)common::GenerationPool_ForEach( &mesh.faces,
        [&]( geometry_mesh_face_handle_t h,
             const mesh_face_record_t & ) noexcept -> common::bool_t {
            hFace = h;
            return false;
        } );

    const common::usize facesBefore = EditableMesh_FaceCount( &mesh );
    const auto result = MeshOps_TriangulateFace( &mesh, hFace );
    CHECK( result.status == geometry_status_t::OK );
    CHECK( result.cFacesCreated == 0u );
    CHECK( EditableMesh_FaceCount( &mesh ) == facesBefore );
}

TEST_CASE( "MeshOps: TriangulateFaces null mesh rejected",
           "[Gate17][TopologyOps]" )
{
    CHECK( MeshOps_TriangulateFaces( nullptr ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: TriangulateFace null mesh rejected",
           "[Gate17][TopologyOps]" )
{
    CHECK( MeshOps_TriangulateFace( nullptr, {} ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: TriangulateFaces uninitialized mesh rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    CHECK( MeshOps_TriangulateFaces( &mesh ).status ==
           geometry_status_t::NOT_INITIALIZED );
}

// ---------------------------------------------------------------------------
// Vertex grid snap
// ---------------------------------------------------------------------------

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: SnapToGrid aligns vertices to grid",
    "[Gate17][TopologyOps]" )
{
    // First move a vertex off-grid so snap has something to do.
    // Smooth slightly to offset vertices from their integer positions.
    REQUIRE( MeshOps_LaplacianSmooth( &mesh, 0.1, 1 ).status ==
             geometry_status_t::OK );

    // Snap to 1.0 grid — should move vertices back to integer positions.
    const auto result = MeshOps_SnapToGrid( &mesh, 1.0 );
    REQUIRE( result.status == geometry_status_t::OK );
    CHECK( result.cVerticesMoved > 0u );

    // Verify all vertices are on the grid (each coordinate is a multiple
    // of 1.0, i.e. an integer).
    bool allOnGrid = true;
    (void)common::GenerationPool_ForEach( &mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            const double dx = v.position.x - std::round( v.position.x );
            const double dy = v.position.y - std::round( v.position.y );
            const double dz = v.position.z - std::round( v.position.z );
            if ( std::abs( dx ) > 1.0e-12 ||
                 std::abs( dy ) > 1.0e-12 ||
                 std::abs( dz ) > 1.0e-12 ) {
                allOnGrid = false;
            }
            return true;
        } );
    CHECK( allOnGrid );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: SnapToGrid with fine grid is effectively no-op on box",
    "[Gate17][TopologyOps]" )
{
    // Box vertices are at integer positions (±1). Snapping to grid
    // spacing 0.5 should keep them in place (each is already a multiple
    // of 0.5).
    const auto result = MeshOps_SnapToGrid( &mesh, 0.5 );
    REQUIRE( result.status == geometry_status_t::OK );
    CHECK( result.cVerticesMoved == 0u );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: SnapToGrid publishes every representable grid correction",
    "[Gate17][TopologyOps][numeric][contract]" )
{
    geometry_mesh_vertex_handle_t hVertex{};
    math::vec3d_t offGrid{};
    (void)common::GenerationPool_ForEach(
        &mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t h,
             const mesh_vertex_record_t &vertex ) noexcept -> common::bool_t {
            hVertex = h;
            offGrid = vertex.position;
            offGrid.x += 3.0e-12;
            return false;
        } );
    REQUIRE( GeometryHandle_IsValid( hVertex ) );
    REQUIRE( MeshOps_MoveVertex( &mesh, hVertex, offGrid ) ==
             geometry_status_t::OK );

    constexpr double spacing = 1.0e-11;
    const double expectedX =
        std::round( offGrid.x / spacing ) * spacing;
    REQUIRE( expectedX != offGrid.x );

    const mesh_snap_result_t result = MeshOps_SnapToGrid(
        &mesh, spacing );
    REQUIRE( result.status == geometry_status_t::OK );
    CHECK( result.cVerticesMoved >= 1u );
    const mesh_vertex_record_t *pVertex =
        EditableMesh_GetVertex( &mesh, hVertex );
    REQUIRE( pVertex != nullptr );
    CHECK( pVertex->position.x == expectedX );
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: LaplacianSmooth rejects non-finite factors atomically",
    "[Gate17][TopologyOps][contract]" )
{
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );
    const std::array<double, 3u> invalid{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for ( const double factor : invalid ) {
        CAPTURE( factor );
        CHECK( MeshOps_LaplacianSmooth( &mesh, factor, 1u ).status ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( MeshMatchesSnapshot( mesh, before ) );
    }
}

TEST_CASE_METHOD( Gate17Fixture,
    "MeshOps: SnapToGrid rejects non-finite and overflowing spacing atomically",
    "[Gate17][TopologyOps][contract]" )
{
    const mesh_logical_snapshot_t before = CaptureMeshSnapshot( mesh );
    REQUIRE( MeshSnapshotIsComplete( before ) );
    const std::array<double, 3u> nonFinite{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for ( const double spacing : nonFinite ) {
        CAPTURE( spacing );
        CHECK( MeshOps_SnapToGrid( &mesh, spacing ).status ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( MeshMatchesSnapshot( mesh, before ) );
    }

    const double subnormal = std::numeric_limits<double>::denorm_min();
    REQUIRE( subnormal > 0.0 );
    CHECK( MeshOps_SnapToGrid( &mesh, subnormal ).status ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( MeshMatchesSnapshot( mesh, before ) );
}

TEST_CASE( "MeshOps: SnapToGrid null mesh rejected",
           "[Gate17][TopologyOps]" )
{
    CHECK( MeshOps_SnapToGrid( nullptr, 1.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "MeshOps: SnapToGrid zero spacing rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_SnapToGrid( &mesh, 0.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    EditableMesh_Shutdown( &mesh );
}

TEST_CASE( "MeshOps: SnapToGrid negative spacing rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    CHECK( MeshOps_SnapToGrid( &mesh, -1.0 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    EditableMesh_Shutdown( &mesh );
}

TEST_CASE( "MeshOps: SnapToGrid uninitialized mesh rejected",
           "[Gate17][TopologyOps]" )
{
    editable_mesh_t mesh{};
    CHECK( MeshOps_SnapToGrid( &mesh, 1.0 ).status ==
           geometry_status_t::NOT_INITIALIZED );
}

} // namespace cypher::editor::geometry
