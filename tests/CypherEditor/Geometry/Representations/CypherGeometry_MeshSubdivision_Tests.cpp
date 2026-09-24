//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSubdivision_Tests.cpp
//  Purpose: Contract tests for Catmull-Clark and linear mesh subdivision.
//  Details: Verifies correct element counts, Euler characteristic, vertex
//           repositioning (Catmull-Clark) vs. position preservation
//           (linear), and null-argument rejection.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSubdivision.h"
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
#include <limits>
#include <vector>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Vec3d_LengthSquared;
using Catch::Approx;

namespace {

// Builds a unit box mesh for subdivision tests. A box has 8V, 12E, 6F,
// and each quad face subdivides into 4 quads.
struct SubdivFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t meshIn{};
    editable_mesh_t meshOut{};

    SubdivFixture() {
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

        REQUIRE( EditableMesh_Init( &meshIn, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshBuilder_TryBuildFromBoundary( &meshIn, &boundary ) ==
                 geometry_status_t::OK );

        REQUIRE( EditableMesh_Init( &meshOut, &allocator ) ==
                 geometry_status_t::OK );
    }
    ~SubdivFixture() {
        EditableMesh_Shutdown( &meshOut );
        EditableMesh_Shutdown( &meshIn );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

struct test_directed_edge_t {
    geometry_mesh_half_edge_handle_t hHalfEdge{};
    common::u32 iOrigin{ common::CY_INVALID_INDEX };
    common::u32 iDestination{ common::CY_INVALID_INDEX };
};

math::vec3d_t ComputeTestFaceNormal(
    const std::vector<math::vec3d_t> &positions,
    const std::vector<common::u32> &face )
{
    math::vec3d_t normal = Vec3d_Make( 0.0, 0.0, 0.0 );
    for ( common::usize i = 0u; i < face.size(); ++i ) {
        const math::vec3d_t &a = positions[face[i]];
        const math::vec3d_t &b = positions[face[( i + 1u ) % face.size()]];
        normal.x += ( a.y - b.y ) * ( a.z + b.z );
        normal.y += ( a.z - b.z ) * ( a.x + b.x );
        normal.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    const common::f64 length = std::sqrt( Vec3d_LengthSquared( normal ) );
    REQUIRE( length > 1.0e-12 );
    return math::Vec3d_Scale( normal, 1.0 / length );
}

void BuildIndexedClosedMesh(
    editable_mesh_t *pMesh,
    const std::vector<math::vec3d_t> &positions,
    const std::vector<std::vector<common::u32>> &faces,
    const std::vector<common::u32> &faceShells,
    common::u32 cShells )
{
    REQUIRE( pMesh != nullptr );
    REQUIRE( EditableMesh_IsInitialized( pMesh ) );
    REQUIRE( faces.size() == faceShells.size() );
    REQUIRE( cShells > 0u );

    common::usize cHalfEdges = 0u;
    for ( const auto &face : faces ) {
        REQUIRE( face.size() >= 3u );
        cHalfEdges += face.size();
    }
    REQUIRE( ( cHalfEdges & 1u ) == 0u );

    REQUIRE( common::GenerationPool_Reserve(
                 &pMesh->vertices, positions.size() ) ==
             common::generation_pool_status_t::OK );
    REQUIRE( common::GenerationPool_Reserve(
                 &pMesh->halfEdges, cHalfEdges ) ==
             common::generation_pool_status_t::OK );
    REQUIRE( common::GenerationPool_Reserve(
                 &pMesh->edges, cHalfEdges / 2u ) ==
             common::generation_pool_status_t::OK );
    REQUIRE( common::GenerationPool_Reserve(
                 &pMesh->loops, faces.size() ) ==
             common::generation_pool_status_t::OK );
    REQUIRE( common::GenerationPool_Reserve(
                 &pMesh->faces, faces.size() ) ==
             common::generation_pool_status_t::OK );
    REQUIRE( common::GenerationPool_Reserve(
                 &pMesh->shells, cShells ) ==
             common::generation_pool_status_t::OK );

    std::vector<geometry_mesh_vertex_handle_t> vertexHandles;
    vertexHandles.reserve( positions.size() );
    for ( const math::vec3d_t &position : positions ) {
        mesh_vertex_record_t vertex{};
        vertex.position = position;
        const auto inserted = common::GenerationPool_Insert(
            &pMesh->vertices, vertex );
        REQUIRE( inserted.status == common::generation_pool_status_t::OK );
        vertexHandles.push_back( inserted.handle );
    }

    std::vector<geometry_mesh_shell_handle_t> shellHandles;
    shellHandles.reserve( cShells );
    for ( common::u32 i = 0u; i < cShells; ++i ) {
        const auto inserted = common::GenerationPool_Insert(
            &pMesh->shells, mesh_shell_record_t{} );
        REQUIRE( inserted.status == common::generation_pool_status_t::OK );
        shellHandles.push_back( inserted.handle );
    }

    std::vector<test_directed_edge_t> directedEdges;
    directedEdges.reserve( cHalfEdges );
    for ( common::usize iFace = 0u; iFace < faces.size(); ++iFace ) {
        const std::vector<common::u32> &indices = faces[iFace];
        REQUIRE( faceShells[iFace] < cShells );

        std::vector<geometry_mesh_half_edge_handle_t> halfEdges;
        halfEdges.reserve( indices.size() );
        for ( common::usize i = 0u; i < indices.size(); ++i ) {
            REQUIRE( indices[i] < vertexHandles.size() );
            mesh_half_edge_record_t halfEdge{};
            halfEdge.hOrigin = vertexHandles[indices[i]];
            const auto inserted = common::GenerationPool_Insert(
                &pMesh->halfEdges, halfEdge );
            REQUIRE( inserted.status ==
                     common::generation_pool_status_t::OK );
            halfEdges.push_back( inserted.handle );
            directedEdges.push_back( {
                inserted.handle,
                indices[i],
                indices[( i + 1u ) % indices.size()]
            } );

            mesh_vertex_record_t *pVertex = common::GenerationPool_Get(
                &pMesh->vertices, vertexHandles[indices[i]] );
            REQUIRE( pVertex != nullptr );
            if ( !common::GenerationHandle_IsValid(
                     pVertex->hOutHalfEdge ) ) {
                pVertex->hOutHalfEdge = inserted.handle;
            }
        }
        for ( common::usize i = 0u; i < halfEdges.size(); ++i ) {
            mesh_half_edge_record_t *pHalfEdge = common::GenerationPool_Get(
                &pMesh->halfEdges, halfEdges[i] );
            REQUIRE( pHalfEdge != nullptr );
            pHalfEdge->hNext = halfEdges[( i + 1u ) % halfEdges.size()];
            pHalfEdge->hPrev = halfEdges[
                ( i + halfEdges.size() - 1u ) % halfEdges.size()];
        }

        mesh_loop_record_t loop{};
        loop.hFirstHalfEdge = halfEdges[0];
        loop.cHalfEdges = static_cast<common::u32>( halfEdges.size() );
        const auto insertedLoop = common::GenerationPool_Insert(
            &pMesh->loops, loop );
        REQUIRE( insertedLoop.status ==
                 common::generation_pool_status_t::OK );
        for ( geometry_mesh_half_edge_handle_t hHalfEdge : halfEdges ) {
            mesh_half_edge_record_t *pHalfEdge = common::GenerationPool_Get(
                &pMesh->halfEdges, hHalfEdge );
            REQUIRE( pHalfEdge != nullptr );
            pHalfEdge->hLoop = insertedLoop.handle;
        }

        mesh_face_record_t face{};
        face.hOuterLoop = insertedLoop.handle;
        face.hShell = shellHandles[faceShells[iFace]];
        face.normal = ComputeTestFaceNormal( positions, indices );
        const auto insertedFace = common::GenerationPool_Insert(
            &pMesh->faces, face );
        REQUIRE( insertedFace.status ==
                 common::generation_pool_status_t::OK );
        mesh_loop_record_t *pLoop = common::GenerationPool_Get(
            &pMesh->loops, insertedLoop.handle );
        REQUIRE( pLoop != nullptr );
        pLoop->hFace = insertedFace.handle;

        mesh_shell_record_t *pShell = common::GenerationPool_Get(
            &pMesh->shells, shellHandles[faceShells[iFace]] );
        REQUIRE( pShell != nullptr );
        if ( !common::GenerationHandle_IsValid( pShell->hAnyFace ) ) {
            pShell->hAnyFace = insertedFace.handle;
        }
        ++pShell->cFaces;
    }

    std::vector<bool> paired( directedEdges.size(), false );
    for ( common::usize i = 0u; i < directedEdges.size(); ++i ) {
        if ( paired[i] ) { continue; }
        common::usize iTwin = directedEdges.size();
        for ( common::usize j = i + 1u; j < directedEdges.size(); ++j ) {
            if ( !paired[j] &&
                 directedEdges[i].iOrigin ==
                     directedEdges[j].iDestination &&
                 directedEdges[i].iDestination ==
                     directedEdges[j].iOrigin ) {
                REQUIRE( iTwin == directedEdges.size() );
                iTwin = j;
            }
        }
        REQUIRE( iTwin != directedEdges.size() );
        paired[i] = true;
        paired[iTwin] = true;

        mesh_half_edge_record_t *pA = common::GenerationPool_Get(
            &pMesh->halfEdges, directedEdges[i].hHalfEdge );
        mesh_half_edge_record_t *pB = common::GenerationPool_Get(
            &pMesh->halfEdges, directedEdges[iTwin].hHalfEdge );
        REQUIRE( pA != nullptr );
        REQUIRE( pB != nullptr );
        pA->hTwin = directedEdges[iTwin].hHalfEdge;
        pB->hTwin = directedEdges[i].hHalfEdge;

        mesh_edge_record_t edge{};
        edge.hHalfEdge = directedEdges[i].hHalfEdge;
        const auto insertedEdge = common::GenerationPool_Insert(
            &pMesh->edges, edge );
        REQUIRE( insertedEdge.status ==
                 common::generation_pool_status_t::OK );
        pA->hEdge = insertedEdge.handle;
        pB->hEdge = insertedEdge.handle;
    }

    const mesh_validation_result_t validation =
        MeshValidation_Validate( pMesh );
    REQUIRE( validation.status == geometry_status_t::OK );
}

void AppendBox(
    std::vector<math::vec3d_t> *pPositions,
    std::vector<std::vector<common::u32>> *pFaces,
    std::vector<common::u32> *pFaceShells,
    math::vec3d_t center,
    common::u32 iShell )
{
    const common::u32 base =
        static_cast<common::u32>( pPositions->size() );
    const common::f64 x = center.x;
    const common::f64 y = center.y;
    const common::f64 z = center.z;
    pPositions->insert( pPositions->end(), {
        Vec3d_Make( x - 1.0, y - 1.0, z - 1.0 ),
        Vec3d_Make( x + 1.0, y - 1.0, z - 1.0 ),
        Vec3d_Make( x + 1.0, y + 1.0, z - 1.0 ),
        Vec3d_Make( x - 1.0, y + 1.0, z - 1.0 ),
        Vec3d_Make( x - 1.0, y - 1.0, z + 1.0 ),
        Vec3d_Make( x + 1.0, y - 1.0, z + 1.0 ),
        Vec3d_Make( x + 1.0, y + 1.0, z + 1.0 ),
        Vec3d_Make( x - 1.0, y + 1.0, z + 1.0 )
    } );
    const common::u32 localFaces[6][4] = {
        { 0u, 3u, 2u, 1u }, { 4u, 5u, 6u, 7u },
        { 0u, 1u, 5u, 4u }, { 1u, 2u, 6u, 5u },
        { 2u, 3u, 7u, 6u }, { 3u, 0u, 4u, 7u }
    };
    for ( const auto &localFace : localFaces ) {
        pFaces->push_back( {
            base + localFace[0], base + localFace[1],
            base + localFace[2], base + localFace[3]
        } );
        pFaceShells->push_back( iShell );
    }
}

struct subdivision_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *SubdivisionFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<subdivision_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) {
        return nullptr;
    }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    void *pMemory = pSystem->pfnAllocate(
        pSystem->pUserData, cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void SubdivisionFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<subdivision_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree(
        pSystem->pUserData, pMemory, cbSize, nAlignment );
}

common::allocator_t MakeSubdivisionFailureAllocator(
    subdivision_failure_allocator_state_t *pState ) noexcept
{
    return {
        &SubdivisionFailureAllocate,
        nullptr,
        &SubdivisionFailureFree,
        pState
    };
}

using subdivision_function_t = geometry_status_t (*)(
    const editable_mesh_t *, editable_mesh_t * ) noexcept;

void CheckAllocationFailureContract(
    const editable_mesh_t &meshIn,
    subdivision_function_t pfnSubdivision )
{
    subdivision_failure_allocator_state_t baselineState{};
    baselineState.iFailOnCall =
        std::numeric_limits<common::usize>::max();
    common::allocator_t baselineAllocator =
        MakeSubdivisionFailureAllocator( &baselineState );
    editable_mesh_t baselineOutput{};
    REQUIRE( EditableMesh_Init(
                 &baselineOutput, &baselineAllocator ) ==
             geometry_status_t::OK );
    REQUIRE( pfnSubdivision( &meshIn, &baselineOutput ) ==
             geometry_status_t::OK );
    const common::usize cAllocationCalls =
        baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    EditableMesh_Shutdown( &baselineOutput );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        DYNAMIC_SECTION( "allocation call " << iFail ) {
            subdivision_failure_allocator_state_t state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator =
                MakeSubdivisionFailureAllocator( &state );
            editable_mesh_t output{};
            REQUIRE( EditableMesh_Init( &output, &allocator ) ==
                     geometry_status_t::OK );
            REQUIRE( pfnSubdivision( &meshIn, &output ) ==
                     geometry_status_t::ALLOCATION_FAILED );
            CHECK_FALSE( EditableMesh_IsInitialized( &output ) );
            CHECK( EditableMesh_VertexCount( &output ) == 0u );
            CHECK( EditableMesh_HalfEdgeCount( &output ) == 0u );
            CHECK( EditableMesh_EdgeCount( &output ) == 0u );
            CHECK( EditableMesh_LoopCount( &output ) == 0u );
            CHECK( EditableMesh_FaceCount( &output ) == 0u );
            CHECK( EditableMesh_ShellCount( &output ) == 0u );
            CHECK( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Catmull-Clark tests
// ---------------------------------------------------------------------------

TEST_CASE( "catmull-clark subdivision of box produces correct element counts",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    // Box: 8V, 12E, 6F (all quads).
    // After one CC subdivision of a closed mesh with V vertices, E edges,
    // and F faces (all quads):
    //   V' = F + E + V = 6 + 12 + 8 = 26
    //   F' = sum of face edge counts = 6 * 4 = 24
    //   E' = 2 * F' (each original face produces N quads, each quad has
    //        4 edges, shared between adjacent quads → E' = 2 * F')
    //        = 48
    REQUIRE( MeshSubdivision_TryCatmullClark( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    CHECK( EditableMesh_VertexCount( &f.meshOut ) == 26u );
    CHECK( EditableMesh_FaceCount( &f.meshOut ) == 24u );
    CHECK( EditableMesh_EdgeCount( &f.meshOut ) == 48u );
}

TEST_CASE( "catmull-clark subdivision preserves Euler characteristic",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    REQUIRE( MeshSubdivision_TryCatmullClark( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    // Closed genus-0 surface: V - E + F = 2.
    CHECK( EditableMesh_EulerCharacteristic( &f.meshOut ) == 2 );
}

TEST_CASE( "catmull-clark smooths vertices inward for a box",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    REQUIRE( MeshSubdivision_TryCatmullClark( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    // After CC subdivision of a unit box, all vertices should lie strictly
    // inside the original bounding box [-1,1]^3, because the smoothing
    // pulls corners inward. Check that every output vertex is within the
    // original extents.
    bool allInside = true;
    (void)common::GenerationPool_ForEach( &f.meshOut.vertices,
        [&]( geometry_mesh_vertex_handle_t /*hV*/,
             const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            if ( std::fabs( v.position.x ) > 1.0 + 1.0e-9 ||
                 std::fabs( v.position.y ) > 1.0 + 1.0e-9 ||
                 std::fabs( v.position.z ) > 1.0 + 1.0e-9 ) {
                allInside = false;
            }
            return true;
        } );
    CHECK( allInside );

    // At least one moved original-vertex should be strictly inside (not on
    // the box surface). The CC rule pulls box corners inward, so no output
    // vertex should sit at exactly ±1 on all three axes.
    bool someInward = false;
    (void)common::GenerationPool_ForEach( &f.meshOut.vertices,
        [&]( geometry_mesh_vertex_handle_t /*hV*/,
             const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            const common::f64 maxCoord =
                std::fmax( std::fabs( v.position.x ),
                std::fmax( std::fabs( v.position.y ),
                           std::fabs( v.position.z ) ) );
            if ( maxCoord < 1.0 - 1.0e-6 ) {
                someInward = true;
            }
            return true;
        } );
    CHECK( someInward );
}

TEST_CASE( "catmull-clark produces all-quad output",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    REQUIRE( MeshSubdivision_TryCatmullClark( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    // Every face should have exactly 4 edges (quad).
    bool allQuads = true;
    (void)common::GenerationPool_ForEach( &f.meshOut.faces,
        [&]( geometry_mesh_face_handle_t /*hF*/,
             const mesh_face_record_t &face ) noexcept -> common::bool_t {
            const mesh_loop_record_t *pLoop =
                common::GenerationPool_Get( &f.meshOut.loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges != 4u ) {
                allQuads = false;
            }
            return true;
        } );
    CHECK( allQuads );
}

TEST_CASE( "catmull-clark null args rejected",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    CHECK( MeshSubdivision_TryCatmullClark( nullptr, &f.meshOut ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshSubdivision_TryCatmullClark( &f.meshIn, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// Linear subdivision tests
// ---------------------------------------------------------------------------

TEST_CASE( "linear subdivision of box produces correct element counts",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    // Same topology as CC: V' = 26, F' = 24, E' = 48.
    REQUIRE( MeshSubdivision_TryLinear( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    CHECK( EditableMesh_VertexCount( &f.meshOut ) == 26u );
    CHECK( EditableMesh_FaceCount( &f.meshOut ) == 24u );
    CHECK( EditableMesh_EdgeCount( &f.meshOut ) == 48u );
}

TEST_CASE( "linear subdivision preserves Euler characteristic",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    REQUIRE( MeshSubdivision_TryLinear( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    CHECK( EditableMesh_EulerCharacteristic( &f.meshOut ) == 2 );
}

TEST_CASE( "linear subdivision preserves original vertex positions",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    REQUIRE( MeshSubdivision_TryLinear( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    // The 8 original box corners (±1, ±1, ±1) should appear in the
    // output mesh at their exact original positions.
    const math::vec3d_t corners[8] = {
        Vec3d_Make( -1.0, -1.0, -1.0 ), Vec3d_Make(  1.0, -1.0, -1.0 ),
        Vec3d_Make( -1.0,  1.0, -1.0 ), Vec3d_Make(  1.0,  1.0, -1.0 ),
        Vec3d_Make( -1.0, -1.0,  1.0 ), Vec3d_Make(  1.0, -1.0,  1.0 ),
        Vec3d_Make( -1.0,  1.0,  1.0 ), Vec3d_Make(  1.0,  1.0,  1.0 ),
    };

    for ( const auto &corner : corners ) {
        bool found = false;
        (void)common::GenerationPool_ForEach( &f.meshOut.vertices,
            [&]( geometry_mesh_vertex_handle_t /*hV*/,
                 const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
                const math::vec3d_t diff = math::Vec3d_Subtract(
                    v.position, corner );
                if ( math::Vec3d_LengthSquared( diff ) < 1.0e-12 ) {
                    found = true;
                    return false;
                }
                return true;
            } );
        CHECK( found );
    }
}

TEST_CASE( "linear subdivision produces all-quad output",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    REQUIRE( MeshSubdivision_TryLinear( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    bool allQuads = true;
    (void)common::GenerationPool_ForEach( &f.meshOut.faces,
        [&]( geometry_mesh_face_handle_t /*hF*/,
             const mesh_face_record_t &face ) noexcept -> common::bool_t {
            const mesh_loop_record_t *pLoop =
                common::GenerationPool_Get( &f.meshOut.loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges != 4u ) {
                allQuads = false;
            }
            return true;
        } );
    CHECK( allQuads );
}

TEST_CASE( "linear subdivision null args rejected",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    CHECK( MeshSubdivision_TryLinear( nullptr, &f.meshOut ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshSubdivision_TryLinear( &f.meshIn, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "linear subdivision vertices stay on original surface",
           "[Gate12][Subdivision]" )
{
    SubdivFixture f;

    REQUIRE( MeshSubdivision_TryLinear( &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );

    // For a unit box, every output vertex should lie on one of the six
    // axis-aligned faces — i.e., at least one coordinate should be ±1.
    // Edge midpoints have one coord ±1 (on the face), face centroids have
    // one coord ±1, and original corners have all three ±1.
    bool allOnSurface = true;
    (void)common::GenerationPool_ForEach( &f.meshOut.vertices,
        [&]( geometry_mesh_vertex_handle_t /*hV*/,
             const mesh_vertex_record_t &v ) noexcept -> common::bool_t {
            const bool onX = std::fabs( std::fabs( v.position.x ) - 1.0 ) < 1.0e-9;
            const bool onY = std::fabs( std::fabs( v.position.y ) - 1.0 ) < 1.0e-9;
            const bool onZ = std::fabs( std::fabs( v.position.z ) - 1.0 ) < 1.0e-9;
            if ( !onX && !onY && !onZ ) {
                allOnSurface = false;
            }
            return true;
    } );
    CHECK( allOnSurface );
}

TEST_CASE( "subdivision outputs pass complete mesh validation",
           "[Gate12][Subdivision][Validation]" )
{
    SubdivFixture linear;
    REQUIRE( MeshSubdivision_TryLinear(
                 &linear.meshIn, &linear.meshOut ) ==
             geometry_status_t::OK );
    const mesh_validation_result_t linearValidation =
        MeshValidation_Validate( &linear.meshOut );
    CHECK( linearValidation.status == geometry_status_t::OK );
    CHECK( linearValidation.bReciprocalTwins );
    CHECK( linearValidation.bClosedLoops );
    CHECK( linearValidation.bEdgeLinks );
    CHECK( linearValidation.bConsistentWinding );
    CHECK( linearValidation.bPositiveVolume );

    SubdivFixture catmullClark;
    REQUIRE( MeshSubdivision_TryCatmullClark(
                 &catmullClark.meshIn, &catmullClark.meshOut ) ==
             geometry_status_t::OK );
    const mesh_validation_result_t catmullClarkValidation =
        MeshValidation_Validate( &catmullClark.meshOut );
    CHECK( catmullClarkValidation.status == geometry_status_t::OK );
    CHECK( catmullClarkValidation.bReciprocalTwins );
    CHECK( catmullClarkValidation.bClosedLoops );
    CHECK( catmullClarkValidation.bEdgeLinks );
    CHECK( catmullClarkValidation.bConsistentWinding );
    CHECK( catmullClarkValidation.bPositiveVolume );
}

TEST_CASE( "subdivision preserves disconnected shell ownership",
           "[Gate12][Subdivision][Topology]" )
{
    const common::allocator_t *pAllocator = common::Allocator_GetSystem();
    editable_mesh_t input{};
    editable_mesh_t output{};
    REQUIRE( EditableMesh_Init( &input, pAllocator ) ==
             geometry_status_t::OK );
    REQUIRE( EditableMesh_Init( &output, pAllocator ) ==
             geometry_status_t::OK );

    std::vector<math::vec3d_t> positions;
    std::vector<std::vector<common::u32>> faces;
    std::vector<common::u32> faceShells;
    AppendBox(
        &positions, &faces, &faceShells,
        Vec3d_Make( -3.0, 0.0, 0.0 ), 0u );
    AppendBox(
        &positions, &faces, &faceShells,
        Vec3d_Make( 3.0, 0.0, 0.0 ), 1u );
    BuildIndexedClosedMesh(
        &input, positions, faces, faceShells, 2u );

    REQUIRE( MeshSubdivision_TryLinear( &input, &output ) ==
             geometry_status_t::OK );
    CHECK( EditableMesh_ShellCount( &output ) == 2u );
    CHECK( EditableMesh_VertexCount( &output ) == 52u );
    CHECK( EditableMesh_FaceCount( &output ) == 48u );
    CHECK( MeshValidation_Validate( &output ).status ==
           geometry_status_t::OK );

    EditableMesh_Shutdown( &output );
    EditableMesh_Shutdown( &input );
}

TEST_CASE( "subdivision supports faces and vertex fans beyond 256",
           "[Gate12][Subdivision][LargeTopology]" )
{
    constexpr common::u32 cRing = 257u;
    constexpr common::f64 twoPi =
        6.283185307179586476925286766559;

    std::vector<math::vec3d_t> positions;
    positions.reserve( cRing + 1u );
    for ( common::u32 i = 0u; i < cRing; ++i ) {
        const common::f64 angle =
            twoPi * static_cast<common::f64>( i ) /
            static_cast<common::f64>( cRing );
        positions.push_back( Vec3d_Make(
            std::cos( angle ), std::sin( angle ), -1.0 ) );
    }
    const common::u32 iApex = cRing;
    positions.push_back( Vec3d_Make( 0.0, 0.0, 1.0 ) );

    std::vector<std::vector<common::u32>> faces;
    std::vector<common::u32> faceShells;
    std::vector<common::u32> base;
    base.reserve( cRing );
    for ( common::u32 i = cRing; i > 0u; --i ) {
        base.push_back( i - 1u );
    }
    faces.push_back( base );
    faceShells.push_back( 0u );
    for ( common::u32 i = 0u; i < cRing; ++i ) {
        faces.push_back( { i, ( i + 1u ) % cRing, iApex } );
        faceShells.push_back( 0u );
    }

    const common::allocator_t *pAllocator = common::Allocator_GetSystem();
    editable_mesh_t input{};
    editable_mesh_t linear{};
    editable_mesh_t catmullClark{};
    REQUIRE( EditableMesh_Init( &input, pAllocator ) ==
             geometry_status_t::OK );
    REQUIRE( EditableMesh_Init( &linear, pAllocator ) ==
             geometry_status_t::OK );
    REQUIRE( EditableMesh_Init( &catmullClark, pAllocator ) ==
             geometry_status_t::OK );
    BuildIndexedClosedMesh(
        &input, positions, faces, faceShells, 1u );
    CHECK( EditableMesh_VertexValence(
               &input,
               common::generation_handle_t<geometry_mesh_vertex_tag_t>{
                   iApex, 1u } ) == cRing );

    REQUIRE( MeshSubdivision_TryLinear( &input, &linear ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSubdivision_TryCatmullClark(
                 &input, &catmullClark ) ==
             geometry_status_t::OK );
    CHECK( EditableMesh_FaceCount( &linear ) == 4u * cRing );
    CHECK( EditableMesh_FaceCount( &catmullClark ) == 4u * cRing );
    CHECK( MeshValidation_Validate( &linear ).status ==
           geometry_status_t::OK );
    CHECK( MeshValidation_Validate( &catmullClark ).status ==
           geometry_status_t::OK );

    EditableMesh_Shutdown( &catmullClark );
    EditableMesh_Shutdown( &linear );
    EditableMesh_Shutdown( &input );
}

TEST_CASE( "catmull-clark preserves crease weights on split source edges",
           "[Gate12][Subdivision][Crease]" )
{
    SubdivFixture f;
    (void)common::GenerationPool_ForEach( &f.meshIn.edges,
        [&]( geometry_mesh_edge_handle_t,
             mesh_edge_record_t &edge ) noexcept -> common::bool_t {
            edge.creaseWeight = 1.0;
            return true;
        } );

    REQUIRE( MeshSubdivision_TryCatmullClark(
                 &f.meshIn, &f.meshOut ) ==
             geometry_status_t::OK );
    common::usize cCreased = 0u;
    common::usize cSmooth = 0u;
    (void)common::GenerationPool_ForEach( &f.meshOut.edges,
        [&]( geometry_mesh_edge_handle_t,
             const mesh_edge_record_t &edge ) noexcept -> common::bool_t {
            if ( edge.creaseWeight == Approx( 1.0 ) ) {
                ++cCreased;
            } else if ( edge.creaseWeight == Approx( 0.0 ) ) {
                ++cSmooth;
            }
            return true;
        } );
    CHECK( cCreased == 24u );
    CHECK( cSmooth == 24u );
}

TEST_CASE( "subdivision rejects non-empty and undersized outputs cleanly",
           "[Gate12][Subdivision][Contract]" )
{
    SECTION( "non-empty output" ) {
        SubdivFixture f;
        const auto inserted = common::GenerationPool_Insert(
            &f.meshOut.vertices, mesh_vertex_record_t{} );
        REQUIRE( inserted.status ==
                 common::generation_pool_status_t::OK );
        CHECK( MeshSubdivision_TryLinear(
                   &f.meshIn, &f.meshOut ) ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK_FALSE( EditableMesh_IsInitialized( &f.meshOut ) );
        CHECK( EditableMesh_VertexCount( &f.meshOut ) == 0u );
    }

    SECTION( "output element limit" ) {
        SubdivFixture f;
        EditableMesh_Shutdown( &f.meshOut );
        editable_mesh_limits_t limits{};
        limits.cVertexMax = 25u;
        REQUIRE( EditableMesh_Init(
                     &f.meshOut, &f.allocator, limits ) ==
                 geometry_status_t::OK );
        CHECK( MeshSubdivision_TryLinear(
                   &f.meshIn, &f.meshOut ) ==
               geometry_status_t::LIMIT_EXCEEDED );
        CHECK_FALSE( EditableMesh_IsInitialized( &f.meshOut ) );
    }
}

TEST_CASE( "subdivision rejects corrupt input and cleans output",
           "[Gate12][Subdivision][Contract]" )
{
    SubdivFixture f;
    geometry_mesh_half_edge_handle_t hHalfEdge{};
    geometry_mesh_half_edge_handle_t hSavedTwin{};
    (void)common::GenerationPool_ForEach( &f.meshIn.halfEdges,
        [&]( geometry_mesh_half_edge_handle_t hCurrent,
             mesh_half_edge_record_t &halfEdge ) noexcept -> common::bool_t {
            hHalfEdge = hCurrent;
            hSavedTwin = halfEdge.hTwin;
            halfEdge.hTwin = {};
            return false;
        } );
    REQUIRE( common::GenerationHandle_IsValid( hHalfEdge ) );

    CHECK( MeshSubdivision_TryCatmullClark(
               &f.meshIn, &f.meshOut ) ==
           geometry_status_t::INVALID_TOPOLOGY );
    CHECK_FALSE( EditableMesh_IsInitialized( &f.meshOut ) );

    mesh_half_edge_record_t *pHalfEdge = common::GenerationPool_Get(
        &f.meshIn.halfEdges, hHalfEdge );
    REQUIRE( pHalfEdge != nullptr );
    pHalfEdge->hTwin = hSavedTwin;
}

TEST_CASE( "subdivision rejects aliased input without destroying it",
           "[Gate12][Subdivision][Contract]" )
{
    SubdivFixture f;
    const common::usize cVertices = EditableMesh_VertexCount( &f.meshIn );
    CHECK( MeshSubdivision_TryLinear( &f.meshIn, &f.meshIn ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( EditableMesh_IsInitialized( &f.meshIn ) );
    CHECK( EditableMesh_VertexCount( &f.meshIn ) == cVertices );
}

TEST_CASE( "subdivision is allocation-failure atomic",
           "[Gate12][Subdivision][Allocation][Contract]" )
{
    SubdivFixture f;
    SECTION( "linear" ) {
        CheckAllocationFailureContract(
            f.meshIn, &MeshSubdivision_TryLinear );
    }
    SECTION( "catmull-clark" ) {
        CheckAllocationFailureContract(
            f.meshIn, &MeshSubdivision_TryCatmullClark );
    }
}

} // namespace cypher::editor::geometry
