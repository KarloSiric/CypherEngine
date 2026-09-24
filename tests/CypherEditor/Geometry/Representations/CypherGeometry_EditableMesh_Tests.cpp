//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_EditableMesh_Tests.cpp
//  Purpose: Gate 8 contract tests for the half-edge mesh topology layer.
//  Details: Verifies the closing criterion: a canonical box built from a
//           six-plane brush boundary produces a half-edge mesh with 8V,
//           12E, 24HE, 6L, 6F, 1 shell, Euler characteristic 2, outward
//           winding, positive volume, and reciprocal valid adjacency.
//           Stale handles are rejected after shutdown and reinit.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using Catch::Approx;

namespace {

// Builds a standard unit box (half-extents 1,1,1 at the origin) and its
// boundary, then constructs a half-edge mesh from it. Nearly every Gate 8
// test starts from this state.
struct MeshFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    MeshFixture() {
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
    ~MeshFixture() {
        EditableMesh_Shutdown( &mesh );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

// Helper: collects all vertex handles from a mesh into a vector.
// Uses the pool's ForEach with a noexcept bool_t-returning lambda.
struct vertex_collector_t {
    geometry_mesh_vertex_handle_t handles[256];
    common::u32 count{ 0u };
};

void CollectVertexHandles(
    const editable_mesh_t *pMesh,
    vertex_collector_t *pOut ) noexcept
{
    pOut->count = 0u;
    (void)common::GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hV,
             const mesh_vertex_record_t & /*v*/ ) noexcept -> common::bool_t {
            if ( pOut->count < 256u ) {
                pOut->handles[pOut->count++] = hV;
            }
            return true;
        } );
}

// Helper: collects half-edge handles.
struct half_edge_collector_t {
    geometry_mesh_half_edge_handle_t handles[512];
    common::u32 count{ 0u };
};

void CollectHalfEdgeHandles(
    const editable_mesh_t *pMesh,
    half_edge_collector_t *pOut ) noexcept
{
    pOut->count = 0u;
    (void)common::GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t hHe,
             const mesh_half_edge_record_t & /*he*/ ) noexcept -> common::bool_t {
            if ( pOut->count < 512u ) {
                pOut->handles[pOut->count++] = hHe;
            }
            return true;
        } );
}

// Helper: collects loop handles.
struct loop_collector_t {
    geometry_mesh_loop_handle_t handles[64];
    common::u32 count{ 0u };
};

void CollectLoopHandles(
    const editable_mesh_t *pMesh,
    loop_collector_t *pOut ) noexcept
{
    pOut->count = 0u;
    (void)common::GenerationPool_ForEach( &pMesh->loops,
        [&]( geometry_mesh_loop_handle_t hL,
             const mesh_loop_record_t & /*l*/ ) noexcept -> common::bool_t {
            if ( pOut->count < 64u ) {
                pOut->handles[pOut->count++] = hL;
            }
            return true;
        } );
}

// Helper: collects face handles.
struct face_collector_t {
    geometry_mesh_face_handle_t handles[64];
    common::u32 count{ 0u };
};

void CollectFaceHandles(
    const editable_mesh_t *pMesh,
    face_collector_t *pOut ) noexcept
{
    pOut->count = 0u;
    (void)common::GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hF,
             const mesh_face_record_t & /*f*/ ) noexcept -> common::bool_t {
            if ( pOut->count < 64u ) {
                pOut->handles[pOut->count++] = hF;
            }
            return true;
        } );
}

struct mesh_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ 0u };
};

void *MeshFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<mesh_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) {
        return nullptr;
    }

    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    return pSystem->pfnAllocate(
        pSystem->pUserData, cbSize, nAlignment );
}

void MeshFailureFree(
    void * /*pUserData*/,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree(
        pSystem->pUserData, pMemory, cbSize, nAlignment );
}

common::allocator_t MakeMeshFailureAllocator(
    mesh_failure_allocator_state_t *pState ) noexcept
{
    return {
        &MeshFailureAllocate,
        nullptr,
        &MeshFailureFree,
        pState
    };
}

