//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBuilder.cpp
//  Purpose: Implements half-edge mesh construction from brush boundaries.
//  Details: The builder maps boundary vertices 1:1 into the mesh vertex
//           pool, then iterates each boundary face to emit half-edges in
//           CCW order. Twin pairing uses a vertex-pair lookup: for each
//           half-edge with origin A and destination B, it searches for an
//           existing half-edge with origin B and destination A. After all
//           faces are processed, every half-edge must have a twin (closed
//           manifold), and every edge record is created from each twin
//           pair exactly once.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherCommon_Vector.h"

#include <cmath>
#include <cstring>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Compact key for looking up a half-edge by its origin and destination
// vertex slots. Used to find twin half-edges during construction.
struct half_edge_key_t {
    u32 iOriginSlot;
    u32 iDestSlot;
};

// Temporary storage for half-edge keys and their handles, used during
// twin pairing. A linear scan is acceptable for brush-scale meshes
// (typical face count < 64, half-edge count < 256).
struct builder_context_t {
    // Parallel arrays: keys[i] describes halfEdgeHandles[i].
    vector_t<half_edge_key_t> keys;
    vector_t<geometry_mesh_half_edge_handle_t> halfEdgeHandles;

    // Vertex handles in boundary vertex order (index i → handle for
    // boundary vertex i).
    vector_t<geometry_mesh_vertex_handle_t> vertexHandles;

    const allocator_t *pAllocator{ nullptr };
};

