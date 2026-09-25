//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshInvariant_Tests.cpp
//  Purpose: Post-condition sweep for every mutating editable-mesh operation.
//  Details: The per-operation tests check element counts, which a mesh with
//           broken twins or open loops can still satisfy. This file runs
//           each operation on a canonical closed box and then demands full
//           structural validity (reciprocal twins, closed loops, Euler
//           characteristic, winding, positive volume). It exists to catch
//           adjacency corruption that count-only assertions miss.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshTopologyOps.h"
#include "CypherGeometry_MeshSubdivision.h"
#include "CypherGeometry_MeshBooleans.h"
#include "CypherGeometry_MeshCleanup.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;

namespace {

// Builds a closed box mesh through the same brush -> boundary -> mesh path
// the editor uses, so the sweep starts from production-shaped topology.
struct InvariantBox {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    editable_mesh_t mesh{};

    explicit InvariantBox( math::vec3d_t center = Vec3d_Make( 0.0, 0.0, 0.0 ),
                           math::vec3d_t half = Vec3d_Make( 1.0, 1.0, 1.0 ) ) {
        geometry_policy_t policy{};
        geometry_source_id_allocator_t idAlloc{};
        brush_solid_t brush{};
        brush_boundary_t boundary{};
        REQUIRE( BrushGenerator_TryMakeBox( &brush, &allocator, policy,
                                            &idAlloc, center, half ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
                 geometry_status_t::OK );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
    ~InvariantBox() { EditableMesh_Shutdown( &mesh ); }

    geometry_mesh_edge_handle_t FirstEdge() const {
        geometry_mesh_edge_handle_t h{};
        (void)common::GenerationPool_ForEach( &mesh.edges,
            [&]( geometry_mesh_edge_handle_t e,
                 const mesh_edge_record_t & ) noexcept -> common::bool_t {
                h = e;
                return false;
            } );
        return h;
    }

    geometry_mesh_face_handle_t FirstFace() const {
        geometry_mesh_face_handle_t h{};
        (void)common::GenerationPool_ForEach( &mesh.faces,
            [&]( geometry_mesh_face_handle_t f,
                 const mesh_face_record_t & ) noexcept -> common::bool_t {
                h = f;
                return false;
            } );
        return h;
    }

    // Returns the origin vertices of loop corners 0 and 2 of a face, the
    // canonical non-adjacent pair for SplitFace on a quad.
    void FaceDiagonal( geometry_mesh_face_handle_t hFace,
                       geometry_mesh_vertex_handle_t *pA,
                       geometry_mesh_vertex_handle_t *pB ) const {
        const mesh_face_record_t *pFace = EditableMesh_GetFace( &mesh, hFace );
        REQUIRE( pFace != nullptr );
        const mesh_loop_record_t *pLoop =
            EditableMesh_GetLoop( &mesh, pFace->hOuterLoop );
        REQUIRE( pLoop != nullptr );
        const mesh_half_edge_record_t *pHE0 =
            EditableMesh_GetHalfEdge( &mesh, pLoop->hFirstHalfEdge );
        const mesh_half_edge_record_t *pHE1 =
            EditableMesh_GetHalfEdge( &mesh, pHE0->hNext );
        const mesh_half_edge_record_t *pHE2 =
            EditableMesh_GetHalfEdge( &mesh, pHE1->hNext );
        *pA = pHE0->hOrigin;
        *pB = pHE2->hOrigin;
    }
};

// Every flag is checked separately so a failure names the broken invariant.
void RequireClosedValid( const editable_mesh_t *pMesh, const char *pLabel ) {
    const mesh_validation_result_t v = MeshValidation_Validate( pMesh );
    INFO( "operation: " << pLabel
          << " euler=" << v.nEulerCharacteristic
          << " volume=" << v.fSignedVolume );
    CHECK( v.bReciprocalTwins );
    CHECK( v.bClosedLoops );
    CHECK( v.bAllVerticesReferenced );
    CHECK( v.bEdgeLinks );
    CHECK( v.bEulerValid );
    CHECK( v.bConsistentWinding );
    CHECK( v.bPositiveVolume );
}

template <typename record_t, typename tag_t>
bool HasReusedLiveSlot(
    const common::generation_pool_t<record_t, tag_t> *pPool ) noexcept
{
    bool bFound = false;
    (void)common::GenerationPool_ForEach(
        pPool,
        [&]( common::generation_handle_t<tag_t> hRecord,
             const record_t & ) noexcept -> common::bool_t {
            bFound = hRecord.nGeneration > 1u;
            return !bFound;
        } );
    return bFound;
}

bool EveryTopologyHandleResolves( const editable_mesh_t *pMesh ) noexcept
{
    bool bValid = true;
    (void)common::GenerationPool_ForEach(
        &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vertex ) noexcept -> common::bool_t {
            bValid = !common::GenerationHandle_IsValid( vertex.hOutHalfEdge ) ||
                     EditableMesh_GetHalfEdge(
                         pMesh, vertex.hOutHalfEdge ) != nullptr;
            return bValid;
        } );
    if ( !bValid ) { return false; }

    (void)common::GenerationPool_ForEach(
        &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t,
             const mesh_half_edge_record_t &halfEdge ) noexcept -> common::bool_t {
            bValid = EditableMesh_GetVertex(
                         pMesh, halfEdge.hOrigin ) != nullptr &&
                     EditableMesh_GetHalfEdge(
                         pMesh, halfEdge.hTwin ) != nullptr &&
                     EditableMesh_GetHalfEdge(
                         pMesh, halfEdge.hNext ) != nullptr &&
                     EditableMesh_GetHalfEdge(
                         pMesh, halfEdge.hPrev ) != nullptr &&
                     EditableMesh_GetEdge(
                         pMesh, halfEdge.hEdge ) != nullptr &&
                     EditableMesh_GetLoop(
                         pMesh, halfEdge.hLoop ) != nullptr;
            return bValid;
        } );
    if ( !bValid ) { return false; }