bool MeshHasNoRecords( const editable_mesh_t &mesh ) noexcept
{
    return EditableMesh_VertexCount( &mesh ) == 0u &&
           EditableMesh_HalfEdgeCount( &mesh ) == 0u &&
           EditableMesh_EdgeCount( &mesh ) == 0u &&
           EditableMesh_LoopCount( &mesh ) == 0u &&
           EditableMesh_FaceCount( &mesh ) == 0u &&
           EditableMesh_ShellCount( &mesh ) == 0u;
}

} // namespace

// ===========================================================================
// Gate 8 closing criterion: canonical box element counts
// ===========================================================================

TEST_CASE( "Gate8: box mesh has 8 vertices", "[mesh][gate8]" ) {
    MeshFixture fix;
    REQUIRE( EditableMesh_VertexCount( &fix.mesh ) == 8u );
}

TEST_CASE( "Gate8: box mesh has 12 edges", "[mesh][gate8]" ) {
    MeshFixture fix;
    REQUIRE( EditableMesh_EdgeCount( &fix.mesh ) == 12u );
}

TEST_CASE( "Gate8: box mesh has 24 half-edges", "[mesh][gate8]" ) {
    MeshFixture fix;
    REQUIRE( EditableMesh_HalfEdgeCount( &fix.mesh ) == 24u );
}

TEST_CASE( "Gate8: box mesh has 6 loops", "[mesh][gate8]" ) {
    MeshFixture fix;
    REQUIRE( EditableMesh_LoopCount( &fix.mesh ) == 6u );
}

TEST_CASE( "Gate8: box mesh has 6 faces", "[mesh][gate8]" ) {
    MeshFixture fix;
    REQUIRE( EditableMesh_FaceCount( &fix.mesh ) == 6u );
}

TEST_CASE( "Gate8: box mesh has 1 shell", "[mesh][gate8]" ) {
    MeshFixture fix;
    REQUIRE( EditableMesh_ShellCount( &fix.mesh ) == 1u );
}

// ===========================================================================
// Euler characteristic
// ===========================================================================

TEST_CASE( "Gate8: box Euler characteristic is 2", "[mesh][gate8]" ) {
    MeshFixture fix;
    // V - E + F = 8 - 12 + 6 = 2
    REQUIRE( EditableMesh_EulerCharacteristic( &fix.mesh ) == 2 );
}

// ===========================================================================
// Signed volume — unit box has volume 8 (half-extents 1 → side 2)
// ===========================================================================

TEST_CASE( "Gate8: box signed volume is positive", "[mesh][gate8]" ) {
    MeshFixture fix;
    const common::f64 vol = EditableMesh_SignedVolume( &fix.mesh );
    REQUIRE( vol > 0.0 );
    REQUIRE( vol == Approx( 8.0 ).margin( 1.0e-10 ) );
}

// ===========================================================================
// Full structural validation
// ===========================================================================

TEST_CASE( "Gate8: box passes all mesh validation checks", "[mesh][gate8]" ) {
    MeshFixture fix;
    const mesh_validation_result_t result =
        MeshValidation_Validate( &fix.mesh );

    INFO( "status = " << EditableMesh_StatusName( result.status ) );
    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.bReciprocalTwins );
    REQUIRE( result.bClosedLoops );
    REQUIRE( result.bAllVerticesReferenced );
    REQUIRE( result.bEulerValid );
    REQUIRE( result.bConsistentWinding );
    REQUIRE( result.bPositiveVolume );
}

// ===========================================================================
// Vertex valence — every vertex of a box is shared by exactly 3 faces
// ===========================================================================

TEST_CASE( "Gate8: every box vertex has valence 3", "[mesh][gate8]" ) {
    MeshFixture fix;

    vertex_collector_t verts;
    CollectVertexHandles( &fix.mesh, &verts );
    REQUIRE( verts.count == 8u );

    for ( common::u32 i = 0u; i < verts.count; ++i ) {
        REQUIRE( EditableMesh_VertexValence( &fix.mesh, verts.handles[i] )
                 == 3u );
    }
}

// ===========================================================================
// Half-edge adjacency — twins are reciprocal, loops close
// ===========================================================================

