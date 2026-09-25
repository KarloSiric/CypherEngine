//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshLoopSlide.cpp
//  Purpose: Implements regular closed-loop sliding on a mesh source.
//  Details: Plan construction is staged and published only after every
//           identity, topology, and rail check succeeds. Apply recomputes
//           all targets, revalidates the captured topology/identities, then
//           calls MeshVertices_TryMove once. That call validates every
//           affected face before writing positions or normals.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshLoopSlide.h"

#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshVertexMove.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename tag_t>
bool HandleEqual(
    generation_handle_t<tag_t> a,
    generation_handle_t<tag_t> b ) noexcept
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

const mesh_half_edge_record_t *HalfEdge(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t h ) noexcept
{
    return GenerationPool_Get( &pMesh->halfEdges, h );
}

geometry_mesh_vertex_handle_t Destination(
    const editable_mesh_t *pMesh,
    const mesh_half_edge_record_t &halfEdge ) noexcept
{
    const mesh_half_edge_record_t *pNext =
        HalfEdge( pMesh, halfEdge.hNext );
    return pNext != nullptr
        ? pNext->hOrigin
        : GEOMETRY_HANDLE_INVALID<geometry_mesh_vertex_tag_t>;
}

bool PositionInSourceDomain( math::vec3d_t position ) noexcept
{
    return math::Vec3d_IsFinite( position ) &&
           std::fabs( position.x ) <= kMeshSourceCoordinateMax &&
           std::fabs( position.y ) <= kMeshSourceCoordinateMax &&
           std::fabs( position.z ) <= kMeshSourceCoordinateMax;
}

