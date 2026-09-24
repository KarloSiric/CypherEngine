//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshLoopTraversal.cpp
//  Purpose: Implements regular closed edge-loop traversal.
//  Details: At the destination of directed loop half-edge h, next(h) is the
//           positive-side rail. Crossing that rail and advancing once gives
//           the next directed loop edge:
//
//               next( twin( next( h ) ) )
//
//           Slot masks make repeat detection linear and remove the old
//           fixed 256-edge ceiling.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshLoopTraversal.h"

#include "CypherGeometry_MeshRecordAccess.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace mesh_detail;

namespace
{

template <typename tag_t>
bool HandleEqual(
    generation_handle_t<tag_t> a,
    generation_handle_t<tag_t> b ) noexcept
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

bool FaceIsLiveQuad(
    const editable_mesh_t *pMesh,
    const mesh_half_edge_record_t &halfEdge,
    bool *pbQuadOut ) noexcept
{
    *pbQuadOut = false;
    const mesh_loop_record_t *pLoop =
        GenerationPool_Get( &pMesh->loops, halfEdge.hLoop );
    if ( pLoop == nullptr ||
         GenerationPool_Get( &pMesh->faces, pLoop->hFace ) == nullptr ) {
        return false;
    }
    *pbQuadOut = pLoop->cHalfEdges == 4u;
    return true;
}

geometry_status_t RailVertices(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t hLoop,
    geometry_mesh_vertex_handle_t *pPositiveOut,
    geometry_mesh_vertex_handle_t *pNegativeOut ) noexcept
{
    const mesh_half_edge_record_t *pH = He( pMesh, hLoop );
    const mesh_half_edge_record_t *pPrevious =
        pH != nullptr ? He( pMesh, pH->hPrev ) : nullptr;
    const mesh_half_edge_record_t *pTwin =
        pH != nullptr ? He( pMesh, pH->hTwin ) : nullptr;
    const mesh_half_edge_record_t *pNegative =
        pTwin != nullptr ? He( pMesh, pTwin->hNext ) : nullptr;
    if ( pH == nullptr || pPrevious == nullptr || pTwin == nullptr ||
         pNegative == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    const geometry_mesh_vertex_handle_t negative =
        DestOf( pMesh, *pNegative );
    if ( GenerationPool_Get( &pMesh->vertices, pPrevious->hOrigin ) == nullptr ||
         GenerationPool_Get( &pMesh->vertices, negative ) == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    *pPositiveOut = pPrevious->hOrigin;
    *pNegativeOut = negative;
    return geometry_status_t::OK;
}

void PublishLoop(
    mesh_regular_edge_loop_t *pDestination,
    mesh_regular_edge_loop_t *pStaged ) noexcept
{
    MeshRegularEdgeLoop_Shutdown( pDestination );
    pDestination->pAllocator = pStaged->pAllocator;
    pDestination->bClosed = pStaged->bClosed;
    Vector_Move(
        &pDestination->directedHalfEdges,
        &pStaged->directedHalfEdges );
    pStaged->pAllocator = nullptr;
    pStaged->bClosed = false;
}

} // namespace

geometry_status_t MeshRegularEdgeLoop_Init(
    mesh_regular_edge_loop_t *pLoop,
    const allocator_t *pAllocator ) noexcept
{
    if ( pLoop == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pLoop->pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !Vector_Init( &pLoop->directedHalfEdges, pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pLoop->pAllocator = pAllocator;
    pLoop->bClosed = false;
    return geometry_status_t::OK;
}

void MeshRegularEdgeLoop_Shutdown(
    mesh_regular_edge_loop_t *pLoop ) noexcept
{
    if ( pLoop == nullptr ) { return; }
    if ( pLoop->pAllocator != nullptr ) {
        Vector_Shutdown( &pLoop->directedHalfEdges );
    }
    pLoop->pAllocator = nullptr;
    pLoop->bClosed = false;
}

bool MeshRegularEdgeLoop_IsInitialized(
    const mesh_regular_edge_loop_t *pLoop ) noexcept
{
    return pLoop != nullptr &&
           Allocator_IsValid( pLoop->pAllocator ) &&
           pLoop->directedHalfEdges.pAllocator == pLoop->pAllocator &&
           Vector_IsValid( &pLoop->directedHalfEdges );
}

geometry_status_t MeshOps_TryTraceRegularClosedEdgeLoop(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t hDirectedSeed,
    mesh_regular_edge_loop_t *pLoopOut ) noexcept
{
    if ( !MeshRegularEdgeLoop_IsInitialized( pLoopOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( He( pMesh, hDirectedSeed ) == nullptr ) {
        return geometry_status_t::INVALID_HANDLE;
    }

    mesh_regular_edge_loop_t staged{};
    geometry_status_t status =
        MeshRegularEdgeLoop_Init( &staged, pLoopOut->pAllocator );
    if ( status != geometry_status_t::OK ) { return status; }

    vector_t<u8> visitedEdges{};
    vector_t<u8> visitedVertices{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &visitedEdges );
        Vector_Shutdown( &visitedVertices );
        MeshRegularEdgeLoop_Shutdown( &staged );
    };
    auto fail = [&]( geometry_status_t result ) noexcept {
        cleanup();
        return result;
    };

    if ( !Vector_Init( &visitedEdges, pLoopOut->pAllocator ) ||
         !Vector_Init( &visitedVertices, pLoopOut->pAllocator ) ||
         !Vector_Resize( &visitedEdges, pMesh->edges.cSlots ) ||
         !Vector_Resize( &visitedVertices, pMesh->vertices.cSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < visitedEdges.nCount; ++i ) {
        visitedEdges.pData[i] = 0u;
    }
    for ( usize i = 0u; i < visitedVertices.nCount; ++i ) {
        visitedVertices.pData[i] = 0u;
    }

    geometry_mesh_half_edge_handle_t hCurrent = hDirectedSeed;
    while ( true ) {
        const mesh_half_edge_record_t *pCurrent = He( pMesh, hCurrent );
        if ( pCurrent == nullptr ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        const mesh_vertex_record_t *pOrigin =
            GenerationPool_Get( &pMesh->vertices, pCurrent->hOrigin );
        const geometry_mesh_vertex_handle_t hDestination =
            DestOf( pMesh, *pCurrent );
        const mesh_vertex_record_t *pDestination =
            GenerationPool_Get( &pMesh->vertices, hDestination );
        const mesh_edge_record_t *pEdge =
            GenerationPool_Get( &pMesh->edges, pCurrent->hEdge );
        if ( pOrigin == nullptr || pDestination == nullptr || pEdge == nullptr ||
             pCurrent->hEdge.nSlot >= visitedEdges.nCount ||
             pCurrent->hOrigin.nSlot >= visitedVertices.nCount ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        if ( visitedEdges.pData[pCurrent->hEdge.nSlot] != 0u ||
             visitedVertices.pData[pCurrent->hOrigin.nSlot] != 0u ) {
            return fail( geometry_status_t::UNSUPPORTED );
        }

        if ( !GeometryHandle_IsValid( pCurrent->hTwin ) ) {
            return fail( geometry_status_t::UNSUPPORTED );
        }
        const mesh_half_edge_record_t *pTwin = He( pMesh, pCurrent->hTwin );
        if ( pTwin == nullptr || !HandleEqual( pTwin->hTwin, hCurrent ) ||
             !HandleEqual( pTwin->hEdge, pCurrent->hEdge ) ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        const mesh_half_edge_record_t *pRecorded = He( pMesh, pEdge->hHalfEdge );
        if ( pRecorded == nullptr ||
             !HandleEqual( pRecorded->hEdge, pCurrent->hEdge ) ||
             ( !HandleEqual( pEdge->hHalfEdge, hCurrent ) &&
               !HandleEqual( pEdge->hHalfEdge, pCurrent->hTwin ) ) ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }

        bool bLeftQuad = false;
        bool bRightQuad = false;
        if ( !FaceIsLiveQuad( pMesh, *pCurrent, &bLeftQuad ) ||
             !FaceIsLiveQuad( pMesh, *pTwin, &bRightQuad ) ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        if ( !bLeftQuad || !bRightQuad ||
             EditableMesh_VertexValence(
                 pMesh, pCurrent->hOrigin ) != 4u ) {
            return fail( geometry_status_t::UNSUPPORTED );
        }

        visitedEdges.pData[pCurrent->hEdge.nSlot] = 1u;
        visitedVertices.pData[pCurrent->hOrigin.nSlot] = 1u;
        if ( !Vector_PushBack( &staged.directedHalfEdges, hCurrent ) ) {
            return fail( geometry_status_t::ALLOCATION_FAILED );
        }

        // The positive rail leaves the current destination in the current
        // (left) face. Cross it, then advance once around the adjacent quad
        // to obtain the next loop half-edge, also leaving that destination.
        const mesh_half_edge_record_t *pPositiveRail =
            He( pMesh, pCurrent->hNext );
        if ( pPositiveRail == nullptr ||
             !HandleEqual( pPositiveRail->hOrigin, hDestination ) ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        if ( !GeometryHandle_IsValid( pPositiveRail->hTwin ) ) {
            return fail( geometry_status_t::UNSUPPORTED );
        }
        const mesh_half_edge_record_t *pAcrossRail =
            He( pMesh, pPositiveRail->hTwin );
        if ( pAcrossRail == nullptr ||
             !HandleEqual( pAcrossRail->hTwin, pCurrent->hNext ) ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        const geometry_mesh_half_edge_handle_t hNext =
            pAcrossRail->hNext;
        const mesh_half_edge_record_t *pNext = He( pMesh, hNext );
        if ( pNext == nullptr ||
             !HandleEqual( pNext->hOrigin, hDestination ) ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }

        if ( HandleEqual( hNext, hDirectedSeed ) ) {
            break;
        }
        if ( HandleEqual( pNext->hEdge, He( pMesh, hDirectedSeed )->hEdge ) ) {
            return fail( geometry_status_t::UNSUPPORTED );
        }
        hCurrent = hNext;
    }

    if ( staged.directedHalfEdges.nCount < 3u ) {
        return fail( geometry_status_t::UNSUPPORTED );
    }

    // A slide rail may not re-enter the selected loop, and each rail segment
    // must have a finite nonzero length. Those restrictions exclude folded
    // local parameterizations from this deliberately conservative slice.
    for ( usize i = 0u; i < staged.directedHalfEdges.nCount; ++i ) {
        const geometry_mesh_half_edge_handle_t h =
            staged.directedHalfEdges.pData[i];
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        geometry_mesh_vertex_handle_t hPositive{};
        geometry_mesh_vertex_handle_t hNegative{};
        status = RailVertices(
            pMesh, h, &hPositive, &hNegative );
        if ( status != geometry_status_t::OK ) { return fail( status ); }
        if ( hPositive.nSlot >= visitedVertices.nCount ||
             hNegative.nSlot >= visitedVertices.nCount ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        if ( visitedVertices.pData[hPositive.nSlot] != 0u ||
             visitedVertices.pData[hNegative.nSlot] != 0u ||
             HandleEqual( hPositive, hNegative ) ) {
            return fail( geometry_status_t::UNSUPPORTED );
        }
        const mesh_vertex_record_t *pV =
            GenerationPool_Get( &pMesh->vertices, pH->hOrigin );
        const mesh_vertex_record_t *pPositive =
            GenerationPool_Get( &pMesh->vertices, hPositive );
        const mesh_vertex_record_t *pNegative =
            GenerationPool_Get( &pMesh->vertices, hNegative );
        if ( pV == nullptr || pPositive == nullptr || pNegative == nullptr ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        const f64 positiveLengthSquared = math::Vec3d_DistanceSquared(
            pV->position, pPositive->position );
        const f64 negativeLengthSquared = math::Vec3d_DistanceSquared(
            pV->position, pNegative->position );
        if ( !std::isfinite( positiveLengthSquared ) ||
             !std::isfinite( negativeLengthSquared ) ) {
            return fail( geometry_status_t::NUMERIC_FAILURE );
        }
        if ( !( positiveLengthSquared > 0.0 ) ||
             !( negativeLengthSquared > 0.0 ) ) {
            return fail( geometry_status_t::DEGENERATE );
        }
    }

    staged.bClosed = true;
    Vector_Shutdown( &visitedEdges );
    Vector_Shutdown( &visitedVertices );
    PublishLoop( pLoopOut, &staged );
    MeshRegularEdgeLoop_Shutdown( &staged );
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