TEST_CASE( "Gate8: every half-edge twin is reciprocal", "[mesh][gate8]" ) {
    MeshFixture fix;

    half_edge_collector_t hes;
    CollectHalfEdgeHandles( &fix.mesh, &hes );
    REQUIRE( hes.count == 24u );

    for ( common::u32 i = 0u; i < hes.count; ++i ) {
        const mesh_half_edge_record_t *pHe =
            EditableMesh_GetHalfEdge( &fix.mesh, hes.handles[i] );
        REQUIRE( pHe != nullptr );

        // Twin must exist.
        const mesh_half_edge_record_t *pTwin =
            common::GenerationPool_Get( &fix.mesh.halfEdges, pHe->hTwin );
        REQUIRE( pTwin != nullptr );

        // Twin's twin must point back.
        REQUIRE( pTwin->hTwin.nSlot == hes.handles[i].nSlot );
        REQUIRE( pTwin->hTwin.nGeneration == hes.handles[i].nGeneration );
    }
}

TEST_CASE( "Gate8: every face loop is closed with correct count",
           "[mesh][gate8]" ) {
    MeshFixture fix;

    loop_collector_t loops;
    CollectLoopHandles( &fix.mesh, &loops );
    REQUIRE( loops.count == 6u );

    for ( common::u32 iLoop = 0u; iLoop < loops.count; ++iLoop ) {
        const mesh_loop_record_t *pLoop =
            EditableMesh_GetLoop( &fix.mesh, loops.handles[iLoop] );
        REQUIRE( pLoop != nullptr );

        // Box faces are quads: 4 half-edges each.
        REQUIRE( pLoop->cHalfEdges == 4u );

        // Walk next pointers and verify we return to the start.
        const mesh_half_edge_record_t *pFirst =
            common::GenerationPool_Get( &fix.mesh.halfEdges,
                                        pLoop->hFirstHalfEdge );
        REQUIRE( pFirst != nullptr );

        common::u32 count = 1u;
        geometry_mesh_half_edge_handle_t hCurr = pFirst->hNext;
        while ( count < pLoop->cHalfEdges ) {
            const mesh_half_edge_record_t *pCurr =
                common::GenerationPool_Get( &fix.mesh.halfEdges, hCurr );
            REQUIRE( pCurr != nullptr );
            hCurr = pCurr->hNext;
            ++count;
        }

        // After cHalfEdges steps we must be back at the start.
        REQUIRE( hCurr.nSlot == pLoop->hFirstHalfEdge.nSlot );
        REQUIRE( hCurr.nGeneration ==
                 pLoop->hFirstHalfEdge.nGeneration );
    }
}

// ===========================================================================
// Face normals — each face normal should point away from the origin
// ===========================================================================

TEST_CASE( "Gate8: every face normal points outward from origin",
           "[mesh][gate8]" ) {
    MeshFixture fix;

    face_collector_t faces;
    CollectFaceHandles( &fix.mesh, &faces );
    REQUIRE( faces.count == 6u );

    for ( common::u32 iFace = 0u; iFace < faces.count; ++iFace ) {
        const mesh_face_record_t *pFace =
            EditableMesh_GetFace( &fix.mesh, faces.handles[iFace] );
        REQUIRE( pFace != nullptr );

        const mesh_loop_record_t *pLoop =
            common::GenerationPool_Get( &fix.mesh.loops,
                                        pFace->hOuterLoop );
        REQUIRE( pLoop != nullptr );

        // Compute centroid by averaging loop vertex positions.
        math::vec3d_t centroid = Vec3d_Make( 0.0, 0.0, 0.0 );
        common::u32 vertCount = 0u;
        geometry_mesh_half_edge_handle_t hHe = pLoop->hFirstHalfEdge;
        for ( common::u32 iV = 0u; iV < pLoop->cHalfEdges; ++iV ) {
            const mesh_half_edge_record_t *pHe =
                common::GenerationPool_Get( &fix.mesh.halfEdges, hHe );
            REQUIRE( pHe != nullptr );
            const mesh_vertex_record_t *pV =
                common::GenerationPool_Get( &fix.mesh.vertices,
                                            pHe->hOrigin );
            REQUIRE( pV != nullptr );
            centroid = math::Vec3d_Add( centroid, pV->position );
            hHe = pHe->hNext;
            ++vertCount;
        }
        REQUIRE( vertCount == pLoop->cHalfEdges );
        centroid = math::Vec3d_Scale( centroid,
                                      1.0 / static_cast<common::f64>(
                                          vertCount ) );

        // The centroid should lie on the positive side of the normal.
        const common::f64 dot =
            math::Vec3d_Dot( pFace->normal, centroid );
        REQUIRE( dot > 0.0 );
    }
}