geometry_status_t DirectedHalfEdge(
    const mesh_source_t *pSource,
    geometry_mesh_vertex_handle_t hA,
    geometry_mesh_vertex_handle_t hB,
    geometry_mesh_edge_handle_t hEdge,
    geometry_mesh_half_edge_handle_t *pOut ) noexcept
{
    const editable_mesh_t *pMesh = &pSource->mesh;
    const mesh_edge_record_t *pEdge =
        GenerationPool_Get( &pMesh->edges, hEdge );
    const mesh_half_edge_record_t *pRecorded = pEdge != nullptr
        ? HalfEdge( pMesh, pEdge->hHalfEdge )
        : nullptr;
    if ( pRecorded == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    const geometry_mesh_vertex_handle_t hRecordedDestination =
        Destination( pMesh, *pRecorded );
    if ( HandleEqual( pRecorded->hOrigin, hA ) &&
         HandleEqual( hRecordedDestination, hB ) ) {
        *pOut = pEdge->hHalfEdge;
        return geometry_status_t::OK;
    }
    if ( !GeometryHandle_IsValid( pRecorded->hTwin ) ) {
        return geometry_status_t::UNSUPPORTED;
    }
    const mesh_half_edge_record_t *pTwin =
        HalfEdge( pMesh, pRecorded->hTwin );
    if ( pTwin == nullptr ||
         !HandleEqual( pTwin->hTwin, pEdge->hHalfEdge ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( HandleEqual( pTwin->hOrigin, hA ) &&
         HandleEqual( Destination( pMesh, *pTwin ), hB ) ) {
        *pOut = pRecorded->hTwin;
        return geometry_status_t::OK;
    }
    return geometry_status_t::CORRUPT_STATE;
}

geometry_status_t RailVertices(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t hLoop,
    geometry_mesh_vertex_handle_t *pPositiveOut,
    geometry_mesh_vertex_handle_t *pNegativeOut ) noexcept
{
    const mesh_half_edge_record_t *pH = HalfEdge( pMesh, hLoop );
    const mesh_half_edge_record_t *pPrevious =
        pH != nullptr ? HalfEdge( pMesh, pH->hPrev ) : nullptr;
    const mesh_half_edge_record_t *pTwin =
        pH != nullptr ? HalfEdge( pMesh, pH->hTwin ) : nullptr;
    const mesh_half_edge_record_t *pNegative =
        pTwin != nullptr ? HalfEdge( pMesh, pTwin->hNext ) : nullptr;
    if ( pH == nullptr || pPrevious == nullptr || pTwin == nullptr ||
         pNegative == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    const geometry_mesh_vertex_handle_t hNegative =
        Destination( pMesh, *pNegative );
    if ( GenerationPool_Get( &pMesh->vertices, pPrevious->hOrigin ) == nullptr ||
         GenerationPool_Get( &pMesh->vertices, hNegative ) == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    *pPositiveOut = pPrevious->hOrigin;
    *pNegativeOut = hNegative;
    return geometry_status_t::OK;
}

geometry_status_t ValidateRailAgreement(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t hPreviousLoop,
    geometry_mesh_vertex_handle_t hPositive,
    geometry_mesh_vertex_handle_t hNegative ) noexcept
{
    const mesh_half_edge_record_t *pPrevious =
        HalfEdge( pMesh, hPreviousLoop );
    const mesh_half_edge_record_t *pPreviousPositive = pPrevious != nullptr
        ? HalfEdge( pMesh, pPrevious->hNext )
        : nullptr;
    const mesh_half_edge_record_t *pPreviousTwin = pPrevious != nullptr
        ? HalfEdge( pMesh, pPrevious->hTwin )
        : nullptr;
    const mesh_half_edge_record_t *pPreviousNegative =
        pPreviousTwin != nullptr
            ? HalfEdge( pMesh, pPreviousTwin->hPrev )
            : nullptr;
    if ( pPreviousPositive == nullptr || pPreviousNegative == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !HandleEqual( Destination( pMesh, *pPreviousPositive ), hPositive ) ||
         !HandleEqual( pPreviousNegative->hOrigin, hNegative ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

void PublishPlan(
    mesh_loop_slide_plan_t *pDestination,
    mesh_loop_slide_plan_t *pStaged ) noexcept
{
    MeshLoopSlidePlan_Shutdown( pDestination );
    pDestination->pAllocator = pStaged->pAllocator;
    pDestination->meshId = pStaged->meshId;
    pDestination->directedSeedA = pStaged->directedSeedA;
    pDestination->directedSeedB = pStaged->directedSeedB;
    pDestination->bClosed = pStaged->bClosed;
    Vector_Move( &pDestination->samples, &pStaged->samples );
    Vector_Move( &pDestination->moveHandles, &pStaged->moveHandles );
    Vector_Move( &pDestination->targetPositions, &pStaged->targetPositions );
    Vector_Move(
        &pDestination->candidatePositions,
        &pStaged->candidatePositions );
    pStaged->pAllocator = nullptr;
    pStaged->meshId = GEOMETRY_SOURCE_ID_INVALID;
    pStaged->directedSeedA = GEOMETRY_SOURCE_ID_INVALID;
    pStaged->directedSeedB = GEOMETRY_SOURCE_ID_INVALID;
    pStaged->bClosed = false;
}

void ResetResult( mesh_loop_slide_result_t *pResult ) noexcept
{
    if ( pResult != nullptr ) { *pResult = {}; }
}

geometry_status_t ValidateFactor( f64 factor ) noexcept
{
    if ( !std::isfinite( factor ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( factor <= -1.0 || factor >= 1.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t MeshLoopSlidePlan_Init(
    mesh_loop_slide_plan_t *pPlan,
    const allocator_t *pAllocator ) noexcept
{
    if ( pPlan == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pPlan->pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !Vector_Init( &pPlan->samples, pAllocator ) ||
         !Vector_Init( &pPlan->moveHandles, pAllocator ) ||
         !Vector_Init( &pPlan->targetPositions, pAllocator ) ||
         !Vector_Init( &pPlan->candidatePositions, pAllocator ) ) {
        Vector_Shutdown( &pPlan->candidatePositions );
        Vector_Shutdown( &pPlan->targetPositions );
        Vector_Shutdown( &pPlan->moveHandles );
        Vector_Shutdown( &pPlan->samples );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pPlan->pAllocator = pAllocator;
    pPlan->meshId = GEOMETRY_SOURCE_ID_INVALID;
    pPlan->directedSeedA = GEOMETRY_SOURCE_ID_INVALID;
    pPlan->directedSeedB = GEOMETRY_SOURCE_ID_INVALID;
    pPlan->bClosed = false;
    return geometry_status_t::OK;
}

void MeshLoopSlidePlan_Shutdown(
    mesh_loop_slide_plan_t *pPlan ) noexcept
{
    if ( pPlan == nullptr ) { return; }
    if ( pPlan->pAllocator != nullptr ) {
        Vector_Shutdown( &pPlan->candidatePositions );
        Vector_Shutdown( &pPlan->targetPositions );
        Vector_Shutdown( &pPlan->moveHandles );
        Vector_Shutdown( &pPlan->samples );
    }
    pPlan->pAllocator = nullptr;
    pPlan->meshId = GEOMETRY_SOURCE_ID_INVALID;
    pPlan->directedSeedA = GEOMETRY_SOURCE_ID_INVALID;
    pPlan->directedSeedB = GEOMETRY_SOURCE_ID_INVALID;
    pPlan->bClosed = false;
}

bool MeshLoopSlidePlan_IsInitialized(
    const mesh_loop_slide_plan_t *pPlan ) noexcept
{
    return pPlan != nullptr &&
           Allocator_IsValid( pPlan->pAllocator ) &&
           pPlan->samples.pAllocator == pPlan->pAllocator &&
           pPlan->moveHandles.pAllocator == pPlan->pAllocator &&
           pPlan->targetPositions.pAllocator == pPlan->pAllocator &&
           pPlan->candidatePositions.pAllocator == pPlan->pAllocator &&
           Vector_IsValid( &pPlan->samples ) &&
           Vector_IsValid( &pPlan->moveHandles ) &&
           Vector_IsValid( &pPlan->targetPositions ) &&
           Vector_IsValid( &pPlan->candidatePositions );
}

geometry_status_t MeshSourceLoopSlidePlan_TryBuild(
    mesh_loop_slide_plan_t *pPlan,
    const mesh_source_t *pSource,
    geometry_source_id_t directedVertexA,
    geometry_source_id_t directedVertexB ) noexcept
{
    if ( !MeshLoopSlidePlan_IsInitialized( pPlan ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !MeshSource_IsInitialized( pSource ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( directedVertexA ) ||
         !GeometrySourceId_IsValid( directedVertexB ) ||
         directedVertexA.value == directedVertexB.value ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    geometry_mesh_vertex_handle_t hA{};
    geometry_mesh_vertex_handle_t hB{};
    geometry_mesh_edge_handle_t hEdge{};
    if ( !MeshSource_TryFindVertex( pSource, directedVertexA, &hA ) ||
         !MeshSource_TryFindVertex( pSource, directedVertexB, &hB ) ||
         !MeshSourceEdit_TryFindEdge(
             pSource, directedVertexA, directedVertexB, &hEdge ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }

    geometry_mesh_half_edge_handle_t hDirected{};
    geometry_status_t status = DirectedHalfEdge(
        pSource, hA, hB, hEdge, &hDirected );
    if ( status != geometry_status_t::OK ) { return status; }

    mesh_regular_edge_loop_t loop{};
    status = MeshRegularEdgeLoop_Init( &loop, pPlan->pAllocator );
    if ( status != geometry_status_t::OK ) { return status; }
    status = MeshOps_TryTraceRegularClosedEdgeLoop(
        &pSource->mesh, hDirected, &loop );
    if ( status != geometry_status_t::OK ) {
        MeshRegularEdgeLoop_Shutdown( &loop );
        return status;
    }

    mesh_loop_slide_plan_t staged{};
    status = MeshLoopSlidePlan_Init( &staged, pPlan->pAllocator );
    if ( status != geometry_status_t::OK ) {
        MeshRegularEdgeLoop_Shutdown( &loop );
        return status;
    }
    auto cleanup = [&]() noexcept {
        MeshLoopSlidePlan_Shutdown( &staged );
        MeshRegularEdgeLoop_Shutdown( &loop );
    };
    auto fail = [&]( geometry_status_t result ) noexcept {
        cleanup();
        return result;
    };

    const usize cLoop = loop.directedHalfEdges.nCount;
    if ( !Vector_Reserve( &staged.samples, cLoop ) ||
         !Vector_Reserve( &staged.moveHandles, cLoop ) ||
         !Vector_Resize( &staged.targetPositions, cLoop ) ||
         !Vector_Resize( &staged.candidatePositions, cLoop ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }

    for ( usize i = 0u; i < cLoop; ++i ) {
        const geometry_mesh_half_edge_handle_t h =
            loop.directedHalfEdges.pData[i];
        const mesh_half_edge_record_t *pH = HalfEdge( &pSource->mesh, h );
        if ( pH == nullptr ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        geometry_mesh_vertex_handle_t hPositive{};
        geometry_mesh_vertex_handle_t hNegative{};
        status = RailVertices(
            &pSource->mesh, h, &hPositive, &hNegative );
        if ( status != geometry_status_t::OK ) { return fail( status ); }

        const geometry_mesh_half_edge_handle_t hPrevious =
            loop.directedHalfEdges.pData[( i + cLoop - 1u ) % cLoop];
        status = ValidateRailAgreement(
            &pSource->mesh, hPrevious, hPositive, hNegative );
        if ( status != geometry_status_t::OK ) { return fail( status ); }

        const mesh_vertex_record_t *pVertex =
            GenerationPool_Get( &pSource->mesh.vertices, pH->hOrigin );
        const mesh_vertex_record_t *pPositive =
            GenerationPool_Get( &pSource->mesh.vertices, hPositive );
        const mesh_vertex_record_t *pNegative =
            GenerationPool_Get( &pSource->mesh.vertices, hNegative );
        if ( pVertex == nullptr || pPositive == nullptr || pNegative == nullptr ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        mesh_loop_slide_sample_t sample{};
        sample.hDirectedHalfEdge = h;
        sample.hVertex = pH->hOrigin;
        sample.hPositiveRailVertex = hPositive;
        sample.hNegativeRailVertex = hNegative;
        sample.vertexId = MeshSource_VertexId( pSource, sample.hVertex );
        sample.positiveRailVertexId =
            MeshSource_VertexId( pSource, hPositive );
        sample.negativeRailVertexId =
            MeshSource_VertexId( pSource, hNegative );
        if ( !GeometrySourceId_IsValid( sample.vertexId ) ||
             !GeometrySourceId_IsValid( sample.positiveRailVertexId ) ||
             !GeometrySourceId_IsValid( sample.negativeRailVertexId ) ) {
            return fail( geometry_status_t::CORRUPT_STATE );
        }
        sample.baselinePosition = pVertex->position;
        sample.positiveRailPosition = pPositive->position;
        sample.negativeRailPosition = pNegative->position;
        if ( !PositionInSourceDomain( sample.baselinePosition ) ||
             !PositionInSourceDomain( sample.positiveRailPosition ) ||
             !PositionInSourceDomain( sample.negativeRailPosition ) ) {
            return fail( geometry_status_t::NUMERIC_FAILURE );
        }
        if ( !Vector_PushBack( &staged.samples, sample ) ||
             !Vector_PushBack( &staged.moveHandles, sample.hVertex ) ) {
            return fail( geometry_status_t::ALLOCATION_FAILED );
        }
        staged.targetPositions.pData[i] = sample.baselinePosition;
    }

    staged.meshId = pSource->sourceId;
    staged.directedSeedA = directedVertexA;
    staged.directedSeedB = directedVertexB;
    staged.bClosed = loop.bClosed;
    MeshRegularEdgeLoop_Shutdown( &loop );
    PublishPlan( pPlan, &staged );
    MeshLoopSlidePlan_Shutdown( &staged );
    return geometry_status_t::OK;
}

geometry_status_t MeshSourceLoopSlidePlan_TryApply(
    mesh_loop_slide_plan_t *pPlan,
    mesh_source_t *pSource,
    f64 factor,
    mesh_loop_slide_result_t *pResultOut ) noexcept
{
    ResetResult( pResultOut );
    if ( !MeshLoopSlidePlan_IsInitialized( pPlan ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !MeshSource_IsInitialized( pSource ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    geometry_status_t status = ValidateFactor( factor );
    if ( status != geometry_status_t::OK ) { return status; }
    if ( pSource->sourceId.value != pPlan->meshId.value ||
         !pPlan->bClosed ) {
        return geometry_status_t::STALE_HANDLE;
    }
    const usize cLoop = pPlan->samples.nCount;
    if ( cLoop < 3u || pPlan->moveHandles.nCount != cLoop ||
         pPlan->targetPositions.nCount != cLoop ||
         pPlan->candidatePositions.nCount != cLoop ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    u32 cChanged = 0u;
    for ( usize i = 0u; i < cLoop; ++i ) {
        const mesh_loop_slide_sample_t &sample = pPlan->samples.pData[i];
        const mesh_half_edge_record_t *pH =
            HalfEdge( &pSource->mesh, sample.hDirectedHalfEdge );
        const mesh_vertex_record_t *pVertex =
            GenerationPool_Get( &pSource->mesh.vertices, sample.hVertex );
        const mesh_loop_slide_sample_t &next =
            pPlan->samples.pData[( i + 1u ) % cLoop];
        if ( pH == nullptr || pVertex == nullptr ||
             !HandleEqual( pH->hOrigin, sample.hVertex ) ||
             !HandleEqual( Destination( &pSource->mesh, *pH ), next.hVertex ) ||
             MeshSource_VertexId( pSource, sample.hVertex ).value !=
                 sample.vertexId.value ) {
            return geometry_status_t::STALE_HANDLE;
        }
        if ( !math::Vec3d_EqualsExact(
                 pVertex->position,
                 pPlan->targetPositions.pData[i] ) ) {
            return geometry_status_t::STALE_HANDLE;
        }
        geometry_mesh_vertex_handle_t hPositive{};
        geometry_mesh_vertex_handle_t hNegative{};
        status = RailVertices(
            &pSource->mesh,
            sample.hDirectedHalfEdge,
            &hPositive,
            &hNegative );
        if ( status != geometry_status_t::OK ) { return status; }
        if ( !HandleEqual( hPositive, sample.hPositiveRailVertex ) ||
             !HandleEqual( hNegative, sample.hNegativeRailVertex ) ||
             MeshSource_VertexId( pSource, hPositive ).value !=
                 sample.positiveRailVertexId.value ||
             MeshSource_VertexId( pSource, hNegative ).value !=
                 sample.negativeRailVertexId.value ) {
            return geometry_status_t::STALE_HANDLE;
        }
        const mesh_vertex_record_t *pPositive =
            GenerationPool_Get( &pSource->mesh.vertices, hPositive );
        const mesh_vertex_record_t *pNegative =
            GenerationPool_Get( &pSource->mesh.vertices, hNegative );
        if ( pPositive == nullptr || pNegative == nullptr ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        // Selected loop vertices are expected to differ from the baseline
        // during preview. Rail vertices are not part of that preview; if a
        // concurrent edit moved either rail, the captured parameterization
        // is stale and must be rebuilt rather than silently overwriting from
        // old coordinates.
        if ( !math::Vec3d_EqualsExact(
                 pPositive->position,
                 sample.positiveRailPosition ) ||
             !math::Vec3d_EqualsExact(
                 pNegative->position,
                 sample.negativeRailPosition ) ) {
            return geometry_status_t::STALE_HANDLE;
        }

        const math::vec3d_t target = factor >= 0.0
            ? math::Vec3d_Lerp(
                  sample.baselinePosition,
                  sample.positiveRailPosition,
                  factor )
            : math::Vec3d_Lerp(
                  sample.baselinePosition,
                  sample.negativeRailPosition,
                  -factor );
        if ( !PositionInSourceDomain( target ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        pPlan->candidatePositions.pData[i] = target;
        if ( !math::Vec3d_EqualsExact( pVertex->position, target ) ) {
            ++cChanged;
        }
    }

    if ( cChanged == 0u ) {
        if ( pResultOut != nullptr ) { pResultOut->bClosed = true; }
        return geometry_status_t::OK;
    }

    u32 cFacesUpdated = 0u;
    status = MeshVertices_TryMove(
        &pSource->mesh,
        span_t<const geometry_mesh_vertex_handle_t>{
            pPlan->moveHandles.pData,
            pPlan->moveHandles.nCount },
        span_t<const math::vec3d_t>{
            pPlan->candidatePositions.pData,
            pPlan->candidatePositions.nCount },
        &cFacesUpdated );
    if ( status != geometry_status_t::OK ) { return status; }
    for ( usize i = 0u; i < cLoop; ++i ) {
        pPlan->targetPositions.pData[i] =
            pPlan->candidatePositions.pData[i];
    }
    if ( pResultOut != nullptr ) {
        pResultOut->cVerticesMoved = cChanged;
        pResultOut->cFacesUpdated = cFacesUpdated;
        pResultOut->bClosed = true;
    }
    return geometry_status_t::OK;
}

geometry_status_t MeshSourceEdit_TrySlideEdgeLoop(
    mesh_source_t *pSource,
    geometry_source_id_t directedVertexA,
    geometry_source_id_t directedVertexB,
    f64 factor,
    mesh_loop_slide_result_t *pResultOut ) noexcept
{
    ResetResult( pResultOut );
    if ( !MeshSource_IsInitialized( pSource ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    geometry_status_t status = ValidateFactor( factor );
    if ( status != geometry_status_t::OK ) { return status; }

    mesh_loop_slide_plan_t plan{};
    status = MeshLoopSlidePlan_Init( &plan, pSource->mesh.pAllocator );
    if ( status == geometry_status_t::OK ) {
        status = MeshSourceLoopSlidePlan_TryBuild(
            &plan, pSource, directedVertexA, directedVertexB );
    }
    if ( status == geometry_status_t::OK ) {
        status = MeshSourceLoopSlidePlan_TryApply(
            &plan, pSource, factor, pResultOut );
    }
    MeshLoopSlidePlan_Shutdown( &plan );
    return status;
}

} // namespace cypher::editor::geometry