    (void)common::GenerationPool_ForEach(
        &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t,
             const mesh_edge_record_t &edge ) noexcept -> common::bool_t {
            bValid = EditableMesh_GetHalfEdge(
                         pMesh, edge.hHalfEdge ) != nullptr;
            return bValid;
        } );
    if ( !bValid ) { return false; }

    (void)common::GenerationPool_ForEach(
        &pMesh->loops,
        [&]( geometry_mesh_loop_handle_t,
             const mesh_loop_record_t &loop ) noexcept -> common::bool_t {
            bValid = EditableMesh_GetHalfEdge(
                         pMesh, loop.hFirstHalfEdge ) != nullptr &&
                     EditableMesh_GetFace( pMesh, loop.hFace ) != nullptr;
            return bValid;
        } );
    if ( !bValid ) { return false; }

    (void)common::GenerationPool_ForEach(
        &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> common::bool_t {
            bValid = EditableMesh_GetLoop(
                         pMesh, face.hOuterLoop ) != nullptr &&
                     EditableMesh_GetShell( pMesh, face.hShell ) != nullptr;
            return bValid;
        } );
    if ( !bValid ) { return false; }

    (void)common::GenerationPool_ForEach(
        &pMesh->shells,
        [&]( geometry_mesh_shell_handle_t,
             const mesh_shell_record_t &shell ) noexcept -> common::bool_t {
            bValid = EditableMesh_GetFace(
                         pMesh, shell.hAnyFace ) != nullptr;
            return bValid;
        } );
    return bValid;
}

} // namespace

TEST_CASE( "MeshInvariant: canonical box is valid", "[MeshInvariant]" )
{
    InvariantBox box;
    RequireClosedValid( &box.mesh, "baseline" );
}

