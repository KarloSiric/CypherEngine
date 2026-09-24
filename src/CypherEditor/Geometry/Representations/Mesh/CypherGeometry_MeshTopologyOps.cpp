//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshTopologyOps.cpp
//  Purpose: Implements topology-editing operations on the editable mesh.
//  Details: Each operation directly mutates the pool records of the mesh.
//           Where an operation adds or removes elements, all affected
//           pointers (twin, next, prev, loop, face, shell) are patched so
//           the mesh stays a valid oriented two-manifold throughout.
//
//           Helper lambdas marked noexcept → bool_t return true to
//           continue pool iteration, matching GenerationPool_ForEach
//           requirements.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshTopologyOps.h"
#include "CypherCommon_Vector.h"

#include <cmath>
#include <new>
#include <type_traits>

namespace cypher::editor::geometry
{

using namespace cypher::common;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

template <typename tag_t>
bool HandlesEqual(
    common::generation_handle_t<tag_t> a,
    common::generation_handle_t<tag_t> b ) noexcept
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

// A retired slot can never be reused, so an operation that needs N new
// records must reserve records + retired slots + N total slots. Calling
// Reserve even when the current capacity is already sufficient also performs
// the pool's complete structural audit before the first insertion.
template <typename record_t, typename tag_t>
geometry_status_t ReserveAdditionalRecords(
    common::generation_pool_t<record_t, tag_t> *pPool,
    usize cAdditions ) noexcept
{
    if ( !GenerationPool_IsInitialized( pPool ) ) {
        return GeometryStatus_FromGenerationPoolStatus(
            GenerationPool_Reserve( pPool, 0u ) );
    }

    if ( pPool->cRetiredSlots > CY_USIZE_MAX - pPool->cRecords ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const usize cUnavailable =
        pPool->cRecords + pPool->cRetiredSlots;
    if ( cAdditions > CY_USIZE_MAX - cUnavailable ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const usize cRequiredSlots = cUnavailable + cAdditions;
    if ( cRequiredSlots > pPool->cSlotLimit ||
         cRequiredSlots > CY_GENERATION_POOL_MAX_CAPACITY ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const generation_pool_status_t reserveStatus =
        GenerationPool_Reserve( pPool, cRequiredSlots );
    if ( reserveStatus != generation_pool_status_t::OK ) {
        return GeometryStatus_FromGenerationPoolStatus( reserveStatus );
    }

    const usize cReusableSlots =
        pPool->cSlots - pPool->cRecords - pPool->cRetiredSlots;
    return cReusableSlots >= cAdditions
        ? geometry_status_t::OK
        : geometry_status_t::CORRUPT_STATE;
}

// Validates the local records a split will later traverse. The declared loop
// count is first bounded by the number of live half-edges, so corrupt counts
// cannot turn validation into an effectively unbounded walk.
bool ValidateLoopForSplit(
    const editable_mesh_t *pMesh,
    geometry_mesh_loop_handle_t hLoop,
    geometry_mesh_face_handle_t hExpectedFace ) noexcept
{
    const mesh_loop_record_t *pLoop =
        GenerationPool_Get( &pMesh->loops, hLoop );
    if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ||
         static_cast<usize>( pLoop->cHalfEdges ) >
             pMesh->halfEdges.cRecords ) {
        return false;
    }

    if ( !HandlesEqual( pLoop->hFace, hExpectedFace ) ) {
        return false;
    }
    const mesh_face_record_t *pFace =
        GenerationPool_Get( &pMesh->faces, hExpectedFace );
    if ( pFace == nullptr || !HandlesEqual( pFace->hOuterLoop, hLoop ) ) {
        return false;
    }

    const geometry_mesh_half_edge_handle_t hFirst =
        pLoop->hFirstHalfEdge;
    geometry_mesh_half_edge_handle_t hCurrent = hFirst;
    for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *pCurrent =
            GenerationPool_Get( &pMesh->halfEdges, hCurrent );
        if ( pCurrent == nullptr ||
             !HandlesEqual( pCurrent->hLoop, hLoop ) ||
             GenerationPool_Get(
                 &pMesh->vertices, pCurrent->hOrigin ) == nullptr ||
             GenerationPool_Get(
                 &pMesh->edges, pCurrent->hEdge ) == nullptr ) {
            return false;
        }

        const mesh_half_edge_record_t *pNext =
            GenerationPool_Get( &pMesh->halfEdges, pCurrent->hNext );
        const mesh_half_edge_record_t *pPrev =
            GenerationPool_Get( &pMesh->halfEdges, pCurrent->hPrev );
        if ( pNext == nullptr || pPrev == nullptr ||
             !HandlesEqual( pNext->hPrev, hCurrent ) ||
             !HandlesEqual( pPrev->hNext, hCurrent ) ) {
            return false;
        }

        hCurrent = pCurrent->hNext;
        if ( i + 1u < pLoop->cHalfEdges &&
             HandlesEqual( hCurrent, hFirst ) ) {
            return false;
        }
    }

    return HandlesEqual( hCurrent, hFirst );
}

// Recomputes a face's outward normal from its loop vertices using
// Newell's method (robust for non-planar polygons).
void RecomputeFaceNormal(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace ) noexcept
{
    mesh_face_record_t *pFace =
        GenerationPool_Get( &pMesh->faces, hFace );
    if ( pFace == nullptr ) { return; }

    const mesh_loop_record_t *pLoop =
        GenerationPool_Get( &pMesh->loops, pFace->hOuterLoop );
    if ( pLoop == nullptr ) { return; }

    math::vec3d_t normal = math::Vec3d_Make( 0.0, 0.0, 0.0 );

    geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
    for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *pCur =
            GenerationPool_Get( &pMesh->halfEdges, hCur );
        if ( pCur == nullptr ) { return; }

        const mesh_half_edge_record_t *pNext =
            GenerationPool_Get( &pMesh->halfEdges, pCur->hNext );
        if ( pNext == nullptr ) { return; }

        const mesh_vertex_record_t *pV0 =
            GenerationPool_Get( &pMesh->vertices, pCur->hOrigin );
        const mesh_vertex_record_t *pV1 =
            GenerationPool_Get( &pMesh->vertices, pNext->hOrigin );
        if ( pV0 == nullptr || pV1 == nullptr ) { return; }

        // Newell's method: accumulate cross products of consecutive
        // edge projections.
        normal.x += ( pV0->position.y - pV1->position.y ) *
                    ( pV0->position.z + pV1->position.z );
        normal.y += ( pV0->position.z - pV1->position.z ) *
                    ( pV0->position.x + pV1->position.x );
        normal.z += ( pV0->position.x - pV1->position.x ) *
                    ( pV0->position.y + pV1->position.y );

        hCur = pCur->hNext;
    }

    const f64 lenSq = math::Vec3d_LengthSquared( normal );
    if ( lenSq > 1.0e-24 ) {
        const f64 invLen = 1.0 / std::sqrt( lenSq );
        pFace->normal = math::Vec3d_Scale( normal, invLen );
    }
}

template <typename record_t, typename tag_t>
geometry_status_t CloneGenerationPoolExact(
    const generation_pool_t<record_t, tag_t> *pSource,
    generation_pool_t<record_t, tag_t> *pDestination ) noexcept
{
    static_assert( std::is_nothrow_copy_constructible_v<record_t> );

    if ( pSource == nullptr || pDestination == nullptr ||
         !GenerationPool_IsValid( pSource ) ||
         !GenerationPool_IsInitialized( pDestination ) ||
         pSource->pAllocator != pDestination->pAllocator ||
         pSource->cSlotLimit != pDestination->cSlotLimit ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const generation_pool_status_t reserveStatus =
        GenerationPool_Reserve( pDestination, pSource->cSlots );
    if ( reserveStatus != generation_pool_status_t::OK ) {
        return GeometryStatus_FromGenerationPoolStatus( reserveStatus );
    }

    for ( usize iSlot = 0u; iSlot < pSource->cSlots; ++iSlot ) {
        const generation_pool_slot_t<record_t> &sourceSlot =
            pSource->pSlots[iSlot];
        generation_pool_slot_t<record_t> &destinationSlot =
            pDestination->pSlots[iSlot];

        destinationSlot.nGeneration = sourceSlot.nGeneration;
        destinationSlot.iNextFree = sourceSlot.iNextFree;
        destinationSlot.bRetired = sourceSlot.bRetired;
        if ( sourceSlot.bOccupied ) {
            const generation_handle_t<tag_t> sourceHandle{
                static_cast<u32>( iSlot ), sourceSlot.nGeneration };
            const record_t *pRecord =
                GenerationPool_Get( pSource, sourceHandle );
            if ( pRecord == nullptr ) {
                return geometry_status_t::CORRUPT_STATE;
            }
            ::new ( static_cast<void *>( destinationSlot.storage ) )
                record_t( *pRecord );
        }
        destinationSlot.bOccupied = sourceSlot.bOccupied;
    }

    pDestination->cRecords = pSource->cRecords;
    pDestination->cRetiredSlots = pSource->cRetiredSlots;
    pDestination->iFreeHead = pSource->iFreeHead;
    return GenerationPool_IsValid( pDestination )
        ? geometry_status_t::OK
        : geometry_status_t::CORRUPT_STATE;
}

geometry_status_t CloneEditableMeshExact(
    const editable_mesh_t *pSource,
    editable_mesh_t *pDestination ) noexcept
{
    if ( pSource == nullptr || pDestination == nullptr ||
         !EditableMesh_IsInitialized( pSource ) ||
         !Allocator_IsValid( pSource->pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    geometry_status_t status = EditableMesh_Init(
        pDestination, pSource->pAllocator, pSource->limits );
    if ( status != geometry_status_t::OK ) { return status; }

    status = CloneGenerationPoolExact(
        &pSource->vertices, &pDestination->vertices );
    if ( status == geometry_status_t::OK ) {
        status = CloneGenerationPoolExact(
            &pSource->halfEdges, &pDestination->halfEdges );
    }
    if ( status == geometry_status_t::OK ) {
        status = CloneGenerationPoolExact(
            &pSource->edges, &pDestination->edges );
    }
    if ( status == geometry_status_t::OK ) {
        status = CloneGenerationPoolExact(
            &pSource->loops, &pDestination->loops );
    }
    if ( status == geometry_status_t::OK ) {
        status = CloneGenerationPoolExact(
            &pSource->faces, &pDestination->faces );
    }
    if ( status == geometry_status_t::OK ) {
        status = CloneGenerationPoolExact(
            &pSource->shells, &pDestination->shells );
    }

    if ( status != geometry_status_t::OK ) {
        EditableMesh_Shutdown( pDestination );
    }
    return status;
}

template <typename record_t, typename tag_t>
void SwapGenerationPoolStorage(
    generation_pool_t<record_t, tag_t> *pA,
    generation_pool_t<record_t, tag_t> *pB ) noexcept
{
    generation_pool_slot_t<record_t> *pSlots = pA->pSlots;
    pA->pSlots = pB->pSlots;
    pB->pSlots = pSlots;

    const usize cRecords = pA->cRecords;
    pA->cRecords = pB->cRecords;
    pB->cRecords = cRecords;

    const usize cSlots = pA->cSlots;
    pA->cSlots = pB->cSlots;
    pB->cSlots = cSlots;

    const usize cRetiredSlots = pA->cRetiredSlots;
    pA->cRetiredSlots = pB->cRetiredSlots;
    pB->cRetiredSlots = cRetiredSlots;

    const usize cSlotLimit = pA->cSlotLimit;
    pA->cSlotLimit = pB->cSlotLimit;
    pB->cSlotLimit = cSlotLimit;

    const u32 iFreeHead = pA->iFreeHead;
    pA->iFreeHead = pB->iFreeHead;
    pB->iFreeHead = iFreeHead;

    const allocator_t *pAllocator = pA->pAllocator;
    pA->pAllocator = pB->pAllocator;
    pB->pAllocator = pAllocator;
}

void SwapEditableMeshStorage(
    editable_mesh_t *pA,
    editable_mesh_t *pB ) noexcept
{
    SwapGenerationPoolStorage( &pA->vertices, &pB->vertices );
    SwapGenerationPoolStorage( &pA->halfEdges, &pB->halfEdges );
    SwapGenerationPoolStorage( &pA->edges, &pB->edges );
    SwapGenerationPoolStorage( &pA->loops, &pB->loops );
    SwapGenerationPoolStorage( &pA->faces, &pB->faces );
    SwapGenerationPoolStorage( &pA->shells, &pB->shells );
}

} // namespace

// ---------------------------------------------------------------------------
// MoveVertex
// ---------------------------------------------------------------------------

geometry_status_t MeshOps_MoveVertex(
    editable_mesh_t *pMesh,
    geometry_mesh_vertex_handle_t hVertex,
    math::vec3d_t newPosition ) noexcept
{
    if ( pMesh == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Vec3d_IsFinite( newPosition ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    mesh_vertex_record_t *pVert =
        GenerationPool_Get( &pMesh->vertices, hVertex );
    if ( pVert == nullptr ) {
        return geometry_status_t::INVALID_HANDLE;
    }

    pVert->position = newPosition;

    // Recompute normals of all faces adjacent to this vertex by walking
    // the half-edge fan: outgoing → loop → face, then twin → next.
    const geometry_mesh_half_edge_handle_t hStart = pVert->hOutHalfEdge;
    geometry_mesh_half_edge_handle_t hCur = hStart;
    u32 safety = 0u;
    do {
        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, hCur );
        if ( pHE == nullptr ) { break; }

        const mesh_loop_record_t *pLoop =
            GenerationPool_Get( &pMesh->loops, pHE->hLoop );
        if ( pLoop != nullptr ) {
            RecomputeFaceNormal( pMesh, pLoop->hFace );
        }

        // Move to next outgoing half-edge: twin → next.
        const mesh_half_edge_record_t *pTwin =
            GenerationPool_Get( &pMesh->halfEdges, pHE->hTwin );
        if ( pTwin == nullptr ) { break; }
        hCur = pTwin->hNext;

        if ( ++safety > 256u ) { break; }
    } while ( hCur.nSlot != hStart.nSlot ||
              hCur.nGeneration != hStart.nGeneration );

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// SplitEdge
// ---------------------------------------------------------------------------

mesh_split_edge_result_t MeshOps_SplitEdge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge,
    f64 t ) noexcept
{
    mesh_split_edge_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !GenerationPool_IsValid( &pMesh->vertices ) ||
         !GenerationPool_IsValid( &pMesh->halfEdges ) ||
         !GenerationPool_IsValid( &pMesh->edges ) ||
         !GenerationPool_IsValid( &pMesh->loops ) ||
         !GenerationPool_IsValid( &pMesh->faces ) ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }
    if ( !std::isfinite( t ) || t <= 0.0 || t >= 1.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    geometry_mesh_half_edge_handle_t hHE_A{};
    geometry_mesh_half_edge_handle_t hHE_B{};
    geometry_mesh_half_edge_handle_t hNextA{};
    geometry_mesh_half_edge_handle_t hNextB{};
    geometry_mesh_loop_handle_t hLoopA{};
    geometry_mesh_loop_handle_t hLoopB{};
    geometry_mesh_face_handle_t hFaceA{};
    geometry_mesh_face_handle_t hFaceB{};
    math::vec3d_t newPos{};

    // Copy every value needed after reserve. No raw pool record pointer may
    // survive this scope because any successful reserve can relocate records.
    {
        const mesh_edge_record_t *pEdge =
            GenerationPool_Get( &pMesh->edges, hEdge );
        if ( pEdge == nullptr ) {
            result.status = geometry_status_t::INVALID_HANDLE;
            return result;
        }

        hHE_A = pEdge->hHalfEdge;
        const mesh_half_edge_record_t *pHE_A =
            GenerationPool_Get( &pMesh->halfEdges, hHE_A );
        if ( pHE_A == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        hHE_B = pHE_A->hTwin;
        const mesh_half_edge_record_t *pHE_B =
            GenerationPool_Get( &pMesh->halfEdges, hHE_B );
        if ( pHE_B == nullptr || HandlesEqual( hHE_A, hHE_B ) ||
             !HandlesEqual( pHE_B->hTwin, hHE_A ) ||
             !HandlesEqual( pHE_A->hEdge, hEdge ) ||
             !HandlesEqual( pHE_B->hEdge, hEdge ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        const geometry_mesh_vertex_handle_t hVA = pHE_A->hOrigin;
        const geometry_mesh_vertex_handle_t hVB = pHE_B->hOrigin;
        const mesh_vertex_record_t *pVA =
            GenerationPool_Get( &pMesh->vertices, hVA );
        const mesh_vertex_record_t *pVB =
            GenerationPool_Get( &pMesh->vertices, hVB );
        if ( pVA == nullptr || pVB == nullptr ||
             HandlesEqual( hVA, hVB ) ||
             !math::Vec3d_IsFinite( pVA->position ) ||
             !math::Vec3d_IsFinite( pVB->position ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        hNextA = pHE_A->hNext;
        hNextB = pHE_B->hNext;
        hLoopA = pHE_A->hLoop;
        hLoopB = pHE_B->hLoop;

        const mesh_half_edge_record_t *pNextA =
            GenerationPool_Get( &pMesh->halfEdges, hNextA );
        const mesh_half_edge_record_t *pNextB =
            GenerationPool_Get( &pMesh->halfEdges, hNextB );
        const mesh_loop_record_t *pLoopA =
            GenerationPool_Get( &pMesh->loops, hLoopA );
        const mesh_loop_record_t *pLoopB =
            GenerationPool_Get( &pMesh->loops, hLoopB );
        if ( pNextA == nullptr || pNextB == nullptr ||
             pLoopA == nullptr || pLoopB == nullptr ||
             !HandlesEqual( pNextA->hPrev, hHE_A ) ||
             !HandlesEqual( pNextB->hPrev, hHE_B ) ||
             !HandlesEqual( pNextA->hOrigin, hVB ) ||
             !HandlesEqual( pNextB->hOrigin, hVA ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        hFaceA = pLoopA->hFace;
        hFaceB = pLoopB->hFace;
        if ( !ValidateLoopForSplit( pMesh, hLoopA, hFaceA ) ||
             ( !HandlesEqual( hLoopA, hLoopB ) &&
               !ValidateLoopForSplit( pMesh, hLoopB, hFaceB ) ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        if ( HandlesEqual( hLoopA, hLoopB ) ) {
            if ( pLoopA->cHalfEdges > CY_U32_MAX - 2u ) {
                result.status = geometry_status_t::LIMIT_EXCEEDED;
                return result;
            }
        } else if ( pLoopA->cHalfEdges == CY_U32_MAX ||
                    pLoopB->cHalfEdges == CY_U32_MAX ) {
            result.status = geometry_status_t::LIMIT_EXCEEDED;
            return result;
        }

        newPos = math::Vec3d_Add(
            math::Vec3d_Scale( pVA->position, 1.0 - t ),
            math::Vec3d_Scale( pVB->position, t ) );
        if ( !math::Vec3d_IsFinite( newPos ) ) {
            result.status = geometry_status_t::NUMERIC_FAILURE;
            return result;
        }
        if ( math::Vec3d_EqualsExact( newPos, pVA->position ) ||
             math::Vec3d_EqualsExact( newPos, pVB->position ) ||
             math::Vec3d_LengthSquared(
                 math::Vec3d_Subtract(
                     newPos, pVA->position ) ) == 0.0 ||
             math::Vec3d_LengthSquared(
                 math::Vec3d_Subtract(
                     newPos, pVB->position ) ) == 0.0 ) {
            // A mathematically interior t can round exactly onto an endpoint.
            // Publishing that point would create a zero-length edge.
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }
    }

    // Secure every reusable slot before the first insertion. A later reserve
    // can fail after an earlier pool grew, but no live record has changed yet.
    geometry_status_t reserveStatus =
        ReserveAdditionalRecords( &pMesh->vertices, 1u );
    if ( reserveStatus != geometry_status_t::OK ) {
        result.status = reserveStatus;
        return result;
    }
    reserveStatus = ReserveAdditionalRecords( &pMesh->halfEdges, 2u );
    if ( reserveStatus != geometry_status_t::OK ) {
        result.status = reserveStatus;
        return result;
    }
    reserveStatus = ReserveAdditionalRecords( &pMesh->edges, 1u );
    if ( reserveStatus != geometry_status_t::OK ) {
        result.status = reserveStatus;
        return result;
    }

    // The handles copied during preflight must still resolve after every
    // reserve. This reacquisition also keeps relocated record pointers local.
    if ( GenerationPool_Get( &pMesh->edges, hEdge ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, hHE_A ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, hHE_B ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, hNextA ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, hNextB ) == nullptr ||
         !ValidateLoopForSplit( pMesh, hLoopA, hFaceA ) ||
         ( !HandlesEqual( hLoopA, hLoopB ) &&
           !ValidateLoopForSplit( pMesh, hLoopB, hFaceB ) ) ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // Create new vertex.
    mesh_vertex_record_t newVert{};
    newVert.position = newPos;
    const auto vertResult = GenerationPool_Insert(
        &pMesh->vertices, newVert );
    if ( vertResult.status != generation_pool_status_t::OK ) {
        result.status = GeometryStatus_FromGenerationPoolStatus(
            vertResult.status );
        return result;
    }
    const geometry_mesh_vertex_handle_t hNewVert = vertResult.handle;

    geometry_mesh_half_edge_handle_t hNewHEA2{};
    geometry_mesh_half_edge_handle_t hNewHEB2{};
    geometry_mesh_edge_handle_t hNewEdge{};
    auto rollbackInserts = [&]() noexcept -> bool {
        bool bRolledBack = true;
        if ( GeometryHandle_IsValid( hNewEdge ) ) {
            bRolledBack =
                GenerationPool_Remove( &pMesh->edges, hNewEdge ) ==
                    generation_pool_status_t::OK &&
                bRolledBack;
        }
        if ( GeometryHandle_IsValid( hNewHEB2 ) ) {
            bRolledBack =
                GenerationPool_Remove( &pMesh->halfEdges, hNewHEB2 ) ==
                    generation_pool_status_t::OK &&
                bRolledBack;
        }
        if ( GeometryHandle_IsValid( hNewHEA2 ) ) {
            bRolledBack =
                GenerationPool_Remove( &pMesh->halfEdges, hNewHEA2 ) ==
                    generation_pool_status_t::OK &&
                bRolledBack;
        }
        bRolledBack =
            GenerationPool_Remove( &pMesh->vertices, hNewVert ) ==
                generation_pool_status_t::OK &&
            bRolledBack;
        return bRolledBack;
    };

    // Create two new half-edges (one for each side of the split).
    // HE_A goes VA → newVert (keeps the original direction).
    // newHE_A2 goes newVert → VB (second half, same side as HE_A).
    // HE_B goes VB → newVert (keeps the original direction).
    // newHE_B2 goes newVert → VA (second half, same side as HE_B).

    mesh_half_edge_record_t newHEA2{};
    newHEA2.hOrigin = hNewVert;
    newHEA2.hLoop = hLoopA;
    const auto heA2Result = GenerationPool_Insert(
        &pMesh->halfEdges, newHEA2 );
    if ( heA2Result.status != generation_pool_status_t::OK ) {
        const geometry_status_t insertStatus =
            GeometryStatus_FromGenerationPoolStatus( heA2Result.status );
        result.status = rollbackInserts()
            ? insertStatus
            : geometry_status_t::CORRUPT_STATE;
        return result;
    }
    hNewHEA2 = heA2Result.handle;

    mesh_half_edge_record_t newHEB2{};
    newHEB2.hOrigin = hNewVert;
    newHEB2.hLoop = hLoopB;
    const auto heB2Result = GenerationPool_Insert(
        &pMesh->halfEdges, newHEB2 );
    if ( heB2Result.status != generation_pool_status_t::OK ) {
        const geometry_status_t insertStatus =
            GeometryStatus_FromGenerationPoolStatus( heB2Result.status );
        result.status = rollbackInserts()
            ? insertStatus
            : geometry_status_t::CORRUPT_STATE;
        return result;
    }
    hNewHEB2 = heB2Result.handle;

    // Create a new edge for the second half.
    mesh_edge_record_t newEdgeRec{};
    newEdgeRec.hHalfEdge = hNewHEA2;
    const auto edgeResult = GenerationPool_Insert(
        &pMesh->edges, newEdgeRec );
    if ( edgeResult.status != generation_pool_status_t::OK ) {
        const geometry_status_t insertStatus =
            GeometryStatus_FromGenerationPoolStatus( edgeResult.status );
        result.status = rollbackInserts()
            ? insertStatus
            : geometry_status_t::CORRUPT_STATE;
        return result;
    }
    hNewEdge = edgeResult.handle;

    mesh_half_edge_record_t *pA =
        GenerationPool_Get( &pMesh->halfEdges, hHE_A );
    mesh_half_edge_record_t *pB =
        GenerationPool_Get( &pMesh->halfEdges, hHE_B );
    mesh_half_edge_record_t *pNextA =
        GenerationPool_Get( &pMesh->halfEdges, hNextA );
    mesh_half_edge_record_t *pNextB =
        GenerationPool_Get( &pMesh->halfEdges, hNextB );
    mesh_half_edge_record_t *pA2 =
        GenerationPool_Get( &pMesh->halfEdges, hNewHEA2 );
    mesh_half_edge_record_t *pB2 =
        GenerationPool_Get( &pMesh->halfEdges, hNewHEB2 );
    mesh_vertex_record_t *pNewV =
        GenerationPool_Get( &pMesh->vertices, hNewVert );
    mesh_loop_record_t *pLoopA =
        GenerationPool_Get( &pMesh->loops, hLoopA );
    mesh_loop_record_t *pLoopB =
        GenerationPool_Get( &pMesh->loops, hLoopB );
    if ( pA == nullptr || pB == nullptr || pNextA == nullptr ||
         pNextB == nullptr || pA2 == nullptr || pB2 == nullptr ||
         pNewV == nullptr || pLoopA == nullptr || pLoopB == nullptr ) {
        (void)rollbackInserts();
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // Patch HE_A: its next now points to newHEA2.
    pA->hNext = hNewHEA2;

    // Patch newHEA2: next = oldNextA, prev = HE_A, twin = newHEB2, edge = newEdge.
    pA2->hNext = hNextA;
    pA2->hPrev = hHE_A;
    pA2->hTwin = hNewHEB2;
    pA2->hEdge = hNewEdge;

    // Patch oldNextA's prev to point to newHEA2.
    pNextA->hPrev = hNewHEA2;

    // Patch HE_B: its next now points to newHEB2.
    pB->hNext = hNewHEB2;

    // Patch newHEB2: next = oldNextB, prev = HE_B, twin = newHEA2 (wait,
    // twin of HE_B was HE_A, and twin of newHEB2 is... let me reconsider.
    //
    // Original: HE_A (VA→VB) ↔ twin HE_B (VB→VA)
    // After split:
    //   HE_A (VA→M) ↔ twin newHEB2 (M→VA)  — original edge
    //   newHEA2 (M→VB) ↔ twin HE_B (VB→M) — new edge
    //
    // So: HE_A.twin = newHEB2, newHEB2.twin = HE_A
    //     newHEA2.twin = HE_B, HE_B.twin = newHEA2

    // Fix twin assignments.
    pA->hTwin = hNewHEB2;
    pB->hTwin = hNewHEA2;
    // HE_B origin stays VB, but after split HE_B goes VB→M, which is
    // the second-half edge. It must move to hNewEdge together with its
    // new twin; leaving it on hEdge gives the original edge three
    // half-edges and the new edge one.
    pB->hEdge = hNewEdge;
    pA2->hTwin = hHE_B;
    pA2->hEdge = hNewEdge;
    pB2->hNext = hNextB;
    pB2->hPrev = hHE_B;
    pB2->hTwin = hHE_A;
    pB2->hEdge = hEdge;

    // Patch oldNextB's prev.
    pNextB->hPrev = hNewHEB2;

    // Update edge records: original edge now refers to HE_A (VA→M).
    // Its half-edge handle stays hHE_A, which is correct since HE_A
    // still exists. New edge refers to newHEA2 (M→VB), already set.

    // Set new vertex's outgoing half-edge to one of the new half-edges.
    pNewV->hOutHalfEdge = hNewHEA2;

    // Update loop half-edge counts (+1 for each affected loop).
    pLoopA->cHalfEdges += 1u;
    pLoopB->cHalfEdges += 1u;

    // Recompute normals of affected faces.
    RecomputeFaceNormal( pMesh, hFaceA );
    if ( !HandlesEqual( hFaceA, hFaceB ) ) {
        RecomputeFaceNormal( pMesh, hFaceB );
    }

    result.hNewVertex = hNewVert;
    result.hNewEdge = hNewEdge;
    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// SplitFace
// ---------------------------------------------------------------------------

mesh_split_face_result_t MeshOps_SplitFace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    geometry_mesh_vertex_handle_t hVertexA,
    geometry_mesh_vertex_handle_t hVertexB ) noexcept
{
    mesh_split_face_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !GenerationPool_IsValid( &pMesh->vertices ) ||
         !GenerationPool_IsValid( &pMesh->halfEdges ) ||
         !GenerationPool_IsValid( &pMesh->edges ) ||
         !GenerationPool_IsValid( &pMesh->loops ) ||
         !GenerationPool_IsValid( &pMesh->faces ) ||
         !GenerationPool_IsValid( &pMesh->shells ) ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    geometry_mesh_loop_handle_t hOuterLoop{};
    geometry_mesh_shell_handle_t hFaceShell{};
    geometry_mesh_half_edge_handle_t hHE_A{};
    geometry_mesh_half_edge_handle_t hHE_B{};
    geometry_mesh_half_edge_handle_t hPrevA{};
    geometry_mesh_half_edge_handle_t hPrevB{};
    math::vec3d_t faceNormal{};
    usize cFromAToB = 0u;
    u32 cOriginalLoopHalfEdges = 0u;
    u32 cNewLoopHalfEdges = 0u;

    // Validate and copy all state needed after reserve. The loop walk is
    // bounded by its declared count, which ValidateLoopForSplit first bounds
    // by the number of live half-edges.
    {
        const mesh_face_record_t *pFace =
            GenerationPool_Get( &pMesh->faces, hFace );
        if ( pFace == nullptr ) {
            result.status = geometry_status_t::INVALID_HANDLE;
            return result;
        }
        if ( HandlesEqual( hVertexA, hVertexB ) ) {
            result.status = geometry_status_t::INVALID_ARGUMENT;
            return result;
        }

        hOuterLoop = pFace->hOuterLoop;
        hFaceShell = pFace->hShell;
        faceNormal = pFace->normal;
        if ( !math::Vec3d_IsFinite( faceNormal ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        const mesh_loop_record_t *pLoop =
            GenerationPool_Get( &pMesh->loops, hOuterLoop );
        if ( pLoop == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        if ( pLoop->cHalfEdges < 4u ) {
            // Need at least four edges to split into two valid faces.
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }
        if ( !ValidateLoopForSplit( pMesh, hOuterLoop, hFace ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        const mesh_shell_record_t *pShell =
            GenerationPool_Get( &pMesh->shells, hFaceShell );
        if ( pShell == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        if ( pShell->cFaces == CY_U32_MAX ) {
            result.status = geometry_status_t::LIMIT_EXCEEDED;
            return result;
        }

        usize iA = CY_USIZE_MAX;
        usize iB = CY_USIZE_MAX;
        geometry_mesh_half_edge_handle_t hCurrent =
            pLoop->hFirstHalfEdge;
        for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
            const mesh_half_edge_record_t *pCurrent =
                GenerationPool_Get( &pMesh->halfEdges, hCurrent );
            if ( pCurrent == nullptr ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }

            if ( HandlesEqual( pCurrent->hOrigin, hVertexA ) ) {
                if ( iA != CY_USIZE_MAX ) {
                    result.status = geometry_status_t::CORRUPT_STATE;
                    return result;
                }
                iA = static_cast<usize>( i );
                hHE_A = hCurrent;
            }
            if ( HandlesEqual( pCurrent->hOrigin, hVertexB ) ) {
                if ( iB != CY_USIZE_MAX ) {
                    result.status = geometry_status_t::CORRUPT_STATE;
                    return result;
                }
                iB = static_cast<usize>( i );
                hHE_B = hCurrent;
            }
            hCurrent = pCurrent->hNext;
        }

        if ( iA == CY_USIZE_MAX || iB == CY_USIZE_MAX ) {
            result.status = geometry_status_t::INVALID_ARGUMENT;
            return result;
        }

        const usize cLoopHalfEdges =
            static_cast<usize>( pLoop->cHalfEdges );
        cFromAToB = iB >= iA
            ? iB - iA
            : cLoopHalfEdges - ( iA - iB );
        if ( cFromAToB <= 1u ||
             cFromAToB >= cLoopHalfEdges - 1u ) {
            result.status = geometry_status_t::INVALID_ARGUMENT;
            return result;
        }

        const mesh_half_edge_record_t *pHEA =
            GenerationPool_Get( &pMesh->halfEdges, hHE_A );
        const mesh_half_edge_record_t *pHEB =
            GenerationPool_Get( &pMesh->halfEdges, hHE_B );
        if ( pHEA == nullptr || pHEB == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        hPrevA = pHEA->hPrev;
        hPrevB = pHEB->hPrev;

        cNewLoopHalfEdges = static_cast<u32>( cFromAToB + 1u );
        cOriginalLoopHalfEdges = static_cast<u32>(
            cLoopHalfEdges - cFromAToB + 1u );
    }

    // All additions are reserved up front. Earlier pools may retain grown
    // backing capacity if a later reserve fails, but no live records or
    // topology have changed at this point.
    geometry_status_t reserveStatus =
        ReserveAdditionalRecords( &pMesh->halfEdges, 2u );
    if ( reserveStatus != geometry_status_t::OK ) {
        result.status = reserveStatus;
        return result;
    }
    reserveStatus = ReserveAdditionalRecords( &pMesh->edges, 1u );
    if ( reserveStatus != geometry_status_t::OK ) {
        result.status = reserveStatus;
        return result;
    }
    reserveStatus = ReserveAdditionalRecords( &pMesh->loops, 1u );
    if ( reserveStatus != geometry_status_t::OK ) {
        result.status = reserveStatus;
        return result;
    }
    reserveStatus = ReserveAdditionalRecords( &pMesh->faces, 1u );
    if ( reserveStatus != geometry_status_t::OK ) {
        result.status = reserveStatus;
        return result;
    }

    // Reacquire every record touched after reserves have finished relocating
    // storage. Inserts now consume prevalidated free slots and cannot allocate.
    if ( !ValidateLoopForSplit( pMesh, hOuterLoop, hFace ) ||
         GenerationPool_Get( &pMesh->shells, hFaceShell ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, hHE_A ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, hHE_B ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, hPrevA ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, hPrevB ) == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // Create two new half-edges for the diagonal (A→B and B→A).
    mesh_half_edge_record_t diagAB{};
    diagAB.hOrigin = hVertexA;
    const auto diagABResult = GenerationPool_Insert(
        &pMesh->halfEdges, diagAB );
    if ( diagABResult.status != generation_pool_status_t::OK ) {
        result.status = GeometryStatus_FromGenerationPoolStatus(
            diagABResult.status );
        return result;
    }
    geometry_mesh_half_edge_handle_t hDiagAB = diagABResult.handle;
    geometry_mesh_half_edge_handle_t hDiagBA{};
    geometry_mesh_edge_handle_t hNewEdge{};
    geometry_mesh_loop_handle_t hNewLoop{};
    geometry_mesh_face_handle_t hNewFace{};
    auto rollbackInserts = [&]() noexcept -> bool {
        bool bRolledBack = true;
        if ( GeometryHandle_IsValid( hNewFace ) ) {
            bRolledBack =
                GenerationPool_Remove( &pMesh->faces, hNewFace ) ==
                    generation_pool_status_t::OK &&
                bRolledBack;
        }
        if ( GeometryHandle_IsValid( hNewLoop ) ) {
            bRolledBack =
                GenerationPool_Remove( &pMesh->loops, hNewLoop ) ==
                    generation_pool_status_t::OK &&
                bRolledBack;
        }
        if ( GeometryHandle_IsValid( hNewEdge ) ) {
            bRolledBack =
                GenerationPool_Remove( &pMesh->edges, hNewEdge ) ==
                    generation_pool_status_t::OK &&
                bRolledBack;
        }
        if ( GeometryHandle_IsValid( hDiagBA ) ) {
            bRolledBack =
                GenerationPool_Remove( &pMesh->halfEdges, hDiagBA ) ==
                    generation_pool_status_t::OK &&
                bRolledBack;
        }
        bRolledBack =
            GenerationPool_Remove( &pMesh->halfEdges, hDiagAB ) ==
                generation_pool_status_t::OK &&
            bRolledBack;
        return bRolledBack;
    };

    mesh_half_edge_record_t diagBA{};
    diagBA.hOrigin = hVertexB;
    const auto diagBAResult = GenerationPool_Insert(
        &pMesh->halfEdges, diagBA );
    if ( diagBAResult.status != generation_pool_status_t::OK ) {
        const geometry_status_t insertStatus =
            GeometryStatus_FromGenerationPoolStatus( diagBAResult.status );
        result.status = rollbackInserts()
            ? insertStatus
            : geometry_status_t::CORRUPT_STATE;
        return result;
    }
    hDiagBA = diagBAResult.handle;

    // Create the new edge record.
    mesh_edge_record_t newEdge{};
    newEdge.hHalfEdge = hDiagAB;
    const auto edgeResult = GenerationPool_Insert(
        &pMesh->edges, newEdge );
    if ( edgeResult.status != generation_pool_status_t::OK ) {
        const geometry_status_t insertStatus =
            GeometryStatus_FromGenerationPoolStatus( edgeResult.status );
        result.status = rollbackInserts()
            ? insertStatus
            : geometry_status_t::CORRUPT_STATE;
        return result;
    }
    hNewEdge = edgeResult.handle;

    // Create a new loop and face for one half of the split.
    mesh_loop_record_t newLoop{};
    newLoop.hFirstHalfEdge = hDiagBA;
    const auto loopResult = GenerationPool_Insert(
        &pMesh->loops, newLoop );
    if ( loopResult.status != generation_pool_status_t::OK ) {
        const geometry_status_t insertStatus =
            GeometryStatus_FromGenerationPoolStatus( loopResult.status );
        result.status = rollbackInserts()
            ? insertStatus
            : geometry_status_t::CORRUPT_STATE;
        return result;
    }
    hNewLoop = loopResult.handle;

    mesh_face_record_t newFace{};
    newFace.hOuterLoop = hNewLoop;
    newFace.hShell = hFaceShell;
    newFace.normal = faceNormal;
    newFace.iSourceSide = CY_INVALID_INDEX;
    const auto faceResult = GenerationPool_Insert(
        &pMesh->faces, newFace );
    if ( faceResult.status != generation_pool_status_t::OK ) {
        const geometry_status_t insertStatus =
            GeometryStatus_FromGenerationPoolStatus( faceResult.status );
        result.status = rollbackInserts()
            ? insertStatus
            : geometry_status_t::CORRUPT_STATE;
        return result;
    }
    hNewFace = faceResult.handle;

    mesh_half_edge_record_t *pDiagAB =
        GenerationPool_Get( &pMesh->halfEdges, hDiagAB );
    mesh_half_edge_record_t *pDiagBA =
        GenerationPool_Get( &pMesh->halfEdges, hDiagBA );
    mesh_half_edge_record_t *pPrevA =
        GenerationPool_Get( &pMesh->halfEdges, hPrevA );
    mesh_half_edge_record_t *pPrevB =
        GenerationPool_Get( &pMesh->halfEdges, hPrevB );
    mesh_half_edge_record_t *pHEA =
        GenerationPool_Get( &pMesh->halfEdges, hHE_A );
    mesh_half_edge_record_t *pHEB =
        GenerationPool_Get( &pMesh->halfEdges, hHE_B );
    mesh_loop_record_t *pOriginalLoop =
        GenerationPool_Get( &pMesh->loops, hOuterLoop );
    mesh_loop_record_t *pNewLoop =
        GenerationPool_Get( &pMesh->loops, hNewLoop );
    mesh_shell_record_t *pShell =
        GenerationPool_Get( &pMesh->shells, hFaceShell );
    if ( pDiagAB == nullptr || pDiagBA == nullptr || pPrevA == nullptr ||
         pPrevB == nullptr || pHEA == nullptr || pHEB == nullptr ||
         pOriginalLoop == nullptr || pNewLoop == nullptr ||
         pShell == nullptr ) {
        (void)rollbackInserts();
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // Check the exact path that will move to the new loop before changing any
    // existing record. This path was measured during the bounded preflight.
    geometry_mesh_half_edge_handle_t hWalk = hHE_A;
    for ( usize i = 0u; i < cFromAToB; ++i ) {
        const mesh_half_edge_record_t *pWalk =
            GenerationPool_Get( &pMesh->halfEdges, hWalk );
        if ( pWalk == nullptr ) {
            (void)rollbackInserts();
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        hWalk = pWalk->hNext;
    }
    if ( !HandlesEqual( hWalk, hHE_B ) ) {
        (void)rollbackInserts();
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    pNewLoop->hFace = hNewFace;

    // Now patch the topology. The original loop goes:
    //   ... → prevA → HE_A → ... → prevB → HE_B → ...
    //
    // After split:
    //   Original loop: ... → prevA → diagAB → HE_B → ... → prevA (cycle)
    //   New loop:      diagBA → HE_A → ... → prevB → diagBA (cycle)
    //
    // So diagAB.next = HE_B, diagAB.prev = prevA (the half-edge before HE_A)
    //    diagBA.next = HE_A, diagBA.prev = prevB (the half-edge before HE_B)

    // Patch diagAB.
    pDiagAB->hNext = hHE_B;
    pDiagAB->hPrev = hPrevA;
    pDiagAB->hTwin = hDiagBA;
    pDiagAB->hEdge = hNewEdge;
    pDiagAB->hLoop = hOuterLoop;

    // Patch diagBA.
    pDiagBA->hNext = hHE_A;
    pDiagBA->hPrev = hPrevB;
    pDiagBA->hTwin = hDiagAB;
    pDiagBA->hEdge = hNewEdge;
    pDiagBA->hLoop = hNewLoop;

    // Patch prevA → diagAB.
    pPrevA->hNext = hDiagAB;

    // Patch HE_B.prev → diagAB.
    pHEB->hPrev = hDiagAB;

    // Patch prevB → diagBA.
    pPrevB->hNext = hDiagBA;

    // Patch HE_A.prev → diagBA.
    pHEA->hPrev = hDiagBA;

    // Reassign loop membership for half-edges that moved to the new loop.
    // Walk the prevalidated A→B path, excluding HE_B.
    hWalk = hHE_A;
    for ( usize i = 0u; i < cFromAToB; ++i ) {
        mesh_half_edge_record_t *pWalk =
            GenerationPool_Get( &pMesh->halfEdges, hWalk );
        pWalk->hLoop = hNewLoop;
        hWalk = pWalk->hNext;
    }

    pNewLoop->cHalfEdges = cNewLoopHalfEdges;
    pOriginalLoop->cHalfEdges = cOriginalLoopHalfEdges;
    pOriginalLoop->hFirstHalfEdge = hDiagAB;

    // Update shell face count.
    pShell->cFaces += 1u;

    // Recompute normals for both faces.
    RecomputeFaceNormal( pMesh, hFace );
    RecomputeFaceNormal( pMesh, hNewFace );

    result.hNewEdge = hNewEdge;
    result.hNewFace = hNewFace;
    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// CollapseEdge
// ---------------------------------------------------------------------------

mesh_collapse_edge_result_t MeshOps_CollapseEdge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept
{
    mesh_collapse_edge_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    const mesh_edge_record_t *pEdge =
        GenerationPool_Get( &pMesh->edges, hEdge );
    if ( pEdge == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    const geometry_mesh_half_edge_handle_t hHE_A = pEdge->hHalfEdge;
    const mesh_half_edge_record_t *pHE_A =
        GenerationPool_Get( &pMesh->halfEdges, hHE_A );
    if ( pHE_A == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    const geometry_mesh_half_edge_handle_t hHE_B = pHE_A->hTwin;
    const mesh_half_edge_record_t *pHE_B =
        GenerationPool_Get( &pMesh->halfEdges, hHE_B );
    if ( pHE_B == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    const geometry_mesh_vertex_handle_t hSurvivor = pHE_A->hOrigin;
    const geometry_mesh_vertex_handle_t hRemoved = pHE_B->hOrigin;
    const geometry_mesh_loop_handle_t hLoopA = pHE_A->hLoop;
    const geometry_mesh_loop_handle_t hLoopB = pHE_B->hLoop;

    // --- Preflight: everything below mutates, so every refusal happens here.
    //
    // Link condition (Dey et al.): collapsing (a, b) keeps a closed
    // two-manifold only if the vertices adjacent to both a and b are
    // exactly the apexes of the triangles that share the edge. Any extra
    // common neighbour means the collapse would fuse two distinct edges
    // into one edge with four incident faces (non-manifold).
    {
        const mesh_loop_record_t *pPreLoopA =
            GenerationPool_Get( &pMesh->loops, pHE_A->hLoop );
        const mesh_loop_record_t *pPreLoopB =
            GenerationPool_Get( &pMesh->loops, pHE_B->hLoop );
        if ( pPreLoopA == nullptr || pPreLoopB == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        // Apex of a triangular side = origin of prev(half-edge).
        u32 cTriangleSides = 0u;
        geometry_mesh_vertex_handle_t apexes[2]{};
        auto recordApex = [&]( const mesh_half_edge_record_t *pSideHE,
                               const mesh_loop_record_t *pSideLoop ) noexcept {
            if ( pSideLoop->cHalfEdges != 3u ) { return; }
            const mesh_half_edge_record_t *pPrev =
                GenerationPool_Get( &pMesh->halfEdges, pSideHE->hPrev );
            if ( pPrev != nullptr ) {
                apexes[cTriangleSides++] = pPrev->hOrigin;
            }
        };
        recordApex( pHE_A, pPreLoopA );
        recordApex( pHE_B, pPreLoopB );

        // A closed shell needs at least four faces; a collapse that would
        // leave fewer (e.g. on a tetrahedron) cannot produce a valid solid.
        if ( EditableMesh_FaceCount( pMesh ) < 4u + cTriangleSides ) {
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }

        // Count vertices adjacent to both endpoints. Full-pool scan is
        // O(H); acceptable for editor-scale edits, and Decimate is the only
        // bulk caller (see its complexity note).
        auto isNeighbour = [&]( geometry_mesh_vertex_handle_t hFrom,
                                geometry_mesh_vertex_handle_t hTo ) noexcept {
            bool found = false;
            (void)GenerationPool_ForEach( &pMesh->halfEdges,
                [&]( geometry_mesh_half_edge_handle_t,
                     const mesh_half_edge_record_t &rec ) noexcept -> bool_t {
                    if ( rec.hOrigin.nSlot != hFrom.nSlot ||
                         rec.hOrigin.nGeneration != hFrom.nGeneration ) {
                        return true;
                    }
                    const mesh_half_edge_record_t *pN =
                        GenerationPool_Get( &pMesh->halfEdges, rec.hNext );
                    if ( pN != nullptr &&
                         pN->hOrigin.nSlot == hTo.nSlot &&
                         pN->hOrigin.nGeneration == hTo.nGeneration ) {
                        found = true;
                        return false;
                    }
                    return true;
                } );
            return found;
        };

        u32 cCommon = 0u;
        bool bApexesCommon = true;
        (void)GenerationPool_ForEach( &pMesh->vertices,
            [&]( geometry_mesh_vertex_handle_t hV,
                 const mesh_vertex_record_t & ) noexcept -> bool_t {
                if ( ( hV.nSlot == hSurvivor.nSlot &&
                       hV.nGeneration == hSurvivor.nGeneration ) ||
                     ( hV.nSlot == hRemoved.nSlot &&
                       hV.nGeneration == hRemoved.nGeneration ) ) {
                    return true;
                }
                if ( isNeighbour( hSurvivor, hV ) &&
                     isNeighbour( hRemoved, hV ) ) {
                    ++cCommon;
                }
                return true;
            } );
        for ( u32 i = 0u; i < cTriangleSides; ++i ) {
            if ( !isNeighbour( hSurvivor, apexes[i] ) ||
                 !isNeighbour( hRemoved, apexes[i] ) ) {
                bApexesCommon = false;
            }
        }
        if ( cCommon != cTriangleSides || !bApexesCommon ) {
            result.status = geometry_status_t::NON_MANIFOLD;
            return result;
        }
    }

    // Geometric preflight.  The topological link condition above prevents a
    // non-manifold collapse, but it cannot detect a face inversion or a
    // closed shell collapsing to zero volume.  Evaluate the midpoint collapse
    // without publishing it and reject destructive candidates before any pool
    // record is changed.
    const mesh_vertex_record_t *pSurvivorVertex =
        GenerationPool_Get( &pMesh->vertices, hSurvivor );
    const mesh_vertex_record_t *pRemovedVertex =
        GenerationPool_Get( &pMesh->vertices, hRemoved );
    if ( pSurvivorVertex == nullptr || pRemovedVertex == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    const math::vec3d_t collapsedPosition = math::Vec3d_Scale(
        math::Vec3d_Add(
            pSurvivorVertex->position, pRemovedVertex->position ),
        0.5 );
    if ( !math::Vec3d_IsFinite( collapsedPosition ) ) {
        result.status = geometry_status_t::NUMERIC_FAILURE;
        return result;
    }

    auto sameHandle = []( auto a, auto b ) noexcept {
        return a.nSlot == b.nSlot &&
               a.nGeneration == b.nGeneration;
    };
    auto proposedPosition = [&]( geometry_mesh_vertex_handle_t hVertex,
                                 math::vec3d_t *pPositionOut ) noexcept {
        if ( sameHandle( hVertex, hSurvivor ) ||
             sameHandle( hVertex, hRemoved ) ) {
            *pPositionOut = collapsedPosition;
            return true;
        }
        const mesh_vertex_record_t *pVertex =
            GenerationPool_Get( &pMesh->vertices, hVertex );
        if ( pVertex == nullptr ) { return false; }
        *pPositionOut = pVertex->position;
        return math::Vec3d_IsFinite( *pPositionOut );
    };

    bool bGeometricStateValid = true;
    bool bFaceWouldDegenerateOrFlip = false;
    f64 proposedVolumeSix = 0.0;
    (void)GenerationPool_ForEach(
        &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) {
                bGeometricStateValid = false;
                return false;
            }

            // A triangular face incident to the collapsed edge is removed by
            // the mutation and therefore contributes no proposed area/volume.
            if ( pLoop->cHalfEdges == 3u &&
                 ( sameHandle( face.hOuterLoop, hLoopA ) ||
                   sameHandle( face.hOuterLoop, hLoopB ) ) ) {
                return true;
            }

            math::vec3d_t oldNewell{};
            math::vec3d_t newNewell{};
            bool bAffected = false;
            geometry_mesh_half_edge_handle_t hCur =
                pLoop->hFirstHalfEdge;
            for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
                const mesh_half_edge_record_t *pCur =
                    GenerationPool_Get( &pMesh->halfEdges, hCur );
                const mesh_half_edge_record_t *pNext = pCur != nullptr
                    ? GenerationPool_Get( &pMesh->halfEdges, pCur->hNext )
                    : nullptr;
                if ( pCur == nullptr || pNext == nullptr ) {
                    bGeometricStateValid = false;
                    return false;
                }

                const mesh_vertex_record_t *pOld0 =
                    GenerationPool_Get( &pMesh->vertices, pCur->hOrigin );
                const mesh_vertex_record_t *pOld1 =
                    GenerationPool_Get( &pMesh->vertices, pNext->hOrigin );
                math::vec3d_t new0{};
                math::vec3d_t new1{};
                if ( pOld0 == nullptr || pOld1 == nullptr ||
                     !proposedPosition( pCur->hOrigin, &new0 ) ||
                     !proposedPosition( pNext->hOrigin, &new1 ) ) {
                    bGeometricStateValid = false;
                    return false;
                }

                const math::vec3d_t &old0 = pOld0->position;
                const math::vec3d_t &old1 = pOld1->position;
                oldNewell.x += ( old0.y - old1.y ) * ( old0.z + old1.z );
                oldNewell.y += ( old0.z - old1.z ) * ( old0.x + old1.x );
                oldNewell.z += ( old0.x - old1.x ) * ( old0.y + old1.y );
                newNewell.x += ( new0.y - new1.y ) * ( new0.z + new1.z );
                newNewell.y += ( new0.z - new1.z ) * ( new0.x + new1.x );
                newNewell.z += ( new0.x - new1.x ) * ( new0.y + new1.y );

                bAffected = bAffected ||
                    sameHandle( pCur->hOrigin, hSurvivor ) ||
                    sameHandle( pCur->hOrigin, hRemoved );
                hCur = pCur->hNext;
            }

            if ( bAffected ) {
                const f64 oldAreaSq = math::Vec3d_LengthSquared( oldNewell );
                const f64 newAreaSq = math::Vec3d_LengthSquared( newNewell );
                const f64 orientation =
                    math::Vec3d_Dot( oldNewell, newNewell );
                if ( !math::Scalar_IsFinite( oldAreaSq ) ||
                     !math::Scalar_IsFinite( newAreaSq ) ||
                     !math::Scalar_IsFinite( orientation ) ) {
                    bGeometricStateValid = false;
                    return false;
                }
                if ( oldAreaSq <= 1.0e-24 || newAreaSq <= 1.0e-24 ||
                     orientation <= 0.0 ) {
                    bFaceWouldDegenerateOrFlip = true;
                    return false;
                }
            }

            // Fan contribution to six times the signed volume, evaluated at
            // the proposed midpoint positions.
            const mesh_half_edge_record_t *pFirst =
                GenerationPool_Get(
                    &pMesh->halfEdges, pLoop->hFirstHalfEdge );
            if ( pFirst == nullptr ) {
                bGeometricStateValid = false;
                return false;
            }
            math::vec3d_t a{};
            if ( !proposedPosition( pFirst->hOrigin, &a ) ) {
                bGeometricStateValid = false;
                return false;
            }

            hCur = pFirst->hNext;
            for ( u32 i = 1u; i + 1u < pLoop->cHalfEdges; ++i ) {
                const mesh_half_edge_record_t *pCurrent =
                    GenerationPool_Get( &pMesh->halfEdges, hCur );
                const mesh_half_edge_record_t *pNext = pCurrent != nullptr
                    ? GenerationPool_Get(
                          &pMesh->halfEdges, pCurrent->hNext )
                    : nullptr;
                math::vec3d_t b{};
                math::vec3d_t c{};
                if ( pCurrent == nullptr || pNext == nullptr ||
                     !proposedPosition( pCurrent->hOrigin, &b ) ||
                     !proposedPosition( pNext->hOrigin, &c ) ) {
                    bGeometricStateValid = false;
                    return false;
                }
                proposedVolumeSix +=
                    a.x * ( b.y * c.z - b.z * c.y ) +
                    a.y * ( b.z * c.x - b.x * c.z ) +
                    a.z * ( b.x * c.y - b.y * c.x );
                hCur = pCurrent->hNext;
            }
            return true;
        } );

    if ( !bGeometricStateValid ||
         !math::Scalar_IsFinite( proposedVolumeSix ) ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }
    const f64 currentVolume = EditableMesh_SignedVolume( pMesh );
    const f64 proposedVolume = proposedVolumeSix / 6.0;
    if ( bFaceWouldDegenerateOrFlip ||
         !math::Scalar_IsFinite( currentVolume ) ||
         !math::Scalar_IsFinite( proposedVolume ) ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }

    // Open surface meshes legitimately have zero signed volume.  Apply the
    // shell-volume guard only when the input already encloses measurable
    // oriented volume; local face checks above still protect open surfaces.
    if ( math::Scalar_Abs( currentVolume ) > 1.0e-18 ) {
        const f64 minimumVolume = std::fmax(
            math::Scalar_Abs( currentVolume ) * 1.0e-12, 1.0e-18 );
        if ( math::Scalar_Abs( proposedVolume ) <= minimumVolume ||
             currentVolume * proposedVolume <= 0.0 ) {
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }
    }

    // Move survivor to the midpoint.
    {
        const mesh_vertex_record_t *pVS =
            GenerationPool_Get( &pMesh->vertices, hSurvivor );
        const mesh_vertex_record_t *pVR =
            GenerationPool_Get( &pMesh->vertices, hRemoved );
        if ( pVS == nullptr || pVR == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        mesh_vertex_record_t *pVS_mut =
            GenerationPool_Get( &pMesh->vertices, hSurvivor );
        pVS_mut->position = collapsedPosition;
    }

    // Retarget all half-edges that originate at hRemoved to hSurvivor.
    (void)GenerationPool_ForEach(
        &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t,
             mesh_half_edge_record_t &rec ) noexcept -> bool_t {
            if ( rec.hOrigin.nSlot == hRemoved.nSlot &&
                 rec.hOrigin.nGeneration == hRemoved.nGeneration ) {
                rec.hOrigin = hSurvivor;
            }
            return true;
        } );

    // For each side (A and B), if the adjacent face becomes degenerate
    // (triangle collapsing to a line), remove the face, its loop, and
    // the two other half-edges/edge that form the degenerate triangle.
    auto removeDegenerateFace = [&](
        geometry_mesh_half_edge_handle_t hCollapseHE ) noexcept {
        const mesh_half_edge_record_t *pColHE =
            GenerationPool_Get( &pMesh->halfEdges, hCollapseHE );
        if ( pColHE == nullptr ) { return; }

        const mesh_loop_record_t *pLoop =
            GenerationPool_Get( &pMesh->loops, pColHE->hLoop );
        if ( pLoop == nullptr || pLoop->cHalfEdges > 3u ) { return; }

        // Triangle: 3 half-edges. After collapse, two vertices are the
        // same, making it degenerate. Remove face + loop + the two
        // half-edges of the collapsed edge within this triangle.
        const geometry_mesh_half_edge_handle_t hNext = pColHE->hNext;
        const geometry_mesh_half_edge_handle_t hPrev = pColHE->hPrev;

        // Patch the twin pointers of the surviving edges.
        // hNext.twin and hPrev.twin should become each other's twins.
        const mesh_half_edge_record_t *pNext =
            GenerationPool_Get( &pMesh->halfEdges, hNext );
        const mesh_half_edge_record_t *pPrev =
            GenerationPool_Get( &pMesh->halfEdges, hPrev );
        if ( pNext == nullptr || pPrev == nullptr ) { return; }

        const geometry_mesh_half_edge_handle_t hTwinNext = pNext->hTwin;
        const geometry_mesh_half_edge_handle_t hTwinPrev = pPrev->hTwin;

        // Make twinNext and twinPrev twins of each other.
        {
            mesh_half_edge_record_t *pTN =
                GenerationPool_Get( &pMesh->halfEdges, hTwinNext );
            if ( pTN != nullptr ) {
                pTN->hTwin = hTwinPrev;
            }
        }

        // next(e)'s edge survives and prev(e)'s edge is removed, so
        // twin(prev) must be re-homed onto the surviving edge; otherwise it
        // keeps naming a removed edge record.
        const geometry_mesh_edge_handle_t hSurvivingEdge = pNext->hEdge;
        const geometry_mesh_edge_handle_t hDoomedEdge = pPrev->hEdge;
        {
            mesh_half_edge_record_t *pTP =
                GenerationPool_Get( &pMesh->halfEdges, hTwinPrev );
            if ( pTP != nullptr ) {
                pTP->hTwin = hTwinNext;
                pTP->hEdge = hSurvivingEdge;
            }
        }
        {
            mesh_edge_record_t *pSurvivingEdge =
                GenerationPool_Get( &pMesh->edges, hSurvivingEdge );
            if ( pSurvivingEdge != nullptr ) {
                pSurvivingEdge->hHalfEdge = hTwinNext;
            }
        }

        // Both slot AND generation must match for the same handle, so the
        // "different handle" test is an OR. (An AND here never removed the
        // edge whenever generations coincided, leaking one edge record per
        // degenerate side and driving the Euler characteristic negative.)
        if ( hDoomedEdge.nSlot != hEdge.nSlot ||
             hDoomedEdge.nGeneration != hEdge.nGeneration ) {
            (void)GenerationPool_Remove( &pMesh->edges, hDoomedEdge );
        }

        auto sameHE = []( geometry_mesh_half_edge_handle_t a,
                          geometry_mesh_half_edge_handle_t b ) noexcept {
            return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
        };

        // Survivor: twin(prev) leaves the survivor on both sides (origins
        // were retargeted above). twin(next) leaves the apex instead, so it
        // is not a valid replacement for the survivor.
        {
            mesh_vertex_record_t *pVS =
                GenerationPool_Get( &pMesh->vertices, hSurvivor );
            if ( pVS != nullptr &&
                 ( sameHE( pVS->hOutHalfEdge, hCollapseHE ) ||
                   sameHE( pVS->hOutHalfEdge, hNext ) ||
                   sameHE( pVS->hOutHalfEdge, hPrev ) ) ) {
                pVS->hOutHalfEdge = hTwinPrev;
            }
        }

        // Apex: prev(e) leaves the apex and is about to be removed;
        // twin(next) also leaves the apex and survives.
        {
            mesh_vertex_record_t *pApex =
                GenerationPool_Get( &pMesh->vertices, pPrev->hOrigin );
            if ( pApex != nullptr && sameHE( pApex->hOutHalfEdge, hPrev ) ) {
                pApex->hOutHalfEdge = hTwinNext;
            }
        }

        // Remove face, loop, and the three half-edges of this triangle.
        const geometry_mesh_face_handle_t hFaceToRemove = pLoop->hFace;
        const geometry_mesh_shell_handle_t hShell =
            GenerationPool_Get( &pMesh->faces, hFaceToRemove )->hShell;
        (void)GenerationPool_Remove( &pMesh->faces, hFaceToRemove );
        (void)GenerationPool_Remove( &pMesh->loops, pColHE->hLoop );
        (void)GenerationPool_Remove( &pMesh->halfEdges, hCollapseHE );
        (void)GenerationPool_Remove( &pMesh->halfEdges, hNext );
        (void)GenerationPool_Remove( &pMesh->halfEdges, hPrev );

        // Update shell face count.
        mesh_shell_record_t *pShell =
            GenerationPool_Get( &pMesh->shells, hShell );
        if ( pShell != nullptr && pShell->cFaces > 0u ) {
            pShell->cFaces -= 1u;
        }
    };

    // Save the loop pointers before removing.
    // Check for degenerate faces on both sides.
    const mesh_loop_record_t *pLoopA =
        GenerationPool_Get( &pMesh->loops, hLoopA );
    const mesh_loop_record_t *pLoopB =
        GenerationPool_Get( &pMesh->loops, hLoopB );
    const bool bDegenerateA =
        ( pLoopA != nullptr && pLoopA->cHalfEdges <= 3u );
    const bool bDegenerateB =
        ( pLoopB != nullptr && pLoopB->cHalfEdges <= 3u );

    if ( bDegenerateA ) {
        removeDegenerateFace( hHE_A );
    } else {
        // Non-degenerate: just splice out the collapse half-edge.
        const mesh_half_edge_record_t *pA =
            GenerationPool_Get( &pMesh->halfEdges, hHE_A );
        if ( pA != nullptr ) {
            mesh_half_edge_record_t *pPrevA =
                GenerationPool_Get( &pMesh->halfEdges, pA->hPrev );
            mesh_half_edge_record_t *pNextA =
                GenerationPool_Get( &pMesh->halfEdges, pA->hNext );
            if ( pPrevA ) { pPrevA->hNext = pA->hNext; }
            if ( pNextA ) { pNextA->hPrev = pA->hPrev; }

            // Update loop's first HE if it was the removed one.
            mesh_loop_record_t *pL =
                GenerationPool_Get( &pMesh->loops, hLoopA );
            if ( pL != nullptr ) {
                if ( pL->hFirstHalfEdge.nSlot == hHE_A.nSlot &&
                     pL->hFirstHalfEdge.nGeneration == hHE_A.nGeneration ) {
                    pL->hFirstHalfEdge = pA->hNext;
                }
                pL->cHalfEdges -= 1u;
            }
            (void)GenerationPool_Remove( &pMesh->halfEdges, hHE_A );
        }
    }

    if ( bDegenerateB ) {
        removeDegenerateFace( hHE_B );
    } else {
        const mesh_half_edge_record_t *pB =
            GenerationPool_Get( &pMesh->halfEdges, hHE_B );
        if ( pB != nullptr ) {
            mesh_half_edge_record_t *pPrevB =
                GenerationPool_Get( &pMesh->halfEdges, pB->hPrev );
            mesh_half_edge_record_t *pNextB =
                GenerationPool_Get( &pMesh->halfEdges, pB->hNext );
            if ( pPrevB ) { pPrevB->hNext = pB->hNext; }
            if ( pNextB ) { pNextB->hPrev = pB->hPrev; }

            mesh_loop_record_t *pL =
                GenerationPool_Get( &pMesh->loops, hLoopB );
            if ( pL != nullptr ) {
                if ( pL->hFirstHalfEdge.nSlot == hHE_B.nSlot &&
                     pL->hFirstHalfEdge.nGeneration == hHE_B.nGeneration ) {
                    pL->hFirstHalfEdge = pB->hNext;
                }
                pL->cHalfEdges -= 1u;
            }
            (void)GenerationPool_Remove( &pMesh->halfEdges, hHE_B );
        }
    }

    // Remove the collapsed edge.
    (void)GenerationPool_Remove( &pMesh->edges, hEdge );

    // Remove the defunct vertex.
    (void)GenerationPool_Remove( &pMesh->vertices, hRemoved );

    // Fix survivor's outgoing half-edge if it was one of the removed HEs.
    {
        mesh_vertex_record_t *pVS =
            GenerationPool_Get( &pMesh->vertices, hSurvivor );
        if ( pVS != nullptr ) {
            if ( !GenerationPool_Contains(
                     &pMesh->halfEdges, pVS->hOutHalfEdge ) ) {
                // Find any surviving outgoing HE from this vertex.
                (void)GenerationPool_ForEach(
                    &pMesh->halfEdges,
                    [&]( geometry_mesh_half_edge_handle_t h,
                         const mesh_half_edge_record_t &rec ) noexcept -> bool_t {
                        if ( rec.hOrigin.nSlot == hSurvivor.nSlot &&
                             rec.hOrigin.nGeneration == hSurvivor.nGeneration ) {
                            pVS->hOutHalfEdge = h;
                            return false;
                        }
                        return true;
                    } );
            }
        }
    }

    // Recompute normals of all remaining adjacent faces.
    (void)GenerationPool_ForEach(
        &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t,
             const mesh_half_edge_record_t &rec ) noexcept -> bool_t {
            if ( rec.hOrigin.nSlot == hSurvivor.nSlot &&
                 rec.hOrigin.nGeneration == hSurvivor.nGeneration ) {
                const mesh_loop_record_t *pL =
                    GenerationPool_Get( &pMesh->loops, rec.hLoop );
                if ( pL != nullptr ) {
                    RecomputeFaceNormal( pMesh, pL->hFace );
                }
            }
            return true;
        } );

    result.hSurvivor = hSurvivor;
    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// DissolveEdge
// ---------------------------------------------------------------------------

geometry_status_t MeshOps_DissolveEdge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept
{
    if ( pMesh == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    const mesh_edge_record_t *pEdge =
        GenerationPool_Get( &pMesh->edges, hEdge );
    if ( pEdge == nullptr ) {
        return geometry_status_t::INVALID_HANDLE;
    }

    const geometry_mesh_half_edge_handle_t hHE_A = pEdge->hHalfEdge;
    const mesh_half_edge_record_t *pHE_A =
        GenerationPool_Get( &pMesh->halfEdges, hHE_A );
    if ( pHE_A == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const geometry_mesh_half_edge_handle_t hHE_B = pHE_A->hTwin;
    const mesh_half_edge_record_t *pHE_B =
        GenerationPool_Get( &pMesh->halfEdges, hHE_B );
    if ( pHE_B == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    // Both half-edges must belong to different faces.
    const geometry_mesh_loop_handle_t hLoopA = pHE_A->hLoop;
    const geometry_mesh_loop_handle_t hLoopB = pHE_B->hLoop;
    if ( hLoopA.nSlot == hLoopB.nSlot &&
         hLoopA.nGeneration == hLoopB.nGeneration ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // The surviving face keeps loopA. LoopB's face is removed.
    const mesh_loop_record_t *pLoopA =
        GenerationPool_Get( &pMesh->loops, hLoopA );
    const mesh_loop_record_t *pLoopB =
        GenerationPool_Get( &pMesh->loops, hLoopB );
    if ( pLoopA == nullptr || pLoopB == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const geometry_mesh_face_handle_t hFaceSurvivor = pLoopA->hFace;
    const geometry_mesh_face_handle_t hFaceRemoved = pLoopB->hFace;

    // Get the prev/next around both half-edges.
    const geometry_mesh_half_edge_handle_t hPrevA = pHE_A->hPrev;
    const geometry_mesh_half_edge_handle_t hNextA = pHE_A->hNext;
    const geometry_mesh_half_edge_handle_t hPrevB = pHE_B->hPrev;
    const geometry_mesh_half_edge_handle_t hNextB = pHE_B->hNext;

    // Merged loop size is |A| + |B| - 2. Below 3 the survivor would be a
    // degenerate face, so refuse before mutating anything.
    if ( pLoopA->cHalfEdges + pLoopB->cHalfEdges < 5u ) {
        return geometry_status_t::DEGENERATE;
    }

    // Splice: prevA → nextB, prevB → nextA.
    {
        mesh_half_edge_record_t *pPrevA =
            GenerationPool_Get( &pMesh->halfEdges, hPrevA );
        if ( pPrevA ) { pPrevA->hNext = hNextB; }
    }
    {
        mesh_half_edge_record_t *pNextB =
            GenerationPool_Get( &pMesh->halfEdges, hNextB );
        if ( pNextB ) { pNextB->hPrev = hPrevA; }
    }
    {
        mesh_half_edge_record_t *pPrevB =
            GenerationPool_Get( &pMesh->halfEdges, hPrevB );
        if ( pPrevB ) { pPrevB->hNext = hNextA; }
    }
    {
        mesh_half_edge_record_t *pNextA =
            GenerationPool_Get( &pMesh->halfEdges, hNextA );
        if ( pNextA ) { pNextA->hPrev = hPrevB; }
    }

    // Reassign all half-edges from loopB to loopA.
    {
        geometry_mesh_half_edge_handle_t hWalk = hNextB;
        u32 safety = 0u;
        while ( safety < 1024u ) {
            mesh_half_edge_record_t *pWalk =
                GenerationPool_Get( &pMesh->halfEdges, hWalk );
            if ( pWalk == nullptr ) { break; }
            if ( pWalk->hLoop.nSlot == hLoopB.nSlot &&
                 pWalk->hLoop.nGeneration == hLoopB.nGeneration ) {
                pWalk->hLoop = hLoopA;
            }
            hWalk = pWalk->hNext;
            ++safety;
            if ( hWalk.nSlot == hNextB.nSlot &&
                 hWalk.nGeneration == hNextB.nGeneration ) {
                break;
            }
        }
    }

    // Update the surviving loop: recount and fix firstHalfEdge.
    {
        mesh_loop_record_t *pLA =
            GenerationPool_Get( &pMesh->loops, hLoopA );
        if ( pLA != nullptr ) {
            pLA->hFirstHalfEdge = hNextA;
            // Recount.
            u32 count = 0u;
            geometry_mesh_half_edge_handle_t hW = hNextA;
            u32 s = 0u;
            do {
                const mesh_half_edge_record_t *pW =
                    GenerationPool_Get( &pMesh->halfEdges, hW );
                if ( pW == nullptr ) { break; }
                ++count;
                hW = pW->hNext;
                if ( ++s > 1024u ) { break; }
            } while ( hW.nSlot != hNextA.nSlot ||
                      hW.nGeneration != hNextA.nGeneration );
            pLA->cHalfEdges = count;
        }
    }

    // Fix vertex outgoing half-edges that pointed to the removed HEs.
    // HE_A runs VA→VB, so next(HE_A) leaves VB and next(HE_B) leaves VA:
    // the replacement for each vertex comes from the *opposite* side.
    {
        mesh_vertex_record_t *pVA =
            GenerationPool_Get( &pMesh->vertices, pHE_A->hOrigin );
        if ( pVA != nullptr &&
             pVA->hOutHalfEdge.nSlot == hHE_A.nSlot &&
             pVA->hOutHalfEdge.nGeneration == hHE_A.nGeneration ) {
            pVA->hOutHalfEdge = hNextB;
        }
    }
    {
        mesh_vertex_record_t *pVB =
            GenerationPool_Get( &pMesh->vertices, pHE_B->hOrigin );
        if ( pVB != nullptr &&
             pVB->hOutHalfEdge.nSlot == hHE_B.nSlot &&
             pVB->hOutHalfEdge.nGeneration == hHE_B.nGeneration ) {
            pVB->hOutHalfEdge = hNextA;
        }
    }

    // Update shell face count.
    {
        const mesh_face_record_t *pRemovedFace =
            GenerationPool_Get( &pMesh->faces, hFaceRemoved );
        if ( pRemovedFace != nullptr ) {
            mesh_shell_record_t *pShell =
                GenerationPool_Get( &pMesh->shells,
                                    pRemovedFace->hShell );
            if ( pShell != nullptr && pShell->cFaces > 0u ) {
                pShell->cFaces -= 1u;
            }
        }
    }

    // Remove the dissolved elements.
    (void)GenerationPool_Remove( &pMesh->halfEdges, hHE_A );
    (void)GenerationPool_Remove( &pMesh->halfEdges, hHE_B );
    (void)GenerationPool_Remove( &pMesh->edges, hEdge );
    (void)GenerationPool_Remove( &pMesh->faces, hFaceRemoved );
    (void)GenerationPool_Remove( &pMesh->loops, hLoopB );

    // Recompute the surviving face normal.
    RecomputeFaceNormal( pMesh, hFaceSurvivor );

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// ExtrudeFace
// ---------------------------------------------------------------------------

static mesh_extrude_face_result_t MeshOps_ExtrudeFaceInPlace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    f64 distance ) noexcept
{
    mesh_extrude_face_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    const mesh_face_record_t *pFace =
        GenerationPool_Get( &pMesh->faces, hFace );
    if ( pFace == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    const math::vec3d_t normal = pFace->normal;
    const geometry_mesh_shell_handle_t hShell = pFace->hShell;
    // GenerationPool_Insert may grow the face pool and invalidate pFace.
    // Preserve every value needed after the first insertion as a handle/value.
    const geometry_mesh_loop_handle_t hOriginalLoop = pFace->hOuterLoop;

    const mesh_loop_record_t *pLoop =
        GenerationPool_Get( &pMesh->loops, hOriginalLoop );
    if ( pLoop == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    const u32 cEdges = pLoop->cHalfEdges;
    if ( cEdges < 3u || cEdges > 256u ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }

    // Collect the original face's boundary vertices and half-edges.
    geometry_mesh_vertex_handle_t origVerts[256];
    geometry_mesh_half_edge_handle_t origHEs[256];

    {
        geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
        for ( u32 i = 0u; i < cEdges; ++i ) {
            const mesh_half_edge_record_t *pHE =
                GenerationPool_Get( &pMesh->halfEdges, hCur );
            if ( pHE == nullptr ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
            origVerts[i] = pHE->hOrigin;
            origHEs[i] = hCur;
            hCur = pHE->hNext;
        }
    }

    // Preflight every offset position before the first insertion. A nonzero
    // distance can still round back onto the original vertex at its current
    // coordinate magnitude, which would create zero-area side faces.
    const math::vec3d_t offset = math::Vec3d_Scale( normal, distance );
    math::vec3d_t newPositions[256];
    for ( u32 i = 0u; i < cEdges; ++i ) {
        const mesh_vertex_record_t *pOrig =
            GenerationPool_Get( &pMesh->vertices, origVerts[i] );
        if ( pOrig == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        newPositions[i] = math::Vec3d_Add( pOrig->position, offset );
        if ( !math::Vec3d_IsFinite( newPositions[i] ) ) {
            result.status = geometry_status_t::NUMERIC_FAILURE;
            return result;
        }
        if ( math::Vec3d_EqualsExact(
                 newPositions[i], pOrig->position ) ) {
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }
    }

    // Create N new vertices at the preflighted offset positions.
    geometry_mesh_vertex_handle_t newVerts[256];

    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_vertex_record_t vRec{};
        vRec.position = newPositions[i];

        const auto vResult = GenerationPool_Insert( &pMesh->vertices, vRec );
        if ( vResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                vResult.status );
            return result;
        }
        newVerts[i] = vResult.handle;
    }

    // For each original edge, find its twin (on the adjacent face) so we
    // can splice the side quads between the original face and its neighbors.
    // Save the twin half-edge handles before we start modifying topology.
    geometry_mesh_half_edge_handle_t twinHEs[256];
    for ( u32 i = 0u; i < cEdges; ++i ) {
        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, origHEs[i] );
        if ( pHE == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        twinHEs[i] = pHE->hTwin;
    }

    // Create the extruded top face: N half-edges around the new vertices,
    // same winding as the original face.
    geometry_mesh_half_edge_handle_t topHEs[256];
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_half_edge_record_t heRec{};
        heRec.hOrigin = newVerts[i];

        const auto heResult = GenerationPool_Insert( &pMesh->halfEdges, heRec );
        if ( heResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                heResult.status );
            return result;
        }
        topHEs[i] = heResult.handle;
    }

    // Link top face half-edges next/prev.
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, topHEs[i] );
        if ( pHE == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        pHE->hNext = topHEs[( i + 1u ) % cEdges];
        pHE->hPrev = topHEs[( i + cEdges - 1u ) % cEdges];
    }

    // Create loop and face for the top.
    mesh_loop_record_t topLoopRec{};
    topLoopRec.hFirstHalfEdge = topHEs[0];
    topLoopRec.cHalfEdges = cEdges;

    const auto topLoopResult = GenerationPool_Insert( &pMesh->loops, topLoopRec );
    if ( topLoopResult.status != generation_pool_status_t::OK ) {
        result.status = GeometryStatus_FromGenerationPoolStatus(
            topLoopResult.status );
        return result;
    }

    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, topHEs[i] );
        if ( pHE != nullptr ) {
            pHE->hLoop = topLoopResult.handle;
        }
    }

    mesh_face_record_t topFaceRec{};
    topFaceRec.hOuterLoop = topLoopResult.handle;
    topFaceRec.normal = normal;
    topFaceRec.hShell = hShell;
    topFaceRec.iSourceSide = CY_INVALID_INDEX;

    const auto topFaceResult = GenerationPool_Insert( &pMesh->faces, topFaceRec );
    if ( topFaceResult.status != generation_pool_status_t::OK ) {
        result.status = GeometryStatus_FromGenerationPoolStatus(
            topFaceResult.status );
        return result;
    }

    {
        mesh_loop_record_t *pTL =
            GenerationPool_Get( &pMesh->loops, topLoopResult.handle );
        if ( pTL != nullptr ) {
            pTL->hFace = topFaceResult.handle;
        }
    }

    // Set outgoing half-edges on new vertices.
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_vertex_record_t *pV =
            GenerationPool_Get( &pMesh->vertices, newVerts[i] );
        if ( pV != nullptr ) {
            pV->hOutHalfEdge = topHEs[i];
        }
    }

    // Create N side quad faces. Each side connects:
    //   origVerts[i] → origVerts[i+1] → newVerts[i+1] → newVerts[i]
    // But we need the winding to face outward. The original face's half-edge
    // origHEs[i] goes origVerts[i] → origVerts[i+1]. The side quad should
    // share the edge with the adjacent face (via the twin of origHEs[i]).
    // Side winding (outward): newVerts[i] → origVerts[i] → origVerts[i+1] → newVerts[i+1]
    // This creates the side facing outward.

    for ( u32 i = 0u; i < cEdges; ++i ) {
        const u32 iNext = ( i + 1u ) % cEdges;

        // Side quad vertices: newV[i], origV[i], origV[iNext], newV[iNext]
        // Winding must be outward (away from the extrusion axis).
        // The correct outward winding for the side is:
        //   origV[iNext] → origV[i] → newV[i] → newV[iNext]
        // This ensures the side normal points away from the solid.
        const geometry_mesh_vertex_handle_t sideVerts[4] = {
            origVerts[iNext], origVerts[i], newVerts[i], newVerts[iNext]
        };

        geometry_mesh_half_edge_handle_t sideHEs[4];
        for ( u32 j = 0u; j < 4u; ++j ) {
            mesh_half_edge_record_t heRec{};
            heRec.hOrigin = sideVerts[j];

            const auto heResult = GenerationPool_Insert(
                &pMesh->halfEdges, heRec );
            if ( heResult.status != generation_pool_status_t::OK ) {
                result.status = GeometryStatus_FromGenerationPoolStatus(
                    heResult.status );
                return result;
            }
            sideHEs[j] = heResult.handle;
        }

        // Link next/prev.
        for ( u32 j = 0u; j < 4u; ++j ) {
            mesh_half_edge_record_t *pHE =
                GenerationPool_Get( &pMesh->halfEdges, sideHEs[j] );
            if ( pHE == nullptr ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
            pHE->hNext = sideHEs[( j + 1u ) % 4u];
            pHE->hPrev = sideHEs[( j + 3u ) % 4u];
        }

        // Create loop.
        mesh_loop_record_t sideLoopRec{};
        sideLoopRec.hFirstHalfEdge = sideHEs[0];
        sideLoopRec.cHalfEdges = 4u;

        const auto sideLoopResult = GenerationPool_Insert(
            &pMesh->loops, sideLoopRec );
        if ( sideLoopResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                sideLoopResult.status );
            return result;
        }

        for ( u32 j = 0u; j < 4u; ++j ) {
            mesh_half_edge_record_t *pHE =
                GenerationPool_Get( &pMesh->halfEdges, sideHEs[j] );
            if ( pHE != nullptr ) {
                pHE->hLoop = sideLoopResult.handle;
            }
        }

        // Compute side face normal.
        math::vec3d_t sidePositions[4];
        for ( u32 j = 0u; j < 4u; ++j ) {
            const mesh_vertex_record_t *pV =
                GenerationPool_Get( &pMesh->vertices, sideVerts[j] );
            sidePositions[j] = ( pV != nullptr )
                ? pV->position
                : math::Vec3d_Make( 0.0, 0.0, 0.0 );
        }
        math::vec3d_t sideNormal = math::Vec3d_Make( 0.0, 0.0, 0.0 );
        for ( u32 j = 0u; j < 4u; ++j ) {
            const math::vec3d_t &v0 = sidePositions[j];
            const math::vec3d_t &v1 = sidePositions[( j + 1u ) % 4u];
            sideNormal.x += ( v0.y - v1.y ) * ( v0.z + v1.z );
            sideNormal.y += ( v0.z - v1.z ) * ( v0.x + v1.x );
            sideNormal.z += ( v0.x - v1.x ) * ( v0.y + v1.y );
        }
        const f64 snLenSq = math::Vec3d_LengthSquared( sideNormal );
        if ( snLenSq > 1.0e-24 ) {
            sideNormal = math::Vec3d_Scale( sideNormal,
                1.0 / std::sqrt( snLenSq ) );
        }

        mesh_face_record_t sideFaceRec{};
        sideFaceRec.hOuterLoop = sideLoopResult.handle;
        sideFaceRec.normal = sideNormal;
        sideFaceRec.hShell = hShell;
        sideFaceRec.iSourceSide = CY_INVALID_INDEX;

        const auto sideFaceResult = GenerationPool_Insert(
            &pMesh->faces, sideFaceRec );
        if ( sideFaceResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                sideFaceResult.status );
            return result;
        }

        {
            mesh_loop_record_t *pSL =
                GenerationPool_Get( &pMesh->loops, sideLoopResult.handle );
            if ( pSL != nullptr ) {
                pSL->hFace = sideFaceResult.handle;
            }
        }

        // Twin pairing:
        // sideHEs[0] (origV[iNext] → origV[i]) twins with the adjacent
        //   face's half-edge that was the twin of the original face's HE[i].
        // sideHEs[2] (newV[i] → newV[iNext]) twins with topHEs[i]
        //   (newV[i] → newV[iNext]) — wait, top goes same direction.
        // Actually: topHEs[i] goes newV[i] → newV[i+1].
        //   sideHEs[2] goes newV[i] → newV[iNext] — same direction, not twins.
        //   sideHEs[3] goes newV[iNext] → newV[i] — wait no, let me recheck.
        //
        // Side verts: [origV[iNext], origV[i], newV[i], newV[iNext]]
        //   sideHEs[0]: origV[iNext] → origV[i]
        //   sideHEs[1]: origV[i] → newV[i]
        //   sideHEs[2]: newV[i] → newV[iNext]
        //   sideHEs[3]: newV[iNext] → origV[iNext]
        //
        // topHEs[i]: newV[i] → newV[i+1] = newV[iNext]
        // So sideHEs[2] (newV[i] → newV[iNext]) is the SAME direction
        //   as topHEs[i]. They are NOT twins.
        // Twin of topHEs[i] should go newV[iNext] → newV[i], but that
        //   doesn't appear on this side quad. The previous side quad (i-1)
        //   has sideHEs[3]: newV[i] → origV[i] — no.
        //
        // Let me reconsider. For the top face to be properly twinned with
        // side quads, the side quad's upper edge must go in the opposite
        // direction to the top face's edge.
        //
        // topHEs[i] goes newV[i] → newV[iNext].
        // The side quad for edge i has sideHEs[2]: newV[i] → newV[iNext]
        //   — same direction, not a twin.
        //
        // The twin of topHEs[i] should be on side quad i, going
        //   newV[iNext] → newV[i]. That would be sideHEs with those
        //   endpoints reversed. But in our side quad, sideHEs[2] goes
        //   newV[i] → newV[iNext], which is the wrong direction.
        //
        // I need to adjust. The side quad's edge along the top should
        // run OPPOSITE to the top face's edge. This means the side
        // quad winding needs to be reconsidered.
        //
        // For a proper manifold, adjacent faces along a shared edge
        // must have opposite winding along that edge. The top face runs
        // CCW around its normal. Side quads run CCW around their outward
        // normals.
        //
        // Twin pairs for side quad i:
        //   sideHEs[0] (origV[iNext] → origV[i]) ↔ twinHEs[i] (was the
        //     neighbor's half-edge across the original face edge)
        //   sideHEs[2] (newV[i] → newV[iNext]) ↔ topHEs[i] reversed.
        //     But topHEs[i] goes newV[i] → newV[iNext], so its twin
        //     should go newV[iNext] → newV[i]. sideHEs[2] goes newV[i]
        //     → newV[iNext], which is the SAME — not twin.
        //
        // The issue: the top face and side quads share edges along the
        // new vertices, and the winding must be consistent.
        //
        // Fixing: flip the side quad winding so its top edge opposes
        // the top face's edge direction. If we use the winding:
        //   origV[i] → origV[iNext] → newV[iNext] → newV[i]
        // Then sideHEs[2] = newV[iNext] → newV[i], which twins with
        //   topHEs[i] (newV[i] → newV[iNext]). ✓
        // And sideHEs[0] = origV[i] → origV[iNext], which twins with...
        //   twinHEs[i] goes origV[?] → origV[?]. The original
        //   origHEs[i] went origV[i] → origV[iNext], and twinHEs[i]
        //   went origV[iNext] → origV[i]. So sideHEs[0] = origV[i] →
        //   origV[iNext] would twin with twinHEs[i] origV[iNext] →
        //   origV[i]. ✓
        //
        // So the correct winding is:
        //   origV[i], origV[iNext], newV[iNext], newV[i]
        // Let me rebuild with this corrected winding.

        // We already created the half-edges with the wrong winding.
        // Patch the origins in place.
        {
            mesh_half_edge_record_t *pH0 =
                GenerationPool_Get( &pMesh->halfEdges, sideHEs[0] );
            mesh_half_edge_record_t *pH1 =
                GenerationPool_Get( &pMesh->halfEdges, sideHEs[1] );
            mesh_half_edge_record_t *pH2 =
                GenerationPool_Get( &pMesh->halfEdges, sideHEs[2] );
            mesh_half_edge_record_t *pH3 =
                GenerationPool_Get( &pMesh->halfEdges, sideHEs[3] );
            if ( !pH0 || !pH1 || !pH2 || !pH3 ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
            // Correct winding: origV[i], origV[iNext], newV[iNext], newV[i]
            pH0->hOrigin = origVerts[i];
            pH1->hOrigin = origVerts[iNext];
            pH2->hOrigin = newVerts[iNext];
            pH3->hOrigin = newVerts[i];
        }

        // Recompute the side normal with corrected winding.
        {
            const mesh_vertex_record_t *pV0 =
                GenerationPool_Get( &pMesh->vertices, origVerts[i] );
            const mesh_vertex_record_t *pV1 =
                GenerationPool_Get( &pMesh->vertices, origVerts[iNext] );
            const mesh_vertex_record_t *pV2 =
                GenerationPool_Get( &pMesh->vertices, newVerts[iNext] );
            const mesh_vertex_record_t *pV3 =
                GenerationPool_Get( &pMesh->vertices, newVerts[i] );
            if ( pV0 && pV1 && pV2 && pV3 ) {
                math::vec3d_t pts[4] = {
                    pV0->position, pV1->position,
                    pV2->position, pV3->position
                };
                math::vec3d_t sn = math::Vec3d_Make( 0.0, 0.0, 0.0 );
                for ( u32 j = 0u; j < 4u; ++j ) {
                    const math::vec3d_t &a = pts[j];
                    const math::vec3d_t &b = pts[( j + 1u ) % 4u];
                    sn.x += ( a.y - b.y ) * ( a.z + b.z );
                    sn.y += ( a.z - b.z ) * ( a.x + b.x );
                    sn.z += ( a.x - b.x ) * ( a.y + b.y );
                }
                const f64 lenSq = math::Vec3d_LengthSquared( sn );
                if ( lenSq > 1.0e-24 ) {
                    sn = math::Vec3d_Scale( sn, 1.0 / std::sqrt( lenSq ) );
                }
                mesh_face_record_t *pSF =
                    GenerationPool_Get( &pMesh->faces, sideFaceResult.handle );
                if ( pSF != nullptr ) {
                    pSF->normal = sn;
                }
            }
        }

        // Twin pairing for this side quad:
        // sideHEs[0] (origV[i] → origV[iNext]) ↔ twinHEs[i]
        //   (origV[iNext] → origV[i], the neighbor's half-edge)
        {
            mesh_half_edge_record_t *pS0 =
                GenerationPool_Get( &pMesh->halfEdges, sideHEs[0] );
            mesh_half_edge_record_t *pT =
                GenerationPool_Get( &pMesh->halfEdges, twinHEs[i] );
            if ( pS0 && pT ) {
                pS0->hTwin = twinHEs[i];
                pT->hTwin = sideHEs[0];

                // Reuse the original edge record.
                const mesh_half_edge_record_t *pOrigHE =
                    GenerationPool_Get( &pMesh->halfEdges, origHEs[i] );
                if ( pOrigHE != nullptr ) {
                    pS0->hEdge = pOrigHE->hEdge;
                    pT->hEdge = pOrigHE->hEdge;

                    // Update the edge record to point to sideHEs[0].
                    mesh_edge_record_t *pEdge =
                        GenerationPool_Get( &pMesh->edges, pOrigHE->hEdge );
                    if ( pEdge != nullptr ) {
                        pEdge->hHalfEdge = sideHEs[0];
                    }
                }
            }
        }

        // sideHEs[2] (newV[iNext] → newV[i]) ↔ topHEs[i] (newV[i] → newV[iNext])
        {
            mesh_half_edge_record_t *pS2 =
                GenerationPool_Get( &pMesh->halfEdges, sideHEs[2] );
            mesh_half_edge_record_t *pTH =
                GenerationPool_Get( &pMesh->halfEdges, topHEs[i] );
            if ( pS2 == nullptr || pTH == nullptr ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
            pS2->hTwin = topHEs[i];
            pTH->hTwin = sideHEs[2];

            // Create new edge for this twin pair.
            mesh_edge_record_t edgeRec{};
            edgeRec.hHalfEdge = sideHEs[2];
            const auto edgeResult = GenerationPool_Insert(
                &pMesh->edges, edgeRec );
            if ( edgeResult.status != generation_pool_status_t::OK ) {
                result.status = GeometryStatus_FromGenerationPoolStatus(
                    edgeResult.status );
                return result;
            }
            pS2->hEdge = edgeResult.handle;
            pTH->hEdge = edgeResult.handle;
        }

        // sideHEs[1] (origV[iNext] → newV[iNext]) ↔ next side's
        //   sideHEs[3] (newV[i] → origV[i]) — these are between
        //   adjacent side quads. We handle these after all sides are built.
        // Actually, side i's sideHEs[3] goes newV[i] → origV[i], and
        //   the previous side (i-1)'s sideHEs[1] goes origV[i] →
        //   newV[i]. So they twin. We'll pair them in a second pass.
    }

    // Second pass: twin-pair the vertical edges between adjacent side quads.
    // We need to re-find the side quad half-edges. Since we created them
    // in order, we can re-traverse. But it's simpler to create the edges
    // during the first pass and pair them now.
    //
    // Each side quad i has:
    //   sideHEs[1]: origV[iNext] → newV[iNext]
    //   sideHEs[3]: newV[i] → origV[i]
    //
    // Side i's sideHEs[3] (newV[i] → origV[i]) twins with side (i-1)'s
    //   sideHEs[1] (origV[i] → newV[i]).
    //
    // We stored these in the pool but didn't save handles. We need to
    // find them by scanning. To avoid that, let's collect them during
    // creation. But we already created them. Let me search by vertex.

    // Collect all side HEs by searching for specific vertex-pair patterns.
    // For each vertex origV[i], find the HE going origV[i] → newV[i]
    // (this is some side's sideHEs[1] — specifically side (i-1)'s) and
    // the HE going newV[i] → origV[i] (side i's sideHEs[3]).

    for ( u32 i = 0u; i < cEdges; ++i ) {
        // Find HE: origV[i] → newV[i] — this is side (iPrev)'s sideHEs[1].
        // Find HE: newV[i] → origV[i] — this is side i's sideHEs[3].
        geometry_mesh_half_edge_handle_t hAtoB =
            GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
        geometry_mesh_half_edge_handle_t hBtoA =
            GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;

        (void)GenerationPool_ForEach( &pMesh->halfEdges,
            [&]( geometry_mesh_half_edge_handle_t hHE,
                 const mesh_half_edge_record_t &he ) noexcept -> bool_t {
                if ( !GenerationHandle_IsValid( he.hTwin ) ) {
                    if ( he.hOrigin.nSlot == origVerts[i].nSlot &&
                         he.hOrigin.nGeneration == origVerts[i].nGeneration ) {
                        // Check destination (next HE's origin).
                        const mesh_half_edge_record_t *pNext =
                            GenerationPool_Get( &pMesh->halfEdges, he.hNext );
                        if ( pNext != nullptr &&
                             pNext->hOrigin.nSlot == newVerts[i].nSlot &&
                             pNext->hOrigin.nGeneration == newVerts[i].nGeneration ) {
                            hAtoB = hHE;
                        }
                    }
                    if ( he.hOrigin.nSlot == newVerts[i].nSlot &&
                         he.hOrigin.nGeneration == newVerts[i].nGeneration ) {
                        const mesh_half_edge_record_t *pNext =
                            GenerationPool_Get( &pMesh->halfEdges, he.hNext );
                        if ( pNext != nullptr &&
                             pNext->hOrigin.nSlot == origVerts[i].nSlot &&
                             pNext->hOrigin.nGeneration == origVerts[i].nGeneration ) {
                            hBtoA = hHE;
                        }
                    }
                }
                return !( GenerationHandle_IsValid( hAtoB ) &&
                          GenerationHandle_IsValid( hBtoA ) );
            } );

        if ( !GenerationHandle_IsValid( hAtoB ) ||
             !GenerationHandle_IsValid( hBtoA ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        mesh_half_edge_record_t *pA =
            GenerationPool_Get( &pMesh->halfEdges, hAtoB );
        mesh_half_edge_record_t *pB =
            GenerationPool_Get( &pMesh->halfEdges, hBtoA );
        if ( pA == nullptr || pB == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        pA->hTwin = hBtoA;
        pB->hTwin = hAtoB;

        mesh_edge_record_t edgeRec{};
        edgeRec.hHalfEdge = hAtoB;
        const auto edgeResult = GenerationPool_Insert(
            &pMesh->edges, edgeRec );
        if ( edgeResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                edgeResult.status );
            return result;
        }
        pA->hEdge = edgeResult.handle;
        pB->hEdge = edgeResult.handle;
    }

    // Remove the original face, its loop, and its half-edges.
    {
        (void)GenerationPool_Remove( &pMesh->faces, hFace );
        (void)GenerationPool_Remove( &pMesh->loops, hOriginalLoop );
        for ( u32 i = 0u; i < cEdges; ++i ) {
            (void)GenerationPool_Remove( &pMesh->halfEdges, origHEs[i] );
        }
    }

    // Fix original vertices' outgoing half-edge pointers — they may have
    // pointed to the now-removed original face's half-edges.
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_vertex_record_t *pV =
            GenerationPool_Get( &pMesh->vertices, origVerts[i] );
        if ( pV == nullptr ) { continue; }
        if ( !GenerationPool_Contains( &pMesh->halfEdges, pV->hOutHalfEdge ) ) {
            // Find any surviving outgoing HE.
            (void)GenerationPool_ForEach( &pMesh->halfEdges,
                [&]( geometry_mesh_half_edge_handle_t h,
                     const mesh_half_edge_record_t &rec ) noexcept -> bool_t {
                    if ( rec.hOrigin.nSlot == origVerts[i].nSlot &&
                         rec.hOrigin.nGeneration == origVerts[i].nGeneration ) {
                        pV->hOutHalfEdge = h;
                        return false;
                    }
                    return true;
                } );
        }
    }

    // Update shell face count: removed 1 original, added 1 top + N sides.
    {
        mesh_shell_record_t *pShell =
            GenerationPool_Get( &pMesh->shells, hShell );
        if ( pShell != nullptr ) {
            pShell->cFaces += cEdges;
            if ( pShell->hAnyFace.nSlot == hFace.nSlot &&
                 pShell->hAnyFace.nGeneration == hFace.nGeneration ) {
                pShell->hAnyFace = topFaceResult.handle;
            }
        }
    }

    result.hExtrudedFace = topFaceResult.handle;
    result.status = geometry_status_t::OK;
    return result;
}

mesh_extrude_face_result_t MeshOps_ExtrudeFace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    f64 distance ) noexcept
{
    mesh_extrude_face_result_t result{};
    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( distance ) ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( distance == 0.0 ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }
    if ( GenerationPool_Get( &pMesh->faces, hFace ) == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    editable_mesh_t stagedMesh{};
    result.status = CloneEditableMeshExact( pMesh, &stagedMesh );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    result = MeshOps_ExtrudeFaceInPlace(
        &stagedMesh, hFace, distance );
    if ( result.status == geometry_status_t::OK ) {
        SwapEditableMeshStorage( pMesh, &stagedMesh );
    } else {
        result.hExtrudedFace = {};
    }
    EditableMesh_Shutdown( &stagedMesh );
    return result;
}

// ---------------------------------------------------------------------------
// InsetFace
// ---------------------------------------------------------------------------

static mesh_inset_face_result_t MeshOps_InsetFaceInPlace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    f64 margin ) noexcept
{
    mesh_inset_face_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( margin <= 0.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    const mesh_face_record_t *pFace =
        GenerationPool_Get( &pMesh->faces, hFace );
    if ( pFace == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    const math::vec3d_t faceNormal = pFace->normal;
    const geometry_mesh_shell_handle_t hShell = pFace->hShell;
    // Face insertion below may reallocate the pool. Keep no face-pool pointer
    // alive across that mutation.
    const geometry_mesh_loop_handle_t hOriginalLoop = pFace->hOuterLoop;

    const mesh_loop_record_t *pLoop =
        GenerationPool_Get( &pMesh->loops, hOriginalLoop );
    if ( pLoop == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    const u32 cEdges = pLoop->cHalfEdges;
    if ( cEdges < 3u || cEdges > 256u ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }

    // Collect original boundary.
    geometry_mesh_vertex_handle_t origVerts[256];
    geometry_mesh_half_edge_handle_t origHEs[256];
    math::vec3d_t origPositions[256];

    {
        geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
        for ( u32 i = 0u; i < cEdges; ++i ) {
            const mesh_half_edge_record_t *pHE =
                GenerationPool_Get( &pMesh->halfEdges, hCur );
            if ( pHE == nullptr ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
            origVerts[i] = pHE->hOrigin;
            origHEs[i] = hCur;

            const mesh_vertex_record_t *pV =
                GenerationPool_Get( &pMesh->vertices, pHE->hOrigin );
            if ( pV == nullptr ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
            origPositions[i] = pV->position;
            hCur = pHE->hNext;
        }
    }

    // Compute the centroid of the face.
    math::vec3d_t centroid = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    for ( u32 i = 0u; i < cEdges; ++i ) {
        centroid = math::Vec3d_Add( centroid, origPositions[i] );
    }
    centroid = math::Vec3d_Scale( centroid, 1.0 / static_cast<f64>( cEdges ) );

    // Compute inset positions: move each vertex toward the centroid by
    // `margin` along the face plane. We project the direction from vertex
    // to centroid onto the face plane to get the inset direction.
    math::vec3d_t insetPositions[256];
    for ( u32 i = 0u; i < cEdges; ++i ) {
        math::vec3d_t toCenter = math::Vec3d_Subtract(
            centroid, origPositions[i] );

        // Project onto face plane (remove normal component).
        const f64 normalComp = math::Vec3d_Dot( toCenter, faceNormal );
        toCenter = math::Vec3d_Subtract( toCenter,
            math::Vec3d_Scale( faceNormal, normalComp ) );

        const f64 distSq = math::Vec3d_LengthSquared( toCenter );
        if ( distSq < 1.0e-20 ) {
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }

        const f64 dist = std::sqrt( distSq );
        if ( margin >= dist ) {
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }

        // Move margin units toward centroid.
        const math::vec3d_t dir = math::Vec3d_Scale( toCenter, 1.0 / dist );
        insetPositions[i] = math::Vec3d_Add( origPositions[i],
            math::Vec3d_Scale( dir, margin ) );
        if ( !math::Vec3d_IsFinite( insetPositions[i] ) ) {
            result.status = geometry_status_t::NUMERIC_FAILURE;
            return result;
        }
        if ( math::Vec3d_EqualsExact(
                 insetPositions[i], origPositions[i] ) ) {
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }
    }

    // Create N inset vertices.
    geometry_mesh_vertex_handle_t insetVerts[256];
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_vertex_record_t vRec{};
        vRec.position = insetPositions[i];
        const auto vResult = GenerationPool_Insert( &pMesh->vertices, vRec );
        if ( vResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                vResult.status );
            return result;
        }
        insetVerts[i] = vResult.handle;
    }

    // Save twin handles of original face edges before removing the face.
    geometry_mesh_half_edge_handle_t twinHEs[256];
    for ( u32 i = 0u; i < cEdges; ++i ) {
        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, origHEs[i] );
        twinHEs[i] = ( pHE != nullptr ) ? pHE->hTwin
            : GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
    }

    // Create the inset face (center face with smaller boundary).
    geometry_mesh_half_edge_handle_t insetHEs[256];
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_half_edge_record_t heRec{};
        heRec.hOrigin = insetVerts[i];
        const auto heResult = GenerationPool_Insert( &pMesh->halfEdges, heRec );
        if ( heResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                heResult.status );
            return result;
        }
        insetHEs[i] = heResult.handle;
    }

    // Link inset face half-edges.
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, insetHEs[i] );
        if ( pHE == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        pHE->hNext = insetHEs[( i + 1u ) % cEdges];
        pHE->hPrev = insetHEs[( i + cEdges - 1u ) % cEdges];
    }

    // Create inset loop and face.
    mesh_loop_record_t insetLoopRec{};
    insetLoopRec.hFirstHalfEdge = insetHEs[0];
    insetLoopRec.cHalfEdges = cEdges;

    const auto insetLoopResult = GenerationPool_Insert(
        &pMesh->loops, insetLoopRec );
    if ( insetLoopResult.status != generation_pool_status_t::OK ) {
        result.status = GeometryStatus_FromGenerationPoolStatus(
            insetLoopResult.status );
        return result;
    }

    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, insetHEs[i] );
        if ( pHE != nullptr ) {
            pHE->hLoop = insetLoopResult.handle;
        }
    }

    mesh_face_record_t insetFaceRec{};
    insetFaceRec.hOuterLoop = insetLoopResult.handle;
    insetFaceRec.normal = faceNormal;
    insetFaceRec.hShell = hShell;
    insetFaceRec.iSourceSide = CY_INVALID_INDEX;

    const auto insetFaceResult = GenerationPool_Insert(
        &pMesh->faces, insetFaceRec );
    if ( insetFaceResult.status != generation_pool_status_t::OK ) {
        result.status = GeometryStatus_FromGenerationPoolStatus(
            insetFaceResult.status );
        return result;
    }

    {
        mesh_loop_record_t *pIL =
            GenerationPool_Get( &pMesh->loops, insetLoopResult.handle );
        if ( pIL != nullptr ) {
            pIL->hFace = insetFaceResult.handle;
        }
    }

    // Set outgoing half-edges on inset vertices.
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_vertex_record_t *pV =
            GenerationPool_Get( &pMesh->vertices, insetVerts[i] );
        if ( pV != nullptr ) {
            pV->hOutHalfEdge = insetHEs[i];
        }
    }

    // Create N ring quads connecting original boundary to inset boundary.
    // Each quad: origV[i] → origV[iNext] → insetV[iNext] → insetV[i]
    // (CCW winding, same normal direction as original face).

    // Store all ring quad HE handles for twin pairing between adjacent quads.
    // ringHEs[i][0..3]: the 4 HEs of ring quad i.
    geometry_mesh_half_edge_handle_t ringHEs[256][4];

    for ( u32 i = 0u; i < cEdges; ++i ) {
        const u32 iNext = ( i + 1u ) % cEdges;

        const geometry_mesh_vertex_handle_t qVerts[4] = {
            origVerts[i], origVerts[iNext], insetVerts[iNext], insetVerts[i]
        };

        for ( u32 j = 0u; j < 4u; ++j ) {
            mesh_half_edge_record_t heRec{};
            heRec.hOrigin = qVerts[j];
            const auto heResult = GenerationPool_Insert(
                &pMesh->halfEdges, heRec );
            if ( heResult.status != generation_pool_status_t::OK ) {
                result.status = GeometryStatus_FromGenerationPoolStatus(
                    heResult.status );
                return result;
            }
            ringHEs[i][j] = heResult.handle;
        }

        // Link next/prev.
        for ( u32 j = 0u; j < 4u; ++j ) {
            mesh_half_edge_record_t *pHE =
                GenerationPool_Get( &pMesh->halfEdges, ringHEs[i][j] );
            if ( pHE == nullptr ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
            pHE->hNext = ringHEs[i][( j + 1u ) % 4u];
            pHE->hPrev = ringHEs[i][( j + 3u ) % 4u];
        }

        // Create loop.
        mesh_loop_record_t rLoopRec{};
        rLoopRec.hFirstHalfEdge = ringHEs[i][0];
        rLoopRec.cHalfEdges = 4u;

        const auto rLoopResult = GenerationPool_Insert(
            &pMesh->loops, rLoopRec );
        if ( rLoopResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                rLoopResult.status );
            return result;
        }

        for ( u32 j = 0u; j < 4u; ++j ) {
            mesh_half_edge_record_t *pHE =
                GenerationPool_Get( &pMesh->halfEdges, ringHEs[i][j] );
            if ( pHE != nullptr ) {
                pHE->hLoop = rLoopResult.handle;
            }
        }

        // Create face with the same normal as the original.
        mesh_face_record_t rFaceRec{};
        rFaceRec.hOuterLoop = rLoopResult.handle;
        rFaceRec.normal = faceNormal;
        rFaceRec.hShell = hShell;
        rFaceRec.iSourceSide = CY_INVALID_INDEX;

        const auto rFaceResult = GenerationPool_Insert(
            &pMesh->faces, rFaceRec );
        if ( rFaceResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                rFaceResult.status );
            return result;
        }

        {
            mesh_loop_record_t *pRL =
                GenerationPool_Get( &pMesh->loops, rLoopResult.handle );
            if ( pRL != nullptr ) {
                pRL->hFace = rFaceResult.handle;
            }
        }

        // Twin pairing:
        // ringHEs[i][0] (origV[i] → origV[iNext]) ↔ twinHEs[i]
        {
            mesh_half_edge_record_t *pR0 =
                GenerationPool_Get( &pMesh->halfEdges, ringHEs[i][0] );
            mesh_half_edge_record_t *pT =
                GenerationPool_Get( &pMesh->halfEdges, twinHEs[i] );
            if ( pR0 && pT ) {
                pR0->hTwin = twinHEs[i];
                pT->hTwin = ringHEs[i][0];

                // Reuse original edge.
                const mesh_half_edge_record_t *pOrigHE =
                    GenerationPool_Get( &pMesh->halfEdges, origHEs[i] );
                if ( pOrigHE != nullptr ) {
                    pR0->hEdge = pOrigHE->hEdge;
                    pT->hEdge = pOrigHE->hEdge;
                    mesh_edge_record_t *pEdge =
                        GenerationPool_Get( &pMesh->edges, pOrigHE->hEdge );
                    if ( pEdge != nullptr ) {
                        pEdge->hHalfEdge = ringHEs[i][0];
                    }
                }
            }
        }

        // ringHEs[i][2] (insetV[iNext] → insetV[i]) ↔ insetHEs[i]
        //   (insetV[i] → insetV[iNext])
        {
            mesh_half_edge_record_t *pR2 =
                GenerationPool_Get( &pMesh->halfEdges, ringHEs[i][2] );
            mesh_half_edge_record_t *pIH =
                GenerationPool_Get( &pMesh->halfEdges, insetHEs[i] );
            if ( pR2 == nullptr || pIH == nullptr ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
            pR2->hTwin = insetHEs[i];
            pIH->hTwin = ringHEs[i][2];

            mesh_edge_record_t edgeRec{};
            edgeRec.hHalfEdge = ringHEs[i][2];
            const auto edgeResult = GenerationPool_Insert(
                &pMesh->edges, edgeRec );
            if ( edgeResult.status != generation_pool_status_t::OK ) {
                result.status = GeometryStatus_FromGenerationPoolStatus(
                    edgeResult.status );
                return result;
            }
            pR2->hEdge = edgeResult.handle;
            pIH->hEdge = edgeResult.handle;
        }
    }

    // Twin-pair the vertical edges between adjacent ring quads.
    // ringHEs[i][1] (origV[iNext] → insetV[iNext]) ↔
    //   ringHEs[iNext][3] (insetV[iNext] → origV[iNext])
    for ( u32 i = 0u; i < cEdges; ++i ) {
        const u32 iNext = ( i + 1u ) % cEdges;

        mesh_half_edge_record_t *pA =
            GenerationPool_Get( &pMesh->halfEdges, ringHEs[i][1] );
        mesh_half_edge_record_t *pB =
            GenerationPool_Get( &pMesh->halfEdges, ringHEs[iNext][3] );
        if ( pA == nullptr || pB == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        pA->hTwin = ringHEs[iNext][3];
        pB->hTwin = ringHEs[i][1];

        mesh_edge_record_t edgeRec{};
        edgeRec.hHalfEdge = ringHEs[i][1];
        const auto edgeResult = GenerationPool_Insert(
            &pMesh->edges, edgeRec );
        if ( edgeResult.status != generation_pool_status_t::OK ) {
            result.status = GeometryStatus_FromGenerationPoolStatus(
                edgeResult.status );
            return result;
        }
        pA->hEdge = edgeResult.handle;
        pB->hEdge = edgeResult.handle;
    }

    // Remove original face, loop, and half-edges.
    {
        (void)GenerationPool_Remove( &pMesh->faces, hFace );
        (void)GenerationPool_Remove( &pMesh->loops, hOriginalLoop );
        for ( u32 i = 0u; i < cEdges; ++i ) {
            (void)GenerationPool_Remove( &pMesh->halfEdges, origHEs[i] );
        }
    }

    // Fix original vertices' outgoing half-edge pointers.
    for ( u32 i = 0u; i < cEdges; ++i ) {
        mesh_vertex_record_t *pV =
            GenerationPool_Get( &pMesh->vertices, origVerts[i] );
        if ( pV == nullptr ) { continue; }
        if ( !GenerationPool_Contains( &pMesh->halfEdges, pV->hOutHalfEdge ) ) {
            (void)GenerationPool_ForEach( &pMesh->halfEdges,
                [&]( geometry_mesh_half_edge_handle_t h,
                     const mesh_half_edge_record_t &rec ) noexcept -> bool_t {
                    if ( rec.hOrigin.nSlot == origVerts[i].nSlot &&
                         rec.hOrigin.nGeneration == origVerts[i].nGeneration ) {
                        pV->hOutHalfEdge = h;
                        return false;
                    }
                    return true;
                } );
        }
    }

    // Update shell: removed 1 face, added 1 inset + N ring quads.
    {
        mesh_shell_record_t *pShell =
            GenerationPool_Get( &pMesh->shells, hShell );
        if ( pShell != nullptr ) {
            if ( pShell->hAnyFace.nSlot == hFace.nSlot &&
                 pShell->hAnyFace.nGeneration == hFace.nGeneration ) {
                pShell->hAnyFace = insetFaceResult.handle;
            }
            pShell->cFaces += cEdges;
        }
    }

    result.hInsetFace = insetFaceResult.handle;
    result.status = geometry_status_t::OK;
    return result;
}

mesh_inset_face_result_t MeshOps_InsetFace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    f64 margin ) noexcept
{
    mesh_inset_face_result_t result{};
    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( margin ) || margin <= 0.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( GenerationPool_Get( &pMesh->faces, hFace ) == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    editable_mesh_t stagedMesh{};
    result.status = CloneEditableMeshExact( pMesh, &stagedMesh );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    result = MeshOps_InsetFaceInPlace(
        &stagedMesh, hFace, margin );
    if ( result.status == geometry_status_t::OK ) {
        SwapEditableMeshStorage( pMesh, &stagedMesh );
    } else {
        result.hInsetFace = {};
    }
    EditableMesh_Shutdown( &stagedMesh );
    return result;
}

// ---------------------------------------------------------------------------
// LoopCut
// ---------------------------------------------------------------------------

static mesh_loop_cut_result_t MeshOps_LoopCutInPlace(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hStartEdge,
    f64 t ) noexcept
{
    mesh_loop_cut_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( t <= 0.0 || t >= 1.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    const mesh_edge_record_t *pStartEdge =
        GenerationPool_Get( &pMesh->edges, hStartEdge );
    if ( pStartEdge == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    // Discover the edge loop: starting from hStartEdge, follow the quad
    // strip by crossing to the opposite edge of each quad face.
    //
    // For a quad face, the "opposite" edge is the one across from the
    // current edge (separated by exactly 2 half-edges in the loop).
    //
    // Collect edges to split and the faces they belong to.
    struct loop_entry_t {
        geometry_mesh_edge_handle_t hEdge;
        geometry_mesh_face_handle_t hFace;
    };

    loop_entry_t loopEntries[256];
    u32 cLoopEntries = 0u;

    geometry_mesh_edge_handle_t hCurEdge = hStartEdge;
    u32 safety = 0u;

    do {
        if ( cLoopEntries >= 256u ) {
            result.status = geometry_status_t::LIMIT_EXCEEDED;
            return result;
        }

        const mesh_edge_record_t *pCurEdge =
            GenerationPool_Get( &pMesh->edges, hCurEdge );
        if ( pCurEdge == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        // Pick a half-edge and find its face.
        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, pCurEdge->hHalfEdge );
        if ( pHE == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        // Determine which face to cross. We need the face we haven't
        // visited yet. For the first edge, use the half-edge's face.
        // For subsequent edges, use the face on the side we came from
        // (the twin's face), but actually we want to continue forward,
        // so we use the face we haven't processed.
        geometry_mesh_half_edge_handle_t hCrossHE = pCurEdge->hHalfEdge;

        // If we came from a previous face, pick the side that's the new face.
        if ( cLoopEntries > 0u ) {
            const mesh_loop_record_t *pLp =
                GenerationPool_Get( &pMesh->loops, pHE->hLoop );
            if ( pLp != nullptr &&
                 pLp->hFace.nSlot == loopEntries[cLoopEntries - 1u].hFace.nSlot &&
                 pLp->hFace.nGeneration == loopEntries[cLoopEntries - 1u].hFace.nGeneration ) {
                // This side is the previous face, use twin's side.
                hCrossHE = pHE->hTwin;
            }
        }

        const mesh_half_edge_record_t *pCrossHE =
            GenerationPool_Get( &pMesh->halfEdges, hCrossHE );
        if ( pCrossHE == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        const mesh_loop_record_t *pCrossLoop =
            GenerationPool_Get( &pMesh->loops, pCrossHE->hLoop );
        if ( pCrossLoop == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        // Only proceed through quad faces.
        if ( pCrossLoop->cHalfEdges != 4u ) {
            break;
        }

        loopEntries[cLoopEntries].hEdge = hCurEdge;
        loopEntries[cLoopEntries].hFace = pCrossLoop->hFace;
        ++cLoopEntries;

        // Find the opposite edge: advance 2 half-edges in the loop from
        // the current half-edge to reach the opposite side.
        const mesh_half_edge_record_t *pNext1 =
            GenerationPool_Get( &pMesh->halfEdges, pCrossHE->hNext );
        if ( pNext1 == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        const mesh_half_edge_record_t *pNext2 =
            GenerationPool_Get( &pMesh->halfEdges, pNext1->hNext );
        if ( pNext2 == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        hCurEdge = pNext2->hEdge;

        if ( ++safety > 256u ) { break; }
    } while ( hCurEdge.nSlot != hStartEdge.nSlot ||
              hCurEdge.nGeneration != hStartEdge.nGeneration );

    if ( cLoopEntries == 0u ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Split each edge in the loop using SplitEdge, then split each face
    // using SplitFace to connect the new vertices.
    geometry_mesh_vertex_handle_t newVerts[256];

    for ( u32 i = 0u; i < cLoopEntries; ++i ) {
        const auto splitResult = MeshOps_SplitEdge(
            pMesh, loopEntries[i].hEdge, t );
        if ( splitResult.status != geometry_status_t::OK ) {
            result.status = splitResult.status;
            return result;
        }
        newVerts[i] = splitResult.hNewVertex;
    }

    // Now split each face by connecting consecutive new vertices.
    for ( u32 i = 0u; i < cLoopEntries; ++i ) {
        // Find the face that contains both newVerts[i] and newVerts[(i+1)%N]
        // (or for the last entry, if the loop is closed, connect back).
        const u32 iNext = ( i + 1u ) % cLoopEntries;

        // After splitting edges, the face should still contain both new
        // vertices. But we may need to re-find the face handle since
        // SplitEdge doesn't change face handles.
        //
        // However, SplitFace might have split the face we need. We need to
        // find the current face containing both vertices.
        geometry_mesh_face_handle_t hTargetFace =
            GEOMETRY_HANDLE_INVALID<geometry_mesh_face_tag_t>;

        // Search for a face containing both newVerts[i] and newVerts[iNext].
        (void)GenerationPool_ForEach( &pMesh->faces,
            [&]( geometry_mesh_face_handle_t hF,
                 const mesh_face_record_t &face ) noexcept -> bool_t {
                const mesh_loop_record_t *pL =
                    GenerationPool_Get( &pMesh->loops, face.hOuterLoop );
                if ( pL == nullptr ) { return true; }

                bool foundI = false;
                bool foundNext = false;

                geometry_mesh_half_edge_handle_t hW = pL->hFirstHalfEdge;
                for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                    const mesh_half_edge_record_t *pW =
                        GenerationPool_Get( &pMesh->halfEdges, hW );
                    if ( pW == nullptr ) { break; }
                    if ( pW->hOrigin.nSlot == newVerts[i].nSlot &&
                         pW->hOrigin.nGeneration == newVerts[i].nGeneration ) {
                        foundI = true;
                    }
                    if ( pW->hOrigin.nSlot == newVerts[iNext].nSlot &&
                         pW->hOrigin.nGeneration == newVerts[iNext].nGeneration ) {
                        foundNext = true;
                    }
                    hW = pW->hNext;
                }

                if ( foundI && foundNext ) {
                    hTargetFace = hF;
                    return false;
                }
                return true;
            } );

        if ( !GenerationHandle_IsValid( hTargetFace ) ) {
            // If the loop isn't closed and this is the last entry, we're done.
            if ( iNext == 0u && ( hCurEdge.nSlot != hStartEdge.nSlot ||
                                   hCurEdge.nGeneration != hStartEdge.nGeneration ) ) {
                continue;
            }
            result.status = geometry_status_t::INVALID_TOPOLOGY;
            return result;
        }

        const auto faceResult = MeshOps_SplitFace(
            pMesh, hTargetFace, newVerts[i], newVerts[iNext] );
        if ( faceResult.status != geometry_status_t::OK ) {
            result.status = faceResult.status;
            return result;
        }
        ++result.cFacesSplit;
    }

    result.status = geometry_status_t::OK;
    return result;
}

mesh_loop_cut_result_t MeshOps_LoopCut(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hStartEdge,
    f64 t ) noexcept
{
    mesh_loop_cut_result_t result{};
    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( t ) || t <= 0.0 || t >= 1.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( GenerationPool_Get( &pMesh->edges, hStartEdge ) == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    editable_mesh_t stagedMesh{};
    result.status = CloneEditableMeshExact( pMesh, &stagedMesh );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    result = MeshOps_LoopCutInPlace(
        &stagedMesh, hStartEdge, t );
    if ( result.status == geometry_status_t::OK ) {
        SwapEditableMeshStorage( pMesh, &stagedMesh );
    } else {
        result.cFacesSplit = 0u;
    }
    EditableMesh_Shutdown( &stagedMesh );
    return result;
}

// ---------------------------------------------------------------------------
// WeldVertices
// ---------------------------------------------------------------------------

mesh_weld_result_t MeshOps_WeldVertices(
    editable_mesh_t *pMesh,
    f64 tolerance ) noexcept
{
    mesh_weld_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( tolerance ) || tolerance <= 0.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    const f64 tolSq = tolerance * tolerance;

    // Collect all vertex handles and positions up front. We can't iterate
    // the pool while mutating it (CollapseEdge removes elements).
    struct vert_entry_t {
        geometry_mesh_vertex_handle_t handle;
        math::vec3d_t position;
        bool removed;
    };

    vector_t<vert_entry_t> verts{};
    const usize cVertexCount = GenerationPool_Count( &pMesh->vertices );
    if ( !Vector_Init( &verts, pMesh->pAllocator, cVertexCount ) ) {
        result.status = geometry_status_t::ALLOCATION_FAILED;
        return result;
    }

    bool bCollected = true;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t h,
             const mesh_vertex_record_t &v ) noexcept -> bool_t {
            if ( !Vector_PushBack(
                     &verts, vert_entry_t{ h, v.position, false } ) ) {
                bCollected = false;
                return false;
            }
            return true;
        } );
    if ( !bCollected || verts.nCount != cVertexCount ) {
        result.status = geometry_status_t::ALLOCATION_FAILED;
        return result;
    }

    // O(n²) scan for close pairs. For editor-scale meshes (< 65k verts)
    // this is acceptable; spatial hashing could be added later if profiling
    // shows a bottleneck.
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        if ( verts.pData[i].removed ) { continue; }

        for ( usize j = i + 1u; j < verts.nCount; ++j ) {
            if ( verts.pData[j].removed ) { continue; }

            const math::vec3d_t diff = math::Vec3d_Subtract(
                verts.pData[i].position, verts.pData[j].position );
            if ( math::Vec3d_LengthSquared( diff ) > tolSq ) {
                continue;
            }

            // These two vertices are close enough — find and collapse
            // the edge between them (if one exists).
            geometry_mesh_edge_handle_t hSharedEdge =
                GEOMETRY_HANDLE_INVALID<geometry_mesh_edge_tag_t>;

            // Walk i's half-edge fan looking for j.
            const mesh_vertex_record_t *pVI =
                GenerationPool_Get( &pMesh->vertices, verts.pData[i].handle );
            if ( pVI == nullptr ) { continue; }

            const geometry_mesh_half_edge_handle_t hStart =
                pVI->hOutHalfEdge;
            geometry_mesh_half_edge_handle_t hCur = hStart;
            u32 safety = 0u;

            do {
                const mesh_half_edge_record_t *pHE =
                    GenerationPool_Get( &pMesh->halfEdges, hCur );
                if ( pHE == nullptr ) { break; }

                const mesh_half_edge_record_t *pTwin =
                    GenerationPool_Get( &pMesh->halfEdges, pHE->hTwin );
                if ( pTwin == nullptr ) { break; }

                if ( pTwin->hOrigin.nSlot == verts.pData[j].handle.nSlot &&
                     pTwin->hOrigin.nGeneration ==
                         verts.pData[j].handle.nGeneration ) {
                    hSharedEdge = pHE->hEdge;
                    break;
                }

                hCur = pTwin->hNext;
                if ( ++safety > 256u ) { break; }
            } while ( hCur.nSlot != hStart.nSlot ||
                      hCur.nGeneration != hStart.nGeneration );

            if ( !GenerationHandle_IsValid( hSharedEdge ) ) {
                continue;
            }

            const auto collapseResult = MeshOps_CollapseEdge(
                pMesh, hSharedEdge );
            if ( collapseResult.status == geometry_status_t::OK ) {
                // Mark the removed vertex so we skip it.
                if ( collapseResult.hSurvivor.nSlot ==
                         verts.pData[i].handle.nSlot &&
                     collapseResult.hSurvivor.nGeneration ==
                         verts.pData[i].handle.nGeneration ) {
                    verts.pData[j].removed = true;
                } else {
                    verts.pData[i].removed = true;
                }
                ++result.cVerticesMerged;
            }
        }
    }

    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// SetEdgeCreaseWeight / GetEdgeCreaseWeight (Gate 15)
// ---------------------------------------------------------------------------

geometry_status_t MeshOps_SetEdgeCreaseWeight(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge,
    f64 weight ) noexcept
{
    if ( pMesh == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !std::isfinite( weight ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    mesh_edge_record_t *pEdge =
        GenerationPool_Get( &pMesh->edges, hEdge );
    if ( pEdge == nullptr ) {
        return geometry_status_t::INVALID_HANDLE;
    }

    // Clamp to [0, 1].
    if ( weight < 0.0 ) { weight = 0.0; }
    if ( weight > 1.0 ) { weight = 1.0; }

    pEdge->creaseWeight = weight;
    return geometry_status_t::OK;
}

f64 MeshOps_GetEdgeCreaseWeight(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept
{
    if ( pMesh == nullptr ) { return 0.0; }

    const mesh_edge_record_t *pEdge =
        GenerationPool_Get( &pMesh->edges, hEdge );
    if ( pEdge == nullptr ) { return 0.0; }

    return pEdge->creaseWeight;
}

// ---------------------------------------------------------------------------
// SelectEdgeRing (Gate 15)
// ---------------------------------------------------------------------------

mesh_edge_selection_result_t MeshOps_SelectEdgeRing(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hStartEdge ) noexcept
{
    mesh_edge_selection_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    const mesh_edge_record_t *pStartEdge =
        GenerationPool_Get( &pMesh->edges, hStartEdge );
    if ( pStartEdge == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    // Edge ring: walk across quad faces by jumping to the opposite edge.
    // The half-edge on which the walk enters the next face is part of the
    // traversal state. Looking the edge's representative half-edge up again
    // can select the face we just left and make the walk bounce between two
    // edges.
    geometry_mesh_edge_handle_t hCurEdge = hStartEdge;
    geometry_mesh_half_edge_handle_t hCurHalfEdge =
        pStartEdge->hHalfEdge;

    while ( true ) {
        if ( result.cEdges >= 256u ) {
            result.status = geometry_status_t::LIMIT_EXCEEDED;
            return result;
        }

        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, hCurHalfEdge );
        if ( pHE == nullptr || !HandlesEqual( pHE->hEdge, hCurEdge ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        for ( u32 i = 0u; i < result.cEdges; ++i ) {
            if ( HandlesEqual( result.edges[i], hCurEdge ) ) {
                result.status = geometry_status_t::CORRUPT_STATE;
                return result;
            }
        }
        result.edges[result.cEdges++] = hCurEdge;

        const mesh_loop_record_t *pLoop =
            GenerationPool_Get( &pMesh->loops, pHE->hLoop );
        if ( pLoop == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        if ( pLoop->cHalfEdges != 4u ) {
            result.status = geometry_status_t::OK;
            return result;
        }

        // Walk 2 half-edges forward to get the opposite edge, then keep the
        // opposite half-edge's twin so the next step continues in the face
        // across that edge.
        const mesh_half_edge_record_t *pNext =
            GenerationPool_Get( &pMesh->halfEdges, pHE->hNext );
        const mesh_half_edge_record_t *pOpposite = pNext != nullptr
            ? GenerationPool_Get( &pMesh->halfEdges, pNext->hNext )
            : nullptr;
        const mesh_half_edge_record_t *pTwin = pOpposite != nullptr
            ? GenerationPool_Get( &pMesh->halfEdges, pOpposite->hTwin )
            : nullptr;
        if ( pNext == nullptr || pOpposite == nullptr || pTwin == nullptr ||
             GenerationPool_Get( &pMesh->edges, pTwin->hEdge ) == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }

        hCurHalfEdge = pOpposite->hTwin;
        hCurEdge = pTwin->hEdge;
        if ( HandlesEqual( hCurEdge, hStartEdge ) ) {
            result.status = geometry_status_t::OK;
            return result;
        }
    }
}

// ---------------------------------------------------------------------------
// SelectEdgeLoop (Gate 15)
// ---------------------------------------------------------------------------

mesh_edge_selection_result_t MeshOps_SelectEdgeLoop(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hStartEdge ) noexcept
{
    mesh_edge_selection_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    const mesh_edge_record_t *pStartEdge =
        GenerationPool_Get( &pMesh->edges, hStartEdge );
    if ( pStartEdge == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    // Edge loop: from the start edge, follow connected edges end-to-end.
    // At each vertex, continue to the "opposite" edge in the vertex fan.
    // For a vertex with valence 4 (surrounded by quads), the opposite
    // edge is the one 2 half-edges away in the fan.
    //
    // Walk one direction first (forward from one endpoint).

    u32 safety = 0u;

    // Get the initial half-edge to determine direction.
    const mesh_half_edge_record_t *pStartHE =
        GenerationPool_Get( &pMesh->halfEdges, pStartEdge->hHalfEdge );
    if ( pStartHE == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    geometry_mesh_half_edge_handle_t hCurHE = pStartEdge->hHalfEdge;

    do {
        if ( result.cEdges >= 256u ) { break; }

        const mesh_half_edge_record_t *pCurHE =
            GenerationPool_Get( &pMesh->halfEdges, hCurHE );
        if ( pCurHE == nullptr ) { break; }

        result.edges[result.cEdges++] = pCurHE->hEdge;

        // At the destination vertex (twin's origin), find the opposite
        // outgoing edge. Walk: twin → next → twin → next to get to the
        // edge 2 faces away in the vertex fan.
        const mesh_half_edge_record_t *pTwin =
            GenerationPool_Get( &pMesh->halfEdges, pCurHE->hTwin );
        if ( pTwin == nullptr ) { break; }

        // Check if the face is a quad for clean loop continuation.
        const mesh_loop_record_t *pLoop =
            GenerationPool_Get( &pMesh->loops, pTwin->hLoop );
        if ( pLoop == nullptr || pLoop->cHalfEdges != 4u ) { break; }

        // Walk 2 half-edges in the twin's loop: next → next.
        const mesh_half_edge_record_t *pN1 =
            GenerationPool_Get( &pMesh->halfEdges, pTwin->hNext );
        if ( pN1 == nullptr ) { break; }

        hCurHE = pN1->hNext;

        // Check if we've looped back to start.
        const mesh_half_edge_record_t *pNext =
            GenerationPool_Get( &pMesh->halfEdges, hCurHE );
        if ( pNext == nullptr ) { break; }

        if ( pNext->hEdge.nSlot == hStartEdge.nSlot &&
             pNext->hEdge.nGeneration == hStartEdge.nGeneration ) {
            break;
        }

        if ( ++safety > 256u ) { break; }
    } while ( true );

    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// BevelEdge (Gate 14)
// ---------------------------------------------------------------------------

static mesh_bevel_edge_result_t MeshOps_BevelEdgeInPlace(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge,
    f64 width,
    u32 cSegments ) noexcept
{
    mesh_bevel_edge_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( width <= 0.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( cSegments < 1u || cSegments > 64u ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    const mesh_edge_record_t *pEdge =
        GenerationPool_Get( &pMesh->edges, hEdge );
    if ( pEdge == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    // Get the two half-edges of this edge.
    const geometry_mesh_half_edge_handle_t hHE_A = pEdge->hHalfEdge;
    const mesh_half_edge_record_t *pHE_A =
        GenerationPool_Get( &pMesh->halfEdges, hHE_A );
    if ( pHE_A == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    const geometry_mesh_half_edge_handle_t hHE_B = pHE_A->hTwin;
    const mesh_half_edge_record_t *pHE_B =
        GenerationPool_Get( &pMesh->halfEdges, hHE_B );
    if ( pHE_B == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // Get faces on both sides of the edge.
    const mesh_loop_record_t *pLoopA =
        GenerationPool_Get( &pMesh->loops, pHE_A->hLoop );
    const mesh_loop_record_t *pLoopB =
        GenerationPool_Get( &pMesh->loops, pHE_B->hLoop );
    if ( pLoopA == nullptr || pLoopB == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    const mesh_face_record_t *pFaceA =
        GenerationPool_Get( &pMesh->faces, pLoopA->hFace );
    const mesh_face_record_t *pFaceB =
        GenerationPool_Get( &pMesh->faces, pLoopB->hFace );
    if ( pFaceA == nullptr || pFaceB == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // Get the two edge endpoints.
    const geometry_mesh_vertex_handle_t hV0 = pHE_A->hOrigin;
    const geometry_mesh_vertex_handle_t hV1 = pHE_B->hOrigin;

    const mesh_vertex_record_t *pV0 =
        GenerationPool_Get( &pMesh->vertices, hV0 );
    const mesh_vertex_record_t *pV1 =
        GenerationPool_Get( &pMesh->vertices, hV1 );
    if ( pV0 == nullptr || pV1 == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // Compute edge direction and normals for offset.
    const math::vec3d_t edgeDir = math::Vec3d_Subtract(
        pV1->position, pV0->position );
    const f64 edgeLenSq = math::Vec3d_LengthSquared( edgeDir );
    if ( edgeLenSq < 1.0e-24 ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }

    // For a single-segment bevel, we split the edge at two points near
    // each end, creating two new vertices offset from V0 and V1 toward
    // each other along the edge.
    //
    // For cSegments > 1, we create (cSegments + 1) vertices along the
    // bevel arc. For simplicity, we linearly interpolate between the
    // offset positions (chamfer). A true rounded bevel would require
    // arc interpolation, but chamfer is the standard first pass.

    const f64 edgeLen = std::sqrt( edgeLenSq );
    if ( width * 2.0 >= edgeLen ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }

    const math::vec3d_t edgeUnit = math::Vec3d_Scale(
        edgeDir, 1.0 / edgeLen );

    // Compute the offset direction: average of the two face normals,
    // projected perpendicular to the edge. This gives the direction to
    // push the bevel vertices away from the edge.
    math::vec3d_t avgNormal = math::Vec3d_Scale(
        math::Vec3d_Add( pFaceA->normal, pFaceB->normal ), 0.5 );

    // Project avgNormal perpendicular to edge.
    const f64 dotEdge = math::Vec3d_Dot( avgNormal, edgeUnit );
    avgNormal = math::Vec3d_Subtract(
        avgNormal, math::Vec3d_Scale( edgeUnit, dotEdge ) );
    const f64 avgNormalLenSq = math::Vec3d_LengthSquared( avgNormal );
    if ( avgNormalLenSq < 1.0e-24 ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }
    avgNormal = math::Vec3d_Scale(
        avgNormal, 1.0 / std::sqrt( avgNormalLenSq ) );

    // For single-segment bevel: create 4 new vertices forming a quad.
    // The bevel face replaces the edge, and the two adjacent faces are
    // modified to reference the new vertices.
    //
    // Bevel vertex positions:
    //   bv0 = V0 + width * edgeUnit  (along edge from V0)
    //   bv1 = V1 - width * edgeUnit  (along edge from V1)
    // These two points plus the original V0 and V1 form the bevel quad.

    // For multi-segment, subdivide the edge into (cSegments + 1) strips.
    // We create (cSegments - 1) intermediate vertices along each face's
    // edge direction.

    // Simplified approach: split the edge at t = width/edgeLen and
    // t = 1 - width/edgeLen, then dissolve the middle portion and
    // create the bevel face.

    const f64 t0 = width / edgeLen;
    // Split edge at t0 first.
    const auto split0 = MeshOps_SplitEdge( pMesh, hEdge, t0 );
    if ( split0.status != geometry_status_t::OK ) {
        result.status = split0.status;
        return result;
    }

    // Now split the second half (from new vertex to V1) at the adjusted
    // parametric position. The remaining edge goes from the split point
    // to V1, with length (1 - t0) * edgeLen. We want to cut at distance
    // width from V1, so t_second = 1 - width / ((1 - t0) * edgeLen).
    const f64 remainingLen = ( 1.0 - t0 ) * edgeLen;
    const f64 t_second = 1.0 - ( width / remainingLen );

    if ( t_second <= 0.0 || t_second >= 1.0 ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }

    const auto split1 = MeshOps_SplitEdge(
        pMesh, split0.hNewEdge, t_second );
    if ( split1.status != geometry_status_t::OK ) {
        result.status = split1.status;
        return result;
    }

    // Now we have: V0 -- splitVert0 -- splitVert1 -- V1
    // The middle edge (splitVert0 to splitVert1) is the one we want to
    // replace with a bevel face. We dissolve the edge and then the bevel
    // face is the merged face.

    // Find the edge between splitVert0 and splitVert1.
    // After split0: split0.hNewEdge connected split0.hNewVertex to V1.
    // After split1: split1 split that edge again, creating split1.hNewVertex.
    // The edge from split0.hNewVertex to split1.hNewVertex is... the
    // original split0.hNewEdge (now shortened).

    // Actually, after the second split on split0.hNewEdge:
    //   split0.hNewEdge now goes split0.hNewVertex → split1.hNewVertex
    //   split1.hNewEdge goes split1.hNewVertex → V1
    // So the middle edge handle is split0.hNewEdge — dissolve it.

    // DissolveEdge merges the two adjacent faces into one, which IS the
    // bevel face.
    const geometry_status_t dissolveStatus = MeshOps_DissolveEdge(
        pMesh, split0.hNewEdge );
    if ( dissolveStatus != geometry_status_t::OK ) {
        result.status = dissolveStatus;
        return result;
    }

    // The dissolved edge merged two faces. We need to find the surviving
    // face that contains both split vertices. Walk from split0.hNewVertex.
    const mesh_vertex_record_t *pSplitV0 =
        GenerationPool_Get( &pMesh->vertices, split0.hNewVertex );
    if ( pSplitV0 != nullptr &&
         GenerationHandle_IsValid( pSplitV0->hOutHalfEdge ) ) {
        const mesh_half_edge_record_t *pOutHE =
            GenerationPool_Get( &pMesh->halfEdges,
                                pSplitV0->hOutHalfEdge );
        if ( pOutHE != nullptr ) {
            const mesh_loop_record_t *pBevelLoop =
                GenerationPool_Get( &pMesh->loops, pOutHE->hLoop );
            if ( pBevelLoop != nullptr ) {
                result.hBevelFace = pBevelLoop->hFace;
            }
        }
    }

    result.status = geometry_status_t::OK;
    return result;
}

mesh_bevel_edge_result_t MeshOps_BevelEdge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge,
    f64 width,
    u32 cSegments ) noexcept
{
    mesh_bevel_edge_result_t result{};
    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( width ) || width <= 0.0 ||
         cSegments < 1u || cSegments > 64u ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( cSegments != 1u ) {
        result.status = geometry_status_t::UNSUPPORTED;
        return result;
    }
    if ( GenerationPool_Get( &pMesh->edges, hEdge ) == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    editable_mesh_t stagedMesh{};
    result.status = CloneEditableMeshExact( pMesh, &stagedMesh );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    result = MeshOps_BevelEdgeInPlace(
        &stagedMesh, hEdge, width, cSegments );
    if ( result.status == geometry_status_t::OK &&
         GenerationPool_Get(
             &stagedMesh.faces, result.hBevelFace ) == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
    }
    if ( result.status == geometry_status_t::OK ) {
        SwapEditableMeshStorage( pMesh, &stagedMesh );
    } else {
        result.hBevelFace = {};
    }
    EditableMesh_Shutdown( &stagedMesh );
    return result;
}

// ---------------------------------------------------------------------------
// BridgeEdges (Gate 14)
// ---------------------------------------------------------------------------

mesh_bridge_edges_result_t MeshOps_BridgeEdges(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdgeA,
    geometry_mesh_edge_handle_t hEdgeB ) noexcept
{
    mesh_bridge_edges_result_t result{};
    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( HandlesEqual( hEdgeA, hEdgeB ) ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    const mesh_edge_record_t *pEdgeA =
        GenerationPool_Get( &pMesh->edges, hEdgeA );
    const mesh_edge_record_t *pEdgeB =
        GenerationPool_Get( &pMesh->edges, hEdgeB );
    if ( pEdgeA == nullptr || pEdgeB == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }
    if ( GenerationPool_Get( &pMesh->halfEdges, pEdgeA->hHalfEdge ) == nullptr ||
         GenerationPool_Get( &pMesh->halfEdges, pEdgeB->hHalfEdge ) == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // EditableMesh currently models closed two-manifold shells only: every
    // half-edge must have a reciprocal twin. A bridge requires explicit open
    // boundary loops, so accepting a closed-mesh edge here would destroy its
    // neighbouring faces. Leave the mesh untouched until that representation
    // and its validation contract exist.
    result.status = geometry_status_t::UNSUPPORTED;
    return result;
}

// ---------------------------------------------------------------------------
// FillHole (Gate 14)
// ---------------------------------------------------------------------------

mesh_fill_hole_result_t MeshOps_FillHole(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hBoundaryEdge ) noexcept
{
    mesh_fill_hole_result_t result{};
    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    const mesh_edge_record_t *pEdge =
        GenerationPool_Get( &pMesh->edges, hBoundaryEdge );
    if ( pEdge == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }
    if ( GenerationPool_Get( &pMesh->halfEdges, pEdge->hHalfEdge ) == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // Open boundary half-edges are not a valid EditableMesh state yet, so a
    // hole cannot be represented without first defining boundary-loop and
    // shell invariants. Do not reinterpret a closed edge as a hole.
    result.status = geometry_status_t::UNSUPPORTED;
    return result;
}

// ---------------------------------------------------------------------------
// DetachFaces (Gate 14)
// ---------------------------------------------------------------------------

mesh_detach_faces_result_t MeshOps_DetachFaces(
    editable_mesh_t *pMesh,
    const geometry_mesh_face_handle_t *phFaces,
    usize cFaces ) noexcept
{
    mesh_detach_faces_result_t result{};
    if ( pMesh == nullptr || phFaces == nullptr || cFaces == 0u ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    constexpr usize kFaceInputMax = 256u;
    if ( cFaces > kFaceInputMax ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }

    for ( usize i = 0u; i < cFaces; ++i ) {
        if ( GenerationPool_Get( &pMesh->faces, phFaces[i] ) == nullptr ) {
            result.status = geometry_status_t::INVALID_HANDLE;
            return result;
        }
        for ( usize j = 0u; j < i; ++j ) {
            if ( HandlesEqual( phFaces[i], phFaces[j] ) ) {
                result.status = geometry_status_t::INVALID_ARGUMENT;
                return result;
            }
        }
    }

    // Separating a proper subset of a closed shell creates two open cut
    // surfaces. The current representation has neither boundary half-edges
    // nor automatic closure faces, so publishing that state would violate the
    // operation's two-manifold and atomicity contract.
    result.status = geometry_status_t::UNSUPPORTED;
    return result;
}

// ---------------------------------------------------------------------------
// Mirror (Gate 14)
// ---------------------------------------------------------------------------

geometry_status_t MeshOps_Mirror(
    editable_mesh_t *pMesh,
    math::planed_t mirrorPlane ) noexcept
{
    if ( pMesh == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    if ( !math::Planed_IsFinite( mirrorPlane ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    math::planed_t unitPlane{};
    if ( !math::Planed_TryNormalize(
             mirrorPlane, 1.0e-12, &unitPlane ) ) {
        return geometry_status_t::DEGENERATE;
    }

    struct origin_update_t {
        geometry_mesh_half_edge_handle_t hHalfEdge;
        geometry_mesh_vertex_handle_t hNewOrigin;
    };
    vector_t<origin_update_t> originUpdates{};
    const usize cHalfEdges = GenerationPool_Count( &pMesh->halfEdges );
    if ( !Vector_Init(
             &originUpdates, pMesh->pAllocator, cHalfEdges ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    bool bTopologyValid = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t hHalfEdge,
             const mesh_half_edge_record_t &halfEdge ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pOldNext =
                GenerationPool_Get( &pMesh->halfEdges, halfEdge.hNext );
            if ( pOldNext == nullptr ||
                 !Vector_PushBack(
                     &originUpdates,
                     origin_update_t{ hHalfEdge, pOldNext->hOrigin } ) ) {
                bTopologyValid = false;
                return false;
            }
            return true;
        } );
    if ( !bTopologyValid || originUpdates.nCount != cHalfEdges ) {
        return bTopologyValid
            ? geometry_status_t::ALLOCATION_FAILED
            : geometry_status_t::CORRUPT_STATE;
    }

    // Prove every reflected coordinate is finite before publishing any
    // mutation, so extreme but finite plane values fail atomically.
    bool bNumericResultValid = true;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vert ) noexcept -> bool_t {
            const f64 distance =
                math::Vec3d_Dot( unitPlane.normal, vert.position ) +
                unitPlane.d;
            const math::vec3d_t reflected = math::Vec3d_Subtract(
                vert.position,
                math::Vec3d_Scale(
                    unitPlane.normal, 2.0 * distance ) );
            if ( !math::Vec3d_IsFinite( reflected ) ) {
                bNumericResultValid = false;
                return false;
            }
            return true;
        } );
    if ( !bNumericResultValid ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    // Reflect all vertex positions across the plane.
    // Reflection formula: P' = P - 2 * (dot(N, P) + d) * N
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             mesh_vertex_record_t &vert ) noexcept -> bool_t {
            const f64 dist =
                math::Vec3d_Dot( unitPlane.normal, vert.position ) +
                unitPlane.d;
            vert.position = math::Vec3d_Subtract(
                vert.position,
                math::Vec3d_Scale( unitPlane.normal, 2.0 * dist ) );
            return true;
        } );

    // Reverse face winding by swapping next/prev pointers on all
    // half-edges. This maintains outward-facing normals after reflection.
    // Swap next/prev and publish the origins captured from the original
    // winding. Scratch is dynamically sized to the actual mesh; silently
    // truncating at a fixed half-edge count would leave large meshes corrupt.
    for ( usize i = 0u; i < originUpdates.nCount; ++i ) {
        mesh_half_edge_record_t *pHE =
            GenerationPool_Get(
                &pMesh->halfEdges,
                originUpdates.pData[i].hHalfEdge );
        if ( pHE != nullptr ) {
            const geometry_mesh_half_edge_handle_t tmp = pHE->hNext;
            pHE->hNext = pHE->hPrev;
            pHE->hPrev = tmp;
            pHE->hOrigin = originUpdates.pData[i].hNewOrigin;
        }
    }

    // Negate all face normals (reflection reverses them).
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hFace,
             mesh_face_record_t & ) noexcept -> bool_t {
            RecomputeFaceNormal( pMesh, hFace );
            return true;
        } );

    // Update vertex outgoing half-edge pointers.
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hV,
             mesh_vertex_record_t &vert ) noexcept -> bool_t {
            // Verify the outgoing pointer still originates at this vertex.
            const mesh_half_edge_record_t *pOut =
                GenerationPool_Get( &pMesh->halfEdges, vert.hOutHalfEdge );
            if ( pOut != nullptr &&
                 ( pOut->hOrigin.nSlot != hV.nSlot ||
                   pOut->hOrigin.nGeneration != hV.nGeneration ) ) {
                // Search for a valid outgoing half-edge.
                (void)GenerationPool_ForEach( &pMesh->halfEdges,
                    [&]( geometry_mesh_half_edge_handle_t hHE,
                         const mesh_half_edge_record_t &he )
                             noexcept -> bool_t {
                        if ( he.hOrigin.nSlot == hV.nSlot &&
                             he.hOrigin.nGeneration == hV.nGeneration ) {
                            vert.hOutHalfEdge = hHE;
                            return false;
                        }
                        return true;
                    } );
            }
            return true;
        } );

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Laplacian smoothing (Gate 17)
// ---------------------------------------------------------------------------

mesh_smooth_result_t MeshOps_LaplacianSmooth(
    editable_mesh_t *pMesh,
    f64 factor,
    u32 cIterations ) noexcept
{
    mesh_smooth_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( factor ) || cIterations == 0u ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Clamp factor to [0, 1].
    if ( factor < 0.0 ) { factor = 0.0; }
    if ( factor > 1.0 ) { factor = 1.0; }

    // Scratch indexed by vertex slot: pending positions for the current
    // pass, and a per-slot "ever moved" flag for the result count. Jacobi
    // needs the pending buffer — writing positions in place while other
    // vertices still read them (Gauss-Seidel) makes the result depend on
    // pool slot order.
    const usize cSlots = GenerationPool_Capacity( &pMesh->vertices );
    if ( cSlots == 0u ) {
        result.status = geometry_status_t::OK;
        return result;
    }
    const usize cPosBytes = cSlots * sizeof( math::vec3d_t );
    const usize cFlagBytes = cSlots * sizeof( u8 );
    auto *pPending = static_cast<math::vec3d_t *>(
        Allocator_Allocate( pMesh->pAllocator, cPosBytes,
                            alignof( math::vec3d_t ) ) );
    auto *pFlags = static_cast<u8 *>(
        Allocator_Allocate( pMesh->pAllocator, cFlagBytes, alignof( u8 ) ) );
    if ( pPending == nullptr || pFlags == nullptr ) {
        if ( pPending ) {
            Allocator_Free( pMesh->pAllocator, pPending, cPosBytes,
                            alignof( math::vec3d_t ) );
        }
        if ( pFlags ) {
            Allocator_Free( pMesh->pAllocator, pFlags, cFlagBytes,
                            alignof( u8 ) );
        }
        result.status = geometry_status_t::ALLOCATION_FAILED;
        return result;
    }
    // Flag bits: 1 = has a pending position this pass, 2 = moved at least
    // once over all passes.
    for ( usize i = 0u; i < cSlots; ++i ) { pFlags[i] = 0u; }

    for ( u32 iter = 0u; iter < cIterations; ++iter ) {

        // Pass 1: read-only. Compute every new position from the positions
        // at the start of the pass.
        (void)GenerationPool_ForEach( &pMesh->vertices,
            [&]( geometry_mesh_vertex_handle_t hV,
                 const mesh_vertex_record_t &vert ) noexcept -> bool_t {
                pFlags[hV.nSlot] &= static_cast<u8>( ~1u );
                if ( !GenerationHandle_IsValid( vert.hOutHalfEdge ) ) {
                    return true;
                }

                // Walk the one-ring: destination of each outgoing
                // half-edge, advancing twin → next.
                math::vec3d_t sumNeighbors = { 0.0, 0.0, 0.0 };
                u32 cNeighbors = 0u;
                bool bBoundary = false;
                const geometry_mesh_half_edge_handle_t hStart =
                    vert.hOutHalfEdge;
                geometry_mesh_half_edge_handle_t hCur = hStart;
                do {
                    const mesh_half_edge_record_t *pHE =
                        GenerationPool_Get( &pMesh->halfEdges, hCur );
                    const mesh_half_edge_record_t *pNext = pHE != nullptr
                        ? GenerationPool_Get( &pMesh->halfEdges, pHE->hNext )
                        : nullptr;
                    const mesh_vertex_record_t *pNeighbor = pNext != nullptr
                        ? GenerationPool_Get( &pMesh->vertices,
                                              pNext->hOrigin )
                        : nullptr;
                    if ( pNeighbor == nullptr ) { bBoundary = true; break; }

                    sumNeighbors = math::Vec3d_Add(
                        sumNeighbors, pNeighbor->position );
                    ++cNeighbors;

                    const mesh_half_edge_record_t *pTwin =
                        GenerationPool_Get( &pMesh->halfEdges, pHE->hTwin );
                    if ( pTwin == nullptr ) { bBoundary = true; break; }
                    hCur = pTwin->hNext;
                    // Valence above the builder's corner bound means the
                    // fan is not closing; treat as boundary, don't spin.
                    if ( cNeighbors > 256u ) { bBoundary = true; break; }
                } while ( hCur.nSlot != hStart.nSlot ||
                          hCur.nGeneration != hStart.nGeneration );

                // Boundary vertices stay fixed (the header promises it):
                // their one-ring is one-sided, so the average would pull
                // open borders inward and shrink holes.
                if ( bBoundary || cNeighbors < 2u ) { return true; }

                const math::vec3d_t avg = math::Vec3d_Scale(
                    sumNeighbors, 1.0 / static_cast<f64>( cNeighbors ) );
                pPending[hV.nSlot] = math::Vec3d_Add(
                    math::Vec3d_Scale( vert.position, 1.0 - factor ),
                    math::Vec3d_Scale( avg, factor ) );
                pFlags[hV.nSlot] |= 1u;
                return true;
            } );

        // Pass 2: publish.
        (void)GenerationPool_ForEach( &pMesh->vertices,
            [&]( geometry_mesh_vertex_handle_t hV,
                 mesh_vertex_record_t &vert ) noexcept -> bool_t {
                if ( ( pFlags[hV.nSlot] & 1u ) == 0u ) { return true; }
                const math::vec3d_t diff =
                    math::Vec3d_Subtract( pPending[hV.nSlot], vert.position );
                if ( math::Vec3d_LengthSquared( diff ) > 1.0e-20 ) {
                    vert.position = pPending[hV.nSlot];
                    pFlags[hV.nSlot] |= 2u;
                }
                return true;
            } );
    }

    for ( usize i = 0u; i < cSlots; ++i ) {
        if ( ( pFlags[i] & 2u ) != 0u ) { ++result.cVerticesMoved; }
    }
    Allocator_Free( pMesh->pAllocator, pPending, cPosBytes,
                    alignof( math::vec3d_t ) );
    Allocator_Free( pMesh->pAllocator, pFlags, cFlagBytes, alignof( u8 ) );

    // Recompute all face normals after smoothing.
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t & ) noexcept -> bool_t {
            RecomputeFaceNormal( pMesh, hFace );
            return true;
        } );

    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// Mesh decimation (Gate 17)
// ---------------------------------------------------------------------------

mesh_decimate_result_t MeshOps_Decimate(
    editable_mesh_t *pMesh,
    usize cTargetFaces,
    f64 fMinEdgeLength ) noexcept
{
    mesh_decimate_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( cTargetFaces < 4u ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !math::Scalar_IsFinite( fMinEdgeLength ) ||
         fMinEdgeLength <= 0.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    const f64 minLenSq = fMinEdgeLength * fMinEdgeLength;

    // Edges CollapseEdge refused (link condition / minimum-face guard).
    // They stay refused only until the next successful collapse, because
    // any collapse can change the neighbourhood that caused the refusal.
    // Bounded so a pathological mesh cannot make this loop unbounded:
    // once the list is full, decimation stops rather than rescanning.
    static constexpr u32 kMaxRefused = 256u;
    geometry_mesh_edge_handle_t refused[kMaxRefused];
    u32 cRefused = 0u;
    auto isRefused = [&]( geometry_mesh_edge_handle_t h ) noexcept {
        for ( u32 i = 0u; i < cRefused; ++i ) {
            if ( refused[i].nSlot == h.nSlot &&
                 refused[i].nGeneration == h.nGeneration ) {
                return true;
            }
        }
        return false;
    };

    // Iteratively find and collapse the shortest edge until we reach the
    // target face count or no collapsible edge is short enough.
    // Complexity: each step is a full O(E) scan plus CollapseEdge's O(H)
    // scans, so reducing by k faces costs O(k * H). Fine for editor-sized
    // meshes; a quadric-error priority queue is required before this is
    // used on large imports.
    for ( ;; ) {
        const usize cFaces = EditableMesh_FaceCount( pMesh );
        if ( cFaces <= cTargetFaces ) { break; }

        // Find the shortest edge that has not been refused.
        geometry_mesh_edge_handle_t hShortest =
            GEOMETRY_HANDLE_INVALID<geometry_mesh_edge_tag_t>;
        f64 shortestLenSq = minLenSq;

        (void)GenerationPool_ForEach( &pMesh->edges,
            [&]( geometry_mesh_edge_handle_t hEdge,
                 const mesh_edge_record_t &edge ) noexcept -> bool_t {
                if ( isRefused( hEdge ) ) { return true; }

                const mesh_half_edge_record_t *pHE =
                    GenerationPool_Get( &pMesh->halfEdges,
                                        edge.hHalfEdge );
                if ( pHE == nullptr ) { return true; }

                const mesh_half_edge_record_t *pTwin =
                    GenerationPool_Get( &pMesh->halfEdges, pHE->hTwin );
                if ( pTwin == nullptr ) { return true; }

                const mesh_vertex_record_t *pVA =
                    GenerationPool_Get( &pMesh->vertices, pHE->hOrigin );
                const mesh_vertex_record_t *pVB =
                    GenerationPool_Get( &pMesh->vertices,
                                        pTwin->hOrigin );
                if ( pVA == nullptr || pVB == nullptr ) { return true; }

                const math::vec3d_t diff = math::Vec3d_Subtract(
                    pVA->position, pVB->position );
                const f64 lenSq = math::Vec3d_LengthSquared( diff );

                if ( lenSq < shortestLenSq ) {
                    shortestLenSq = lenSq;
                    hShortest = hEdge;
                }
                return true;
            } );

        if ( !GenerationHandle_IsValid( hShortest ) ) {
            break;
        }

        // Collapse the shortest edge. Topological refusals are expected
        // (that is how the link condition protects the manifold); anything
        // else is a real failure and is reported.
        const mesh_collapse_edge_result_t collapseResult =
            MeshOps_CollapseEdge( pMesh, hShortest );
        if ( collapseResult.status == geometry_status_t::NON_MANIFOLD ||
             collapseResult.status == geometry_status_t::DEGENERATE ) {
            if ( cRefused >= kMaxRefused ) { break; }
            refused[cRefused++] = hShortest;
            continue;
        }
        if ( collapseResult.status != geometry_status_t::OK ) {
            result.status = collapseResult.status;
            return result;
        }

        cRefused = 0u;
        ++result.cEdgesCollapsed;
    }

    result.status = geometry_status_t::OK;
    return result;
}

// ---------------------------------------------------------------------------
// Face triangulation (Gate 17)
// ---------------------------------------------------------------------------

mesh_triangulate_result_t MeshOps_TriangulateFace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace ) noexcept
{
    mesh_triangulate_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    const mesh_face_record_t *pFace =
        GenerationPool_Get( &pMesh->faces, hFace );
    if ( pFace == nullptr ) {
        result.status = geometry_status_t::INVALID_HANDLE;
        return result;
    }

    const mesh_loop_record_t *pLoop =
        GenerationPool_Get( &pMesh->loops, pFace->hOuterLoop );
    if ( pLoop == nullptr ) {
        result.status = geometry_status_t::CORRUPT_STATE;
        return result;
    }

    // If already a triangle, nothing to do.
    if ( pLoop->cHalfEdges <= 3u ) {
        result.status = geometry_status_t::OK;
        return result;
    }

    // Collect corner handles and positions in loop order. 256 matches the
    // MeshBuilder per-face corner bound, so any face the builder accepts
    // can be triangulated.
    static constexpr u32 kMaxCorners = 256u;
    const u32 cFaceVerts = pLoop->cHalfEdges;
    if ( cFaceVerts > kMaxCorners ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }
    geometry_mesh_vertex_handle_t faceVerts[kMaxCorners];
    math::vec3d_t facePos[kMaxCorners];

    math::vec3d_t newell = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    geometry_mesh_half_edge_handle_t hCur = pLoop->hFirstHalfEdge;
    for ( u32 i = 0u; i < cFaceVerts; ++i ) {
        const mesh_half_edge_record_t *pHE =
            GenerationPool_Get( &pMesh->halfEdges, hCur );
        const mesh_vertex_record_t *pV = pHE != nullptr
            ? GenerationPool_Get( &pMesh->vertices, pHE->hOrigin )
            : nullptr;
        if ( pV == nullptr ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
        faceVerts[i] = pHE->hOrigin;
        facePos[i] = pV->position;
        hCur = pHE->hNext;
    }
    for ( u32 i = 0u; i < cFaceVerts; ++i ) {
        const math::vec3d_t a = facePos[i];
        const math::vec3d_t b = facePos[( i + 1u ) % cFaceVerts];
        newell.x += ( a.y - b.y ) * ( a.z + b.z );
        newell.y += ( a.z - b.z ) * ( a.x + b.x );
        newell.z += ( a.x - b.x ) * ( a.y + b.y );
    }

    // Project onto the plane that drops the dominant normal axis. The
    // Newell normal follows the loop's CCW winding, so choosing the
    // projection sign from it makes the loop CCW in 2D and a positive
    // cross product means "convex corner".
    const f64 ax = std::fabs( newell.x );
    const f64 ay = std::fabs( newell.y );
    const f64 az = std::fabs( newell.z );
    if ( ax + ay + az <= 0.0 ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }
    f64 u[kMaxCorners];
    f64 v[kMaxCorners];
    f64 minU = 0.0, maxU = 0.0, minV = 0.0, maxV = 0.0;
    for ( u32 i = 0u; i < cFaceVerts; ++i ) {
        const math::vec3d_t p = facePos[i];
        if ( az >= ax && az >= ay ) {
            u[i] = p.x; v[i] = newell.z > 0.0 ? p.y : -p.y;
        } else if ( ay >= ax ) {
            u[i] = p.z; v[i] = newell.y > 0.0 ? p.x : -p.x;
        } else {
            u[i] = p.y; v[i] = newell.x > 0.0 ? p.z : -p.z;
        }
        if ( i == 0u || u[i] < minU ) { minU = u[i]; }
        if ( i == 0u || u[i] > maxU ) { maxU = u[i]; }
        if ( i == 0u || v[i] < minV ) { minV = v[i]; }
        if ( i == 0u || v[i] > maxV ) { maxV = v[i]; }
    }
    // Area tolerance scales with the face so the convexity test behaves the
    // same for a 1-unit face and a 10k-unit face. This is a filter, not an
    // exact predicate; exact orient2d belongs to the Kernel roadmap.
    const f64 extent = std::fmax( maxU - minU, maxV - minV );
    const f64 areaEps = 1.0e-12 * extent * extent;

    auto cross2 = [&]( u32 a, u32 b, u32 c ) noexcept {
        return ( u[b] - u[a] ) * ( v[c] - v[a] ) -
               ( v[b] - v[a] ) * ( u[c] - u[a] );
    };

    // Ear clipping, planned entirely in 2D before any mutation so a face
    // that cannot be triangulated (self-overlapping, all-collinear) is
    // rejected with the mesh untouched. Unlike a fan from corner 0 this
    // never emits a zero-area triangle across collinear corners (produced
    // by SplitEdge) and never emits overlapping triangles on concave faces.
    u32 active[kMaxCorners];
    u32 cActive = cFaceVerts;
    for ( u32 i = 0u; i < cFaceVerts; ++i ) { active[i] = i; }

    struct diagonal_t { u32 iA; u32 iB; };
    diagonal_t diagonals[kMaxCorners];
    u32 cDiagonals = 0u;

    while ( cActive > 3u ) {
        bool bClipped = false;
        for ( u32 k = 0u; k < cActive; ++k ) {
            const u32 iPrev = active[( k + cActive - 1u ) % cActive];
            const u32 iTip = active[k];
            const u32 iNext = active[( k + 1u ) % cActive];

            // Reflex or collinear tips are never ears.
            if ( cross2( iPrev, iTip, iNext ) <= areaEps ) { continue; }

            // No other remaining corner may lie inside or on the candidate
            // triangle; a corner on the diagonal would make it cross the
            // boundary.
            bool bBlocked = false;
            for ( u32 m = 0u; m < cActive && !bBlocked; ++m ) {
                const u32 iP = active[m];
                if ( iP == iPrev || iP == iTip || iP == iNext ) { continue; }
                if ( cross2( iPrev, iTip, iP ) >= -areaEps &&
                     cross2( iTip, iNext, iP ) >= -areaEps &&
                     cross2( iNext, iPrev, iP ) >= -areaEps ) {
                    bBlocked = true;
                }
            }
            if ( bBlocked ) { continue; }

            diagonals[cDiagonals++] = { iPrev, iNext };
            for ( u32 m = k; m + 1u < cActive; ++m ) {
                active[m] = active[m + 1u];
            }
            --cActive;
            bClipped = true;
            break;
        }
        if ( !bClipped ) {
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }
    }
    if ( cross2( active[0], active[1], active[2] ) <= areaEps ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }

    // Apply the planned diagonals. SplitFace(face, prev, next) keeps the
    // remainder polygon (without the tip) on the original face handle and
    // puts the clipped ear on a new face, so hFace stays the remainder.
    for ( u32 i = 0u; i < cDiagonals; ++i ) {
        const mesh_split_face_result_t splitResult = MeshOps_SplitFace(
            pMesh, hFace, faceVerts[diagonals[i].iA],
            faceVerts[diagonals[i].iB] );
        if ( splitResult.status != geometry_status_t::OK ) {
            // Only reachable on pool exhaustion: the plan was validated
            // above. Report it instead of claiming success; the splits
            // already applied are valid topology but the face is only
            // partially triangulated.
            result.status = splitResult.status;
            return result;
        }
        ++result.cFacesCreated;
    }

    result.status = geometry_status_t::OK;
    return result;
}

mesh_triangulate_result_t MeshOps_TriangulateFaces(
    editable_mesh_t *pMesh ) noexcept
{
    mesh_triangulate_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }

    // Snapshot face handles first: SplitFace inserts faces, and the pool
    // forbids structural mutation during ForEach. The list is heap-sized
    // to the live face count; a fixed stack array here used to drop every
    // face past 4096 silently while still reporting OK.
    const usize cFaces = EditableMesh_FaceCount( pMesh );
    if ( cFaces == 0u ) {
        result.status = geometry_status_t::OK;
        return result;
    }
    const usize cBytes = cFaces * sizeof( geometry_mesh_face_handle_t );
    auto *pFaceHandles = static_cast<geometry_mesh_face_handle_t *>(
        Allocator_Allocate( pMesh->pAllocator, cBytes,
                            alignof( geometry_mesh_face_handle_t ) ) );
    if ( pFaceHandles == nullptr ) {
        result.status = geometry_status_t::ALLOCATION_FAILED;
        return result;
    }

    usize cCollected = 0u;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t & ) noexcept -> bool_t {
            pFaceHandles[cCollected++] = hFace;
            return cCollected < cFaces;
        } );

    result.status = geometry_status_t::OK;
    for ( usize i = 0u; i < cCollected; ++i ) {
        const mesh_triangulate_result_t triResult =
            MeshOps_TriangulateFace( pMesh, pFaceHandles[i] );
        if ( triResult.status != geometry_status_t::OK ) {
            // Faces before this one are triangulated and valid; stop and
            // report rather than masking the failure as success.
            result.status = triResult.status;
            break;
        }
        result.cFacesCreated += triResult.cFacesCreated;
    }

    Allocator_Free( pMesh->pAllocator, pFaceHandles, cBytes,
                    alignof( geometry_mesh_face_handle_t ) );
    return result;
}

// ---------------------------------------------------------------------------
// Vertex grid snap (Gate 17)
// ---------------------------------------------------------------------------

mesh_snap_result_t MeshOps_SnapToGrid(
    editable_mesh_t *pMesh,
    f64 gridSpacing ) noexcept
{
    mesh_snap_result_t result{};

    if ( pMesh == nullptr ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( !std::isfinite( gridSpacing ) || gridSpacing <= 0.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    bool bNumericResultValid = true;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vert ) noexcept -> bool_t {
            const math::vec3d_t snapped = math::Vec3d_Make(
                std::round( vert.position.x / gridSpacing ) * gridSpacing,
                std::round( vert.position.y / gridSpacing ) * gridSpacing,
                std::round( vert.position.z / gridSpacing ) * gridSpacing );
            if ( !math::Vec3d_IsFinite( snapped ) ) {
                bNumericResultValid = false;
                return false;
            }
            return true;
        } );
    if ( !bNumericResultValid ) {
        result.status = geometry_status_t::NUMERIC_FAILURE;
        return result;
    }

    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             mesh_vertex_record_t &vert ) noexcept -> bool_t {
            const f64 snappedX = std::round(
                vert.position.x / gridSpacing ) * gridSpacing;
            const f64 snappedY = std::round(
                vert.position.y / gridSpacing ) * gridSpacing;
            const f64 snappedZ = std::round(
                vert.position.z / gridSpacing ) * gridSpacing;

            const math::vec3d_t snapped =
                math::Vec3d_Make( snappedX, snappedY, snappedZ );
            if ( !math::Vec3d_EqualsExact( snapped, vert.position ) ) {
                vert.position = snapped;
                ++result.cVerticesMoved;
            }
            return true;
        } );

    // Recompute all face normals after snapping.
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t & ) noexcept -> bool_t {
            RecomputeFaceNormal( pMesh, hFace );
            return true;
        } );

    result.status = geometry_status_t::OK;
    return result;
}

} // namespace cypher::editor::geometry