// ===========================================================================
// Shell coverage — every face belongs to the one shell
// ===========================================================================

TEST_CASE( "Gate8: every face belongs to the single shell",
           "[mesh][gate8]" ) {
    MeshFixture fix;

    face_collector_t faces;
    CollectFaceHandles( &fix.mesh, &faces );
    REQUIRE( faces.count == 6u );

    // All faces must reference the same shell.
    const mesh_face_record_t *pFirst =
        EditableMesh_GetFace( &fix.mesh, faces.handles[0] );
    REQUIRE( pFirst != nullptr );
    const geometry_mesh_shell_handle_t hShell = pFirst->hShell;

    for ( common::u32 i = 1u; i < faces.count; ++i ) {
        const mesh_face_record_t *pFace =
            EditableMesh_GetFace( &fix.mesh, faces.handles[i] );
        REQUIRE( pFace != nullptr );
        REQUIRE( pFace->hShell.nSlot == hShell.nSlot );
        REQUIRE( pFace->hShell.nGeneration == hShell.nGeneration );
    }

    const mesh_shell_record_t *pShell =
        common::GenerationPool_Get( &fix.mesh.shells, hShell );
    REQUIRE( pShell != nullptr );
    REQUIRE( pShell->cFaces == 6u );
}

// ===========================================================================
// Source side traceability — every face records a valid source side
// ===========================================================================

TEST_CASE( "Gate8: every face records a valid source side index",
           "[mesh][gate8]" ) {
    MeshFixture fix;

    face_collector_t faces;
    CollectFaceHandles( &fix.mesh, &faces );
    REQUIRE( faces.count == 6u );

    for ( common::u32 i = 0u; i < faces.count; ++i ) {
        const mesh_face_record_t *pFace =
            EditableMesh_GetFace( &fix.mesh, faces.handles[i] );
        REQUIRE( pFace != nullptr );
        // Box has 6 sides, indices 0..5.
        REQUIRE( pFace->iSourceSide != CY_INVALID_INDEX );
        REQUIRE( pFace->iSourceSide < 6u );
    }
}

// ===========================================================================
// Stale handle rejection — handles from a previous mesh generation fail
// ===========================================================================

TEST_CASE( "Gate8: stale vertex handle is rejected after shutdown",
           "[mesh][gate8]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) ==
             geometry_status_t::OK );
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
             geometry_status_t::OK );

    // Capture a handle from the first generation.
    vertex_collector_t verts;
    CollectVertexHandles( &mesh, &verts );
    REQUIRE( verts.count > 0u );
    const geometry_mesh_vertex_handle_t hStale = verts.handles[0];
    REQUIRE( common::GenerationHandle_IsValid( hStale ) );

    // Shutdown and reinit — old handles must be stale.
    EditableMesh_Shutdown( &mesh );
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );

    // The stale handle should resolve to nullptr.
    REQUIRE( EditableMesh_GetVertex( &mesh, hStale ) == nullptr );

    EditableMesh_Shutdown( &mesh );
    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

// ===========================================================================
// Lifecycle edge cases
// ===========================================================================