TEST_CASE( "MeshInvariant: SplitEdge", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_SplitEdge( &box.mesh, box.FirstEdge(), 0.5 ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "SplitEdge" );
}

TEST_CASE( "MeshInvariant: SplitFace", "[MeshInvariant]" )
{
    InvariantBox box;
    const auto hFace = box.FirstFace();
    geometry_mesh_vertex_handle_t a{}, b{};
    box.FaceDiagonal( hFace, &a, &b );
    REQUIRE( MeshOps_SplitFace( &box.mesh, hFace, a, b ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "SplitFace" );
}

TEST_CASE( "MeshInvariant: CollapseEdge", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_CollapseEdge( &box.mesh, box.FirstEdge() ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "CollapseEdge" );
}

TEST_CASE( "MeshInvariant: DissolveEdge", "[MeshInvariant]" )
{
    InvariantBox box;
    // A box edge joins two perpendicular faces; dissolving one leaves a
    // non-planar hexagon, which is still a valid closed topology.
    REQUIRE( MeshOps_DissolveEdge( &box.mesh, box.FirstEdge() ) ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "DissolveEdge" );
}

TEST_CASE( "MeshInvariant: ExtrudeFace", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_ExtrudeFace( &box.mesh, box.FirstFace(), 0.5 ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "ExtrudeFace" );
}

TEST_CASE( "MeshInvariant: InsetFace", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_InsetFace( &box.mesh, box.FirstFace(), 0.25 ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "InsetFace" );
}

TEST_CASE( "MeshInvariant: LoopCut", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_LoopCut( &box.mesh, box.FirstEdge(), 0.5 ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "LoopCut" );
}

TEST_CASE( "MeshInvariant: BevelEdge single segment", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_BevelEdge( &box.mesh, box.FirstEdge(), 0.2, 1u ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "BevelEdge x1" );
}

TEST_CASE( "MeshInvariant: BevelEdge reports unsupported multi-segment requests without mutation", "[MeshInvariant]" )
{
    InvariantBox box;
    const common::usize cVertices = EditableMesh_VertexCount( &box.mesh );
    const common::usize cEdges = EditableMesh_EdgeCount( &box.mesh );
    const common::usize cFaces = EditableMesh_FaceCount( &box.mesh );
    REQUIRE( MeshOps_BevelEdge( &box.mesh, box.FirstEdge(), 0.2, 3u ).status ==
             geometry_status_t::UNSUPPORTED );
    CHECK( EditableMesh_VertexCount( &box.mesh ) == cVertices );
    CHECK( EditableMesh_EdgeCount( &box.mesh ) == cEdges );
    CHECK( EditableMesh_FaceCount( &box.mesh ) == cFaces );
    RequireClosedValid( &box.mesh, "BevelEdge unsupported x3" );
}

TEST_CASE( "MeshInvariant: Mirror", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_Mirror( &box.mesh, math::CY_PLANED_X ) ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "Mirror" );
}

TEST_CASE( "MeshInvariant: TriangulateFaces on quads", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_TriangulateFaces( &box.mesh ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "TriangulateFaces quads" );
}