geometry_status_t BuilderContext_Init(
    builder_context_t *pCtx,
    const allocator_t *pAllocator,
    usize cVertices,
    usize cHalfEdges ) noexcept
{
    pCtx->pAllocator = pAllocator;
    if ( !Vector_Init( &pCtx->keys, pAllocator, cHalfEdges ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !Vector_Init(
             &pCtx->halfEdgeHandles, pAllocator, cHalfEdges ) ) {
        Vector_Shutdown( &pCtx->keys );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !Vector_Init( &pCtx->vertexHandles, pAllocator, cVertices ) ) {
        Vector_Shutdown( &pCtx->halfEdgeHandles );
        Vector_Shutdown( &pCtx->keys );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void BuilderContext_Shutdown( builder_context_t *pCtx ) noexcept
{
    Vector_Shutdown( &pCtx->keys );
    Vector_Shutdown( &pCtx->halfEdgeHandles );
    Vector_Shutdown( &pCtx->vertexHandles );
}

// Finds the handle of a previously inserted half-edge whose origin is
// iOriginSlot and destination is iDestSlot. Returns INVALID if not found.
geometry_mesh_half_edge_handle_t FindHalfEdge(
    const builder_context_t *pCtx,
    u32 iOriginSlot,
    u32 iDestSlot ) noexcept
{
    for ( usize i = 0u; i < pCtx->keys.nCount; ++i ) {
        if ( pCtx->keys.pData[i].iOriginSlot == iOriginSlot &&
             pCtx->keys.pData[i].iDestSlot == iDestSlot ) {
            return pCtx->halfEdgeHandles.pData[i];
        }
    }
    return GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
}

bool MeshIsEmpty( const editable_mesh_t &mesh ) noexcept
{
    return GenerationPool_Count( &mesh.vertices ) == 0u &&
           GenerationPool_Count( &mesh.halfEdges ) == 0u &&
           GenerationPool_Count( &mesh.edges ) == 0u &&
           GenerationPool_Count( &mesh.loops ) == 0u &&
           GenerationPool_Count( &mesh.faces ) == 0u &&
           GenerationPool_Count( &mesh.shells ) == 0u;
}

void ClearMeshRecords( editable_mesh_t *pMesh ) noexcept
{
    // Clear in ownership order so no live parent record refers to an element
    // that has already been destroyed. Pool capacity is deliberately retained
    // for the caller's next build attempt.
    GenerationPool_Clear( &pMesh->shells );
    GenerationPool_Clear( &pMesh->faces );
    GenerationPool_Clear( &pMesh->loops );
    GenerationPool_Clear( &pMesh->edges );
    GenerationPool_Clear( &pMesh->halfEdges );
    GenerationPool_Clear( &pMesh->vertices );
}

geometry_status_t TryComputeBoundaryFaceNormal(
    const brush_boundary_t &boundary,
    const brush_boundary_face_t &face,
    math::vec3d_t *pNormalOut ) noexcept
{
    const usize cIndices = boundary.faceVertexIndices.nCount;
    const usize iFirst = static_cast<usize>( face.iFirstIndex );
    const usize cFaceVertices = static_cast<usize>( face.cVertices );
    if ( face.cVertices < 3u || face.cVertices > 256u ) {
        return face.cVertices > 256u
            ? geometry_status_t::LIMIT_EXCEEDED
            : geometry_status_t::DEGENERATE;
    }
    if ( iFirst > cIndices || cFaceVertices > cIndices - iFirst ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    // Newell's method uses the complete polygon ring. Unlike a cross product
    // of the first three points it remains valid when those points happen to
    // be collinear but the polygon as a whole is not degenerate.
    math::vec3d_t normal{};
    for ( usize i = 0u; i < cFaceVertices; ++i ) {
        const u32 iCurrent = boundary.faceVertexIndices.pData[iFirst + i];
        const u32 iNext = boundary.faceVertexIndices.pData[
            iFirst + ( ( i + 1u ) % cFaceVertices )];
        if ( static_cast<usize>( iCurrent ) >= boundary.vertices.nCount ||
             static_cast<usize>( iNext ) >= boundary.vertices.nCount ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        const math::vec3d_t &current = boundary.vertices.pData[iCurrent];
        const math::vec3d_t &next = boundary.vertices.pData[iNext];
        if ( !math::Vec3d_IsFinite( current ) ||
             !math::Vec3d_IsFinite( next ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }

        normal.x += ( current.y - next.y ) * ( current.z + next.z );
        normal.y += ( current.z - next.z ) * ( current.x + next.x );
        normal.z += ( current.x - next.x ) * ( current.y + next.y );
    }

    const f64 lengthSquared = math::Vec3d_LengthSquared( normal );
    if ( !std::isfinite( lengthSquared ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( lengthSquared <= 1.0e-30 ) {
        return geometry_status_t::DEGENERATE;
    }

    *pNormalOut = math::Vec3d_Scale(
        normal, 1.0 / std::sqrt( lengthSquared ) );
    return math::Vec3d_IsFinite( *pNormalOut )
        ? geometry_status_t::OK
        : geometry_status_t::NUMERIC_FAILURE;
}

geometry_status_t ValidateBoundaryForBuild(
    const editable_mesh_t &mesh,
    const brush_boundary_t &boundary,
    usize *pHalfEdgeCountOut ) noexcept
{
    if ( boundary.vertices.pAllocator == nullptr ||
         boundary.edges.pAllocator == nullptr ||
         boundary.faces.pAllocator == nullptr ||
         boundary.faceVertexIndices.pAllocator == nullptr ||
         !Vector_IsValid( &boundary.vertices ) ||
         !Vector_IsValid( &boundary.edges ) ||
         !Vector_IsValid( &boundary.faces ) ||
         !Vector_IsValid( &boundary.faceVertexIndices ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const usize cVertices = boundary.vertices.nCount;
    const usize cEdges = boundary.edges.nCount;
    const usize cFaces = boundary.faces.nCount;
    if ( cVertices < 4u || cEdges < 6u || cFaces < 4u ||
         boundary.faceVertexIndices.nCount == 0u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( cVertices > mesh.limits.cVertexMax ||
         cFaces > mesh.limits.cFaceMax || cFaces > mesh.limits.cLoopMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    usize cHalfEdges = 0u;
    for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = boundary.faces.pData[iFace];
        math::vec3d_t ignoredNormal{};
        const geometry_status_t normalStatus =
            TryComputeBoundaryFaceNormal( boundary, face, &ignoredNormal );
        if ( normalStatus != geometry_status_t::OK ) {
            return normalStatus;
        }
        if ( static_cast<usize>( face.cVertices ) >
             CY_USIZE_MAX - cHalfEdges ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cHalfEdges += static_cast<usize>( face.cVertices );
    }

    if ( ( cHalfEdges & 1u ) != 0u ) {
        return geometry_status_t::NON_MANIFOLD;
    }
    if ( cHalfEdges > mesh.limits.cHalfEdgeMax ||
         cHalfEdges / 2u > mesh.limits.cEdgeMax ||
         mesh.limits.cShellMax < 1u ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    *pHalfEdgeCountOut = cHalfEdges;
    return geometry_status_t::OK;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

geometry_status_t MeshBuilder_TryBuildFromBoundary(
    editable_mesh_t *pMeshOut,
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( pMeshOut == nullptr || pBoundary == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMeshOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !MeshIsEmpty( *pMeshOut ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const usize cVerts = pBoundary->vertices.nCount;
    const usize cFaces = pBoundary->faces.nCount;

    usize cTotalHalfEdges = 0u;
    geometry_status_t s = ValidateBoundaryForBuild(
        *pMeshOut, *pBoundary, &cTotalHalfEdges );
    if ( s != geometry_status_t::OK ) {
        return s;
    }

    // Reserve pool capacity up front so no reallocation mid-build.
    auto reservePool = [&]( auto *pPool, usize count ) -> geometry_status_t {
        const auto status = GenerationPool_Reserve( pPool, count );
        return GeometryStatus_FromGenerationPoolStatus( status );
    };

    s = reservePool( &pMeshOut->vertices, cVerts );
    if ( s != geometry_status_t::OK ) { return s; }
    s = reservePool( &pMeshOut->halfEdges, cTotalHalfEdges );
    if ( s != geometry_status_t::OK ) { return s; }
    s = reservePool( &pMeshOut->edges, cTotalHalfEdges / 2u );
    if ( s != geometry_status_t::OK ) { return s; }
    s = reservePool( &pMeshOut->loops, cFaces );
    if ( s != geometry_status_t::OK ) { return s; }
    s = reservePool( &pMeshOut->faces, cFaces );
    if ( s != geometry_status_t::OK ) { return s; }
    s = reservePool( &pMeshOut->shells, 1u );
    if ( s != geometry_status_t::OK ) { return s; }

    // Temporary builder context for twin pairing.
    builder_context_t ctx{};
    if ( BuilderContext_Init(
             &ctx, pMeshOut->pAllocator, cVerts, cTotalHalfEdges ) !=
         geometry_status_t::OK ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    auto failBuild = [&]( geometry_status_t status ) noexcept {
        BuilderContext_Shutdown( &ctx );
        ClearMeshRecords( pMeshOut );
        return status;
    };

    // Step 1: Insert boundary vertices into the mesh vertex pool.
    for ( usize i = 0u; i < cVerts; ++i ) {
        mesh_vertex_record_t vRec{};
        vRec.position = pBoundary->vertices.pData[i];

        const auto result = GenerationPool_Insert(
            &pMeshOut->vertices, vRec );
        if ( result.status != generation_pool_status_t::OK ) {
            return failBuild(
                GeometryStatus_FromGenerationPoolStatus( result.status ) );
        }
        if ( !Vector_PushBack( &ctx.vertexHandles, result.handle ) ) {
            return failBuild( geometry_status_t::ALLOCATION_FAILED );
        }
    }

    // Step 2: For each face, create half-edges, a loop, and a face record.
    geometry_mesh_face_handle_t hFirstFace =
        GEOMETRY_HANDLE_INVALID<geometry_mesh_face_tag_t>;

    for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &bFace = pBoundary->faces.pData[iFace];
        const u32 cFaceVerts = bFace.cVertices;

        // Create half-edges for each edge of this face polygon.
        geometry_mesh_half_edge_handle_t hFirstHE =
            GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;

        // Temporary array of this face's half-edge handles for linking.
        geometry_mesh_half_edge_handle_t faceHalfEdges[256];
        for ( u32 iV = 0u; iV < cFaceVerts; ++iV ) {
            const u32 iBoundaryVert =
                pBoundary->faceVertexIndices.pData[bFace.iFirstIndex + iV];
            const u32 iBoundaryVertNext =
                pBoundary->faceVertexIndices.pData[
                    bFace.iFirstIndex + ( ( iV + 1u ) % cFaceVerts )];

            mesh_half_edge_record_t heRec{};
            heRec.hOrigin = ctx.vertexHandles.pData[iBoundaryVert];

            const auto heResult = GenerationPool_Insert(
                &pMeshOut->halfEdges, heRec );
            if ( heResult.status != generation_pool_status_t::OK ) {
                return failBuild( GeometryStatus_FromGenerationPoolStatus(
                    heResult.status ) );
            }

            faceHalfEdges[iV] = heResult.handle;

            // Record key for twin lookup.
            half_edge_key_t key{};
            key.iOriginSlot =
                ctx.vertexHandles.pData[iBoundaryVert].nSlot;
            key.iDestSlot =
                ctx.vertexHandles.pData[iBoundaryVertNext].nSlot;
            if ( !Vector_PushBack( &ctx.keys, key ) ||
                 !Vector_PushBack(
                     &ctx.halfEdgeHandles, heResult.handle ) ) {
                return failBuild( geometry_status_t::ALLOCATION_FAILED );
            }

            // Set the vertex's outgoing half-edge if not yet assigned.
            mesh_vertex_record_t *pVert = GenerationPool_Get(
                &pMeshOut->vertices,
                ctx.vertexHandles.pData[iBoundaryVert] );
            if ( pVert != nullptr &&
                 !GenerationHandle_IsValid( pVert->hOutHalfEdge ) ) {
                pVert->hOutHalfEdge = heResult.handle;
            }

            if ( iV == 0u ) {
                hFirstHE = heResult.handle;
            }
        }

        // Link next/prev pointers in the face loop.
        for ( u32 iV = 0u; iV < cFaceVerts; ++iV ) {
            mesh_half_edge_record_t *pHe = GenerationPool_Get(
                &pMeshOut->halfEdges, faceHalfEdges[iV] );
            if ( pHe == nullptr ) {
                return failBuild( geometry_status_t::CORRUPT_STATE );
            }
            pHe->hNext = faceHalfEdges[( iV + 1u ) % cFaceVerts];
            pHe->hPrev = faceHalfEdges[
                ( iV + cFaceVerts - 1u ) % cFaceVerts];
        }

        // Create a loop for this face.
        mesh_loop_record_t loopRec{};
        loopRec.hFirstHalfEdge = hFirstHE;
        loopRec.cHalfEdges = cFaceVerts;

        const auto loopResult = GenerationPool_Insert(
            &pMeshOut->loops, loopRec );
        if ( loopResult.status != generation_pool_status_t::OK ) {
            return failBuild( GeometryStatus_FromGenerationPoolStatus(
                loopResult.status ) );
        }

        // Set the loop handle on all half-edges of this face.
        for ( u32 iV = 0u; iV < cFaceVerts; ++iV ) {
            mesh_half_edge_record_t *pHe = GenerationPool_Get(
                &pMeshOut->halfEdges, faceHalfEdges[iV] );
            if ( pHe != nullptr ) {
                pHe->hLoop = loopResult.handle;
            }
        }

        math::vec3d_t faceNormal{};
        const geometry_status_t normalStatus =
            TryComputeBoundaryFaceNormal( *pBoundary, bFace, &faceNormal );
        if ( normalStatus != geometry_status_t::OK ) {
            return failBuild( normalStatus );
        }

        // Create the face record.
        mesh_face_record_t faceRec{};
        faceRec.hOuterLoop = loopResult.handle;
        faceRec.normal = faceNormal;
        faceRec.iSourceSide = bFace.iSide;

        const auto faceResult = GenerationPool_Insert(
            &pMeshOut->faces, faceRec );
        if ( faceResult.status != generation_pool_status_t::OK ) {
            return failBuild( GeometryStatus_FromGenerationPoolStatus(
                faceResult.status ) );
        }

        // Link the loop back to its face.
        mesh_loop_record_t *pLoop = GenerationPool_Get(
            &pMeshOut->loops, loopResult.handle );
        if ( pLoop != nullptr ) {
            pLoop->hFace = faceResult.handle;
        }

        if ( iFace == 0u ) {
            hFirstFace = faceResult.handle;
        }
    }

    // Step 3: Pair twin half-edges and create edge records.
    const usize cHalfEdges = ctx.keys.nCount;
    // Track which half-edges already have twins assigned.
    for ( usize i = 0u; i < cHalfEdges; ++i ) {
        mesh_half_edge_record_t *pHe = GenerationPool_Get(
            &pMeshOut->halfEdges, ctx.halfEdgeHandles.pData[i] );
        if ( pHe == nullptr ) {
            return failBuild( geometry_status_t::CORRUPT_STATE );
        }

        // Skip if twin already assigned (by the other half of the pair).
        if ( GenerationHandle_IsValid( pHe->hTwin ) ) {
            continue;
        }

        // Find the opposing half-edge: origin=dest, dest=origin.
        const u32 myOrigin = ctx.keys.pData[i].iOriginSlot;
        const u32 myDest = ctx.keys.pData[i].iDestSlot;
        const geometry_mesh_half_edge_handle_t hTwin =
            FindHalfEdge( &ctx, myDest, myOrigin );

        if ( !GenerationHandle_IsValid( hTwin ) ) {
            // Open boundary — not a closed manifold.
            return failBuild( geometry_status_t::NON_MANIFOLD );
        }

        mesh_half_edge_record_t *pTwin = GenerationPool_Get(
            &pMeshOut->halfEdges, hTwin );
        if ( pTwin == nullptr ) {
            return failBuild( geometry_status_t::CORRUPT_STATE );
        }

        // Link twins.
        pHe->hTwin = hTwin;
        pTwin->hTwin = ctx.halfEdgeHandles.pData[i];

        // Create an edge record for this twin pair.
        mesh_edge_record_t edgeRec{};
        edgeRec.hHalfEdge = ctx.halfEdgeHandles.pData[i];

        const auto edgeResult = GenerationPool_Insert(
            &pMeshOut->edges, edgeRec );
        if ( edgeResult.status != generation_pool_status_t::OK ) {
            return failBuild( GeometryStatus_FromGenerationPoolStatus(
                edgeResult.status ) );
        }

        // Set the edge handle on both half-edges.
        pHe->hEdge = edgeResult.handle;
        pTwin->hEdge = edgeResult.handle;
    }

    // Step 4: Create a single shell covering all faces.
    mesh_shell_record_t shellRec{};
    shellRec.hAnyFace = hFirstFace;
    shellRec.cFaces = static_cast<u32>( cFaces );

    const auto shellResult = GenerationPool_Insert(
        &pMeshOut->shells, shellRec );
    if ( shellResult.status != generation_pool_status_t::OK ) {
        return failBuild( GeometryStatus_FromGenerationPoolStatus(
            shellResult.status ) );
    }

    // Set the shell handle on all faces.
    (void)GenerationPool_ForEach( &pMeshOut->faces,
        [&]( geometry_mesh_face_handle_t /*hFace*/,
             mesh_face_record_t &face ) noexcept -> bool_t {
            face.hShell = shellResult.handle;
            return true;
        } );

    const mesh_validation_result_t validation =
        MeshValidation_Validate( pMeshOut );
    if ( validation.status != geometry_status_t::OK ) {
        return failBuild( validation.status );
    }

    BuilderContext_Shutdown( &ctx );
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