TEST_CASE( "Gate8: building into uninitialized mesh returns error",
           "[mesh][gate8]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) ==
             geometry_status_t::OK );

    // Mesh not initialized — builder must reject.
    REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
             geometry_status_t::NOT_INITIALIZED );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Gate8: validation on uninitialized mesh returns error",
           "[mesh][gate8]" ) {
    editable_mesh_t mesh{};
    const mesh_validation_result_t result =
        MeshValidation_Validate( &mesh );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Gate8: null pointer arguments are rejected", "[mesh][gate8]" ) {
    REQUIRE( EditableMesh_Init( nullptr, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( MeshBuilder_TryBuildFromBoundary( nullptr, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );

    const mesh_validation_result_t result =
        MeshValidation_Validate( nullptr );
    REQUIRE( result.status == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Gate8: builder rejects a non-empty destination without mutation",
           "[mesh][gate8][contract]" ) {
    MeshFixture fix;

    const common::usize cVertices = EditableMesh_VertexCount( &fix.mesh );
    const common::usize cHalfEdges =
        EditableMesh_HalfEdgeCount( &fix.mesh );
    const common::usize cEdges = EditableMesh_EdgeCount( &fix.mesh );
    const common::usize cFaces = EditableMesh_FaceCount( &fix.mesh );

    REQUIRE( MeshBuilder_TryBuildFromBoundary(
                 &fix.mesh, &fix.boundary ) ==
             geometry_status_t::INVALID_ARGUMENT );
    CHECK( EditableMesh_VertexCount( &fix.mesh ) == cVertices );
    CHECK( EditableMesh_HalfEdgeCount( &fix.mesh ) == cHalfEdges );
    CHECK( EditableMesh_EdgeCount( &fix.mesh ) == cEdges );
    CHECK( EditableMesh_FaceCount( &fix.mesh ) == cFaces );
    CHECK( MeshValidation_Validate( &fix.mesh ).status ==
           geometry_status_t::OK );
}

TEST_CASE( "Gate8: builder rejects corrupt boundary ranges before mutation",
           "[mesh][gate8][contract]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) ==
             geometry_status_t::OK );
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );

    boundary.faces.pData[0].iFirstIndex = common::CY_U32_MAX;
    CHECK( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
           geometry_status_t::CORRUPT_STATE );
    CHECK( MeshHasNoRecords( mesh ) );

    EditableMesh_Shutdown( &mesh );
    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "Gate8: every builder allocation failure leaves an empty mesh",
           "[mesh][gate8][allocation][contract]" ) {
    common::allocator_t sourceAllocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &sourceAllocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &boundary, &sourceAllocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) ==
             geometry_status_t::OK );

    bool bObservedFailure = false;
    bool bReachedSuccess = false;
    for ( common::usize iFail = 1u; iFail <= 32u; ++iFail ) {
        mesh_failure_allocator_state_t state{};
        state.iFailOnCall = iFail;
        common::allocator_t failingAllocator =
            MakeMeshFailureAllocator( &state );
        editable_mesh_t mesh{};
        REQUIRE( EditableMesh_Init( &mesh, &failingAllocator ) ==
                 geometry_status_t::OK );

        const geometry_status_t status =
            MeshBuilder_TryBuildFromBoundary( &mesh, &boundary );
        if ( status == geometry_status_t::ALLOCATION_FAILED ) {
            bObservedFailure = true;
            CHECK( MeshHasNoRecords( mesh ) );
        } else {
            REQUIRE( status == geometry_status_t::OK );
            CHECK( MeshValidation_Validate( &mesh ).status ==
                   geometry_status_t::OK );
            bReachedSuccess = true;
        }

        EditableMesh_Shutdown( &mesh );
        if ( bReachedSuccess ) {
            break;
        }
    }

    CHECK( bObservedFailure );
    CHECK( bReachedSuccess );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

// ===========================================================================
// Non-box solid — wedge (5 faces) passes validation too
// ===========================================================================

TEST_CASE( "Gate8: wedge mesh passes structural validation",
           "[mesh][gate8]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    REQUIRE( BrushGenerator_TryMakeWedge(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ),
                 0u, 2u ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) ==
             geometry_status_t::OK );
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
             geometry_status_t::OK );

    // Wedge: 6 vertices, 9 edges, 5 faces → Euler = 6-9+5 = 2.
    REQUIRE( EditableMesh_VertexCount( &mesh ) == 6u );
    REQUIRE( EditableMesh_EdgeCount( &mesh ) == 9u );
    REQUIRE( EditableMesh_FaceCount( &mesh ) == 5u );
    REQUIRE( EditableMesh_EulerCharacteristic( &mesh ) == 2 );

    const mesh_validation_result_t result =
        MeshValidation_Validate( &mesh );
    INFO( "status = " << EditableMesh_StatusName( result.status ) );
    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.bReciprocalTwins );
    REQUIRE( result.bConsistentWinding );
    REQUIRE( result.bPositiveVolume );

    EditableMesh_Shutdown( &mesh );
    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