TEST_CASE( "MeshInvariant: TriangulateFaces on pentagons", "[MeshInvariant]" )
{
    // Splitting a box edge turns both adjacent quads into pentagons, which
    // exercises the multi-step fan path (n - 3 > 1 splits per face).
    InvariantBox box;
    REQUIRE( MeshOps_SplitEdge( &box.mesh, box.FirstEdge(), 0.5 ).status ==
             geometry_status_t::OK );
    REQUIRE( MeshOps_TriangulateFaces( &box.mesh ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "TriangulateFaces pentagons" );

    bool allTriangles = true;
    (void)common::GenerationPool_ForEach( &box.mesh.loops,
        [&]( geometry_mesh_loop_handle_t,
             const mesh_loop_record_t &loop ) noexcept -> common::bool_t {
            if ( loop.cHalfEdges != 3u ) { allTriangles = false; }
            return true;
        } );
    CHECK( allTriangles );
}

TEST_CASE( "MeshInvariant: LaplacianSmooth", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_LaplacianSmooth( &box.mesh, 0.5, 2u ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "LaplacianSmooth" );
}

TEST_CASE( "MeshInvariant: Decimate", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_TriangulateFaces( &box.mesh ).status ==
             geometry_status_t::OK );
    REQUIRE( MeshOps_Decimate( &box.mesh, 4u, 10.0 ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "Decimate" );
}

TEST_CASE( "MeshInvariant: SnapToGrid", "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshOps_SnapToGrid( &box.mesh, 0.5 ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "SnapToGrid" );
}

TEST_CASE( "MeshInvariant: CatmullClark output", "[MeshInvariant]" )
{
    InvariantBox box;
    editable_mesh_t out{};
    REQUIRE( EditableMesh_Init( &out, &box.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSubdivision_TryCatmullClark( &box.mesh, &out ) ==
             geometry_status_t::OK );
    RequireClosedValid( &out, "CatmullClark" );
    EditableMesh_Shutdown( &out );
}

TEST_CASE( "MeshInvariant: Linear subdivision output", "[MeshInvariant]" )
{
    InvariantBox box;
    editable_mesh_t out{};
    REQUIRE( EditableMesh_Init( &out, &box.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSubdivision_TryLinear( &box.mesh, &out ) ==
             geometry_status_t::OK );
    RequireClosedValid( &out, "Linear subdivision" );
    EditableMesh_Shutdown( &out );
}

TEST_CASE( "MeshInvariant: Boolean intersect output", "[MeshInvariant]" )
{
    InvariantBox a;
    InvariantBox b( Vec3d_Make( 1.0, 0.0, 0.0 ) );
    editable_mesh_t out{};
    REQUIRE( MeshBool_TryIntersect( &a.mesh, &b.mesh, &a.allocator, 1.0e-6,
                                    &out ) == geometry_status_t::OK );
    RequireClosedValid( &out, "MeshBool intersect" );
    EditableMesh_Shutdown( &out );
}

TEST_CASE( "MeshInvariant: Clone after deletions preserves topology",
           "[MeshInvariant]" )
{
    // Collapse leaves holes and bumped generations in the source pools.
    // A clone that re-inserts records sequentially would give the copies
    // different slot/generation values than the handles stored inside
    // them, so every adjacency pointer would dangle.
    InvariantBox box;
    REQUIRE( MeshOps_SplitEdge( &box.mesh, box.FirstEdge(), 0.5 ).status ==
             geometry_status_t::OK );
    REQUIRE( MeshOps_CollapseEdge( &box.mesh, box.FirstEdge() ).status ==
             geometry_status_t::OK );
    // Reinsert after deletion so at least one live record carries a bumped
    // generation. This covers both sparse source slots and actual slot reuse.
    REQUIRE( MeshOps_SplitEdge( &box.mesh, box.FirstEdge(), 0.5 ).status ==
             geometry_status_t::OK );
    REQUIRE( ( HasReusedLiveSlot( &box.mesh.vertices ) ||
               HasReusedLiveSlot( &box.mesh.halfEdges ) ||
               HasReusedLiveSlot( &box.mesh.edges ) ) );
    RequireClosedValid( &box.mesh, "source before clone" );

    editable_mesh_t clone{};
    REQUIRE( EditableMesh_TryClone( &box.mesh, &box.allocator, &clone ) ==
             geometry_status_t::OK );
    RequireClosedValid( &clone, "clone" );
    CHECK( EveryTopologyHandleResolves( &clone ) );
    CHECK( EditableMesh_VertexCount( &clone ) ==
           EditableMesh_VertexCount( &box.mesh ) );
    CHECK( EditableMesh_HalfEdgeCount( &clone ) ==
           EditableMesh_HalfEdgeCount( &box.mesh ) );
    CHECK( EditableMesh_EdgeCount( &clone ) ==
           EditableMesh_EdgeCount( &box.mesh ) );
    CHECK( EditableMesh_LoopCount( &clone ) ==
           EditableMesh_LoopCount( &box.mesh ) );
    CHECK( EditableMesh_FaceCount( &clone ) ==
           EditableMesh_FaceCount( &box.mesh ) );
    CHECK( EditableMesh_ShellCount( &clone ) ==
           EditableMesh_ShellCount( &box.mesh ) );
    CHECK( EditableMesh_SignedVolume( &clone ) ==
           Catch::Approx( EditableMesh_SignedVolume( &box.mesh ) ) );
    EditableMesh_Shutdown( &clone );
}

TEST_CASE( "MeshInvariant: RecalculateNormals keeps a valid box valid",
           "[MeshInvariant]" )
{
    InvariantBox box;
    REQUIRE( MeshCleanup_RecalculateNormals( &box.mesh ).status ==
             geometry_status_t::OK );
    RequireClosedValid( &box.mesh, "RecalculateNormals" );
}

TEST_CASE( "MeshCleanup: non-finite tolerances are rejected atomically",
           "[MeshCleanup][Contract]" )
{
    InvariantBox box;
    const common::usize cVertices = EditableMesh_VertexCount( &box.mesh );
    const common::usize cHalfEdges = EditableMesh_HalfEdgeCount( &box.mesh );
    const common::usize cEdges = EditableMesh_EdgeCount( &box.mesh );
    const common::usize cLoops = EditableMesh_LoopCount( &box.mesh );
    const common::usize cFaces = EditableMesh_FaceCount( &box.mesh );
    const common::usize cShells = EditableMesh_ShellCount( &box.mesh );
    const common::f64 fVolume = EditableMesh_SignedVolume( &box.mesh );
    const common::f64 infinity =
        std::numeric_limits<common::f64>::infinity();

    const auto removeResult =
        MeshCleanup_RemoveDegenerateFaces( &box.mesh, infinity );
    CHECK( removeResult.status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( removeResult.cFacesRemoved == 0u );

    const auto smoothResult = MeshCleanup_ComputeAutoSmoothNormals(
        &box.mesh, &box.allocator, infinity );
    CHECK( smoothResult.status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( smoothResult.pNormals == nullptr );
    CHECK( smoothResult.cNormals == 0u );

    CHECK( EditableMesh_VertexCount( &box.mesh ) == cVertices );
    CHECK( EditableMesh_HalfEdgeCount( &box.mesh ) == cHalfEdges );
    CHECK( EditableMesh_EdgeCount( &box.mesh ) == cEdges );
    CHECK( EditableMesh_LoopCount( &box.mesh ) == cLoops );
    CHECK( EditableMesh_FaceCount( &box.mesh ) == cFaces );
    CHECK( EditableMesh_ShellCount( &box.mesh ) == cShells );
    CHECK( EditableMesh_SignedVolume( &box.mesh ) == fVolume );
    RequireClosedValid( &box.mesh, "rejected non-finite cleanup arguments" );
}

TEST_CASE( "MeshCleanup: auto-smooth rejects the first value above pi",
           "[MeshCleanup][Contract]" )
{
    InvariantBox box;
    constexpr common::f64 kPi =
        3.141592653589793238462643383279502884;
    const common::f64 fJustAbovePi = std::nextafter(
        kPi, std::numeric_limits<common::f64>::infinity() );

    const auto result = MeshCleanup_ComputeAutoSmoothNormals(
        &box.mesh, &box.allocator, fJustAbovePi );
    CHECK( result.status == geometry_status_t::INVALID_ARGUMENT );
    CHECK( result.pNormals == nullptr );
    CHECK( result.cNormals == 0u );
    RequireClosedValid( &box.mesh, "rejected auto-smooth angle above pi" );
}

} // namespace cypher::editor::geometry