// ---------------------------------------------------------------------------
// Mesh analysis queries (Gate 13)
// ---------------------------------------------------------------------------

TEST_CASE( "EditableMesh: SurfaceArea of unit box is 24",
           "[Gate13][MeshAnalysis]" )
{
    MeshFixture f;

    // Unit box has half-extents 1 → side lengths 2 → 6 faces of 2×2 = 4
    // each → total surface area = 24.
    const common::f64 area = EditableMesh_SurfaceArea( &f.mesh );
    CHECK( area == Approx( 24.0 ).margin( 1.0e-9 ) );
}

TEST_CASE( "EditableMesh: SurfaceArea null returns zero",
           "[Gate13][MeshAnalysis]" )
{
    CHECK( EditableMesh_SurfaceArea( nullptr ) == 0.0 );
}

TEST_CASE( "EditableMesh: Centroid of unit box is at origin",
           "[Gate13][MeshAnalysis]" )
{
    MeshFixture f;

    const math::vec3d_t c = EditableMesh_Centroid( &f.mesh );
    CHECK( c.x == Approx( 0.0 ).margin( 1.0e-9 ) );
    CHECK( c.y == Approx( 0.0 ).margin( 1.0e-9 ) );
    CHECK( c.z == Approx( 0.0 ).margin( 1.0e-9 ) );
}

TEST_CASE( "EditableMesh: BoundingSphere of unit box",
           "[Gate13][MeshAnalysis]" )
{
    MeshFixture f;

    mesh_bounding_sphere_t sphere{};
    REQUIRE( EditableMesh_BoundingSphere( &f.mesh, &sphere ) );

    // Center at origin, radius = sqrt(3) ≈ 1.732 (corner distance).
    CHECK( sphere.center.x == Approx( 0.0 ).margin( 1.0e-9 ) );
    CHECK( sphere.center.y == Approx( 0.0 ).margin( 1.0e-9 ) );
    CHECK( sphere.center.z == Approx( 0.0 ).margin( 1.0e-9 ) );
    CHECK( sphere.radius == Approx( std::sqrt( 3.0 ) ).margin( 1.0e-9 ) );
}

TEST_CASE( "EditableMesh: BoundingSphere null returns false",
           "[Gate13][MeshAnalysis]" )
{
    mesh_bounding_sphere_t sphere{};
    CHECK_FALSE( EditableMesh_BoundingSphere( nullptr, &sphere ) );
}

TEST_CASE( "EditableMesh: BoundingBox of unit box",
           "[Gate13][MeshAnalysis]" )
{
    MeshFixture f;

    math::vec3d_t bMin{}, bMax{};
    REQUIRE( EditableMesh_BoundingBox( &f.mesh, &bMin, &bMax ) );

    CHECK( bMin.x == Approx( -1.0 ).margin( 1.0e-9 ) );
    CHECK( bMin.y == Approx( -1.0 ).margin( 1.0e-9 ) );
    CHECK( bMin.z == Approx( -1.0 ).margin( 1.0e-9 ) );
    CHECK( bMax.x == Approx(  1.0 ).margin( 1.0e-9 ) );
    CHECK( bMax.y == Approx(  1.0 ).margin( 1.0e-9 ) );
    CHECK( bMax.z == Approx(  1.0 ).margin( 1.0e-9 ) );
}

TEST_CASE( "EditableMesh: BoundingBox null returns false",
           "[Gate13][MeshAnalysis]" )
{
    math::vec3d_t bMin{}, bMax{};
    CHECK_FALSE( EditableMesh_BoundingBox( nullptr, &bMin, &bMax ) );
}

} // namespace cypher::editor::geometry
