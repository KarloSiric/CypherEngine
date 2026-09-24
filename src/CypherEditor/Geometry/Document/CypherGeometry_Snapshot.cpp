//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Snapshot.cpp
//  Purpose: Implements immutable deep-frozen geometry document snapshots.
//  Details: The snapshot deep-copies every brush from the document, including
//           all side storage. This makes the snapshot completely independent
//           of the mutable document — edits, undo, and even document shutdown
//           leave an existing snapshot unchanged. At typical editor brush
//           counts (hundreds) the copy cost is negligible.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Snapshot.h"

#include "CypherCommon_Sort.h"

#include <new>

namespace cypher::editor::geometry
{

namespace
{

geometry_status_t AllocateAndCopyBrush(
    const brush_solid_t *pSrc,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits,
    brush_solid_t **ppBrushOut ) noexcept
{
    *ppBrushOut = nullptr;
    void *pMemory = common::Allocator_AllocateZeroed(
        pAllocator, sizeof( brush_solid_t ), alignof( brush_solid_t ) );
    if ( pMemory == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    auto *pBrush = new ( pMemory ) brush_solid_t{};

    const geometry_status_t copyStatus =
        BrushSolid_DeepCopy( pBrush, pSrc, pAllocator, limits );
    if ( copyStatus != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pBrush );
        pBrush->~brush_solid_t();
        common::Allocator_Free(
            pAllocator, pBrush,
            sizeof( brush_solid_t ), alignof( brush_solid_t ) );
        return copyStatus;
    }
    *ppBrushOut = pBrush;
    return geometry_status_t::OK;
}

void FreeMeshCopy(
    const common::allocator_t *pAllocator,
    mesh_source_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) {
        return;
    }
    MeshSource_Shutdown( pMesh );
    pMesh->~mesh_source_t();
    common::Allocator_Free(
        pAllocator, pMesh,
        sizeof( mesh_source_t ), alignof( mesh_source_t ) );
}

geometry_status_t AllocateAndCopyMesh(
    const mesh_source_t *pSrc,
    const common::allocator_t *pAllocator,
    mesh_source_t **ppMeshOut ) noexcept
{
    *ppMeshOut = nullptr;
    void *pMemory = common::Allocator_AllocateZeroed(
        pAllocator, sizeof( mesh_source_t ), alignof( mesh_source_t ) );
    if ( pMemory == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    auto *pMesh = new ( pMemory ) mesh_source_t{};
    const geometry_status_t copyStatus =
        MeshSource_TryClone( pSrc, pAllocator, pMesh );
    if ( copyStatus != geometry_status_t::OK ) {
        FreeMeshCopy( pAllocator, pMesh );
        return copyStatus;
    }
    *ppMeshOut = pMesh;
    return geometry_status_t::OK;
}

void FreeMeshCopies(
    const common::allocator_t *pAllocator,
    common::vector_t<mesh_source_t *> *pMeshes ) noexcept
{
    for ( common::usize i = 0u; i < pMeshes->nCount; ++i ) {
        FreeMeshCopy( pAllocator, pMeshes->pData[i] );
    }
    common::Vector_Shutdown( pMeshes );
}

void FreeBrush(
    const common::allocator_t *pAllocator,
    brush_solid_t *pBrush ) noexcept
{
    if ( pBrush == nullptr ) {
        return;
    }
    BrushSolid_Shutdown( pBrush );
    pBrush->~brush_solid_t();
    common::Allocator_Free(
        pAllocator, pBrush,
        sizeof( brush_solid_t ), alignof( brush_solid_t ) );
}

bool IsCanonicalSnapshotDestination(
    const geometry_snapshot_t &snapshot ) noexcept
{
    const geometry_policy_t defaultPolicy{};
    return snapshot.brushes.pData == nullptr &&
           snapshot.brushes.nCount == 0u &&
           snapshot.brushes.nCapacity == 0u &&
           snapshot.brushes.pAllocator == nullptr &&
           snapshot.meshes.pData == nullptr &&
           snapshot.meshes.nCount == 0u &&
           snapshot.meshes.pAllocator == nullptr &&
           snapshot.policy.numerical.fCoordinateMagnitudeLimit ==
               defaultPolicy.numerical.fCoordinateMagnitudeLimit &&
           snapshot.policy.numerical.fAbsoluteDistanceTolerance ==
               defaultPolicy.numerical.fAbsoluteDistanceTolerance &&
           snapshot.policy.numerical.fRelativeDistanceTolerance ==
               defaultPolicy.numerical.fRelativeDistanceTolerance &&
           snapshot.policy.numerical.fMinimumEdgeLength ==
               defaultPolicy.numerical.fMinimumEdgeLength &&
           snapshot.policy.numerical.fMinimumFaceArea ==
               defaultPolicy.numerical.fMinimumFaceArea &&
           snapshot.policy.numerical.fAngularToleranceRadians ==
               defaultPolicy.numerical.fAngularToleranceRadians &&
           snapshot.policy.numerical.fPlanarityTolerance ==
               defaultPolicy.numerical.fPlanarityTolerance &&
           snapshot.policy.numerical.fCoplanarDistanceTolerance ==
               defaultPolicy.numerical.fCoplanarDistanceTolerance &&
           snapshot.policy.numerical.fUnitNormalTolerance ==
               defaultPolicy.numerical.fUnitNormalTolerance &&
           snapshot.policy.numerical.fSnapDistance ==
               defaultPolicy.numerical.fSnapDistance &&
           snapshot.policy.numerical.fWeldDistance ==
               defaultPolicy.numerical.fWeldDistance &&
           snapshot.policy.numerical.fCanonicalQuantization ==
               defaultPolicy.numerical.fCanonicalQuantization &&
           snapshot.policy.limits.cBrushesMax ==
               defaultPolicy.limits.cBrushesMax &&
           snapshot.policy.limits.cBrushSidesMax ==
               defaultPolicy.limits.cBrushSidesMax &&
           snapshot.policy.limits.cBrushSidesPerBrushMax ==
               defaultPolicy.limits.cBrushSidesPerBrushMax &&
           snapshot.policy.limits.cVerticesMax ==
               defaultPolicy.limits.cVerticesMax &&
           snapshot.policy.limits.cHalfEdgesMax ==
               defaultPolicy.limits.cHalfEdgesMax &&
           snapshot.policy.limits.cEdgesMax ==
               defaultPolicy.limits.cEdgesMax &&
           snapshot.policy.limits.cLoopsMax ==
               defaultPolicy.limits.cLoopsMax &&
           snapshot.policy.limits.cFacesMax ==
               defaultPolicy.limits.cFacesMax &&
           snapshot.policy.limits.cShellsMax ==
               defaultPolicy.limits.cShellsMax &&
           snapshot.policy.limits.cIntersectionEventsMax ==
               defaultPolicy.limits.cIntersectionEventsMax &&
           snapshot.policy.limits.cJournalRecordsMax ==
               defaultPolicy.limits.cJournalRecordsMax &&
           snapshot.policy.limits.cDiagnosticsMax ==
               defaultPolicy.limits.cDiagnosticsMax &&
           snapshot.policy.limits.cTraversalDepthMax ==
               defaultPolicy.limits.cTraversalDepthMax &&
           snapshot.policy.limits.cbScratchMax ==
               defaultPolicy.limits.cbScratchMax &&
           snapshot.revision == GEOMETRY_REVISION_INITIAL &&
           snapshot.pAllocator == nullptr;
}

geometry_status_t ValidateDocumentForSnapshot(
    const geometry_document_t &document ) noexcept
{
    const bool bPolicyValid = GeometryPolicy_IsValid( document.policy );
    const common::u64 cSourceEntriesMax = bPolicyValid
        ? GeometryDocument_SourceIdCapacity( document.policy )
        : 0u;
    if ( !common::Allocator_IsValid( document.pAllocator ) ||
         !common::Vector_IsValid( &document.brushes ) ||
         document.brushes.pAllocator != document.pAllocator ||
         !bPolicyValid ||
         !GeometrySourceIdRegistry_ValidateDeep( &document.sourceIds ) ||
         document.sourceIds.pAllocator != document.pAllocator ||
         document.sourceIds.bLoadRegistrationOpen ||
         cSourceEntriesMax > common::CY_USIZE_MAX ||
         document.sourceIds.cEntriesMax !=
             static_cast<common::usize>( cSourceEntriesMax ) ||
         static_cast<common::u64>( document.brushes.nCount ) >
             document.policy.limits.cBrushesMax ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    common::vector_t<geometry_source_id_t> liveIds{};
    const common::usize cExpectedLiveIds =
        GeometrySourceIdRegistry_Count( &document.sourceIds );
    if ( !common::Vector_Init(
             &liveIds, document.pAllocator, cExpectedLiveIds ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    common::usize cLiveIds = 0u;
    common::usize cSidesTotal = 0u;
    for ( common::usize iBrush = 0u;
          iBrush < document.brushes.nCount;
          ++iBrush ) {
        const brush_solid_t *pBrush = document.brushes.pData[iBrush];
        if ( pBrush == nullptr ||
             !GeometrySourceId_IsValid( pBrush->sourceId ) ||
             !common::Vector_IsValid( &pBrush->sides ) ||
             pBrush->sides.pAllocator != document.pAllocator ||
             static_cast<common::u64>( pBrush->sides.nCount ) >
                 document.policy.limits.cBrushSidesPerBrushMax ||
             !GeometrySourceIdRegistry_Contains(
                 &document.sourceIds, pBrush->sourceId ) ||
             pBrush->sides.nCount == common::CY_USIZE_MAX ||
             cLiveIds >
                 common::CY_USIZE_MAX - 1u - pBrush->sides.nCount ||
             cSidesTotal >
                 common::CY_USIZE_MAX - pBrush->sides.nCount ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        cLiveIds += pBrush->sides.nCount + 1u;
        cSidesTotal += pBrush->sides.nCount;
        if ( !common::Vector_PushBack( &liveIds, pBrush->sourceId ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }

        for ( common::usize iSide = 0u;
              iSide < pBrush->sides.nCount;
              ++iSide ) {
            const brush_solid_side_t &side = pBrush->sides.pData[iSide];
            if ( !GeometrySourceId_IsValid( side.sourceId ) ||
                 !GeometrySourceIdRegistry_Contains(
                     &document.sourceIds, side.sourceId ) ||
                 !math::Planed_IsFinite( side.plane ) ||
                 !math::Planed_IsNormalized(
                     side.plane,
                     document.policy.numerical.fUnitNormalTolerance ) ) {
                return geometry_status_t::CORRUPT_STATE;
            }
            if ( !common::Vector_PushBack( &liveIds, side.sourceId ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }

    // Mesh identities are live document IDs as well; each must be
    // registered, and together with brush IDs they must account for the
    // registry's whole live set.
    if ( !common::Vector_IsValid( &document.meshes ) ||
         ( document.meshes.nCount > 0u &&
           document.meshes.pAllocator != document.pAllocator ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    for ( common::usize iMesh = 0u;
          iMesh < document.meshes.nCount;
          ++iMesh ) {
        const mesh_source_t *pMesh = document.meshes.pData[iMesh];
        const common::usize cBefore = liveIds.nCount;
        if ( pMesh == nullptr ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        const geometry_status_t collectStatus =
            MeshSource_TryCollectSourceIds( pMesh, &liveIds );
        if ( collectStatus != geometry_status_t::OK ) {
            return collectStatus == geometry_status_t::ALLOCATION_FAILED
                ? geometry_status_t::ALLOCATION_FAILED
                : geometry_status_t::CORRUPT_STATE;
        }
        for ( common::usize k = cBefore; k < liveIds.nCount; ++k ) {
            if ( !GeometrySourceIdRegistry_Contains(
                     &document.sourceIds, liveIds.pData[k] ) ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
        cLiveIds += liveIds.nCount - cBefore;
    }

    if ( cSidesTotal > document.policy.limits.cBrushSidesMax ||
         cExpectedLiveIds != cLiveIds ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    struct source_id_less_t {
        bool operator()(
            const geometry_source_id_t &left,
            const geometry_source_id_t &right ) const noexcept
        {
            return left.value < right.value;
        }
    };
    common::Sort_Unstable(
        common::Vector_Span( &liveIds ), source_id_less_t{} );
    for ( common::usize i = 1u; i < liveIds.nCount; ++i ) {
        if ( liveIds.pData[i - 1u].value == liveIds.pData[i].value ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }
    return geometry_status_t::OK;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t GeometrySnapshot_TakeFromDocument(
    geometry_snapshot_t *pSnapshot,
    const geometry_document_t *pDocument ) noexcept
{
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !IsCanonicalSnapshotDestination( *pSnapshot ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    const geometry_status_t validationStatus =
        ValidateDocumentForSnapshot( *pDocument );
    if ( validationStatus != geometry_status_t::OK ) {
        return validationStatus;
    }

    const common::usize cBrushes = GeometryDocument_BrushCount( pDocument );
    const common::allocator_t *pAllocator = pDocument->pAllocator;

    geometry_snapshot_t pending{};
    if ( !common::Vector_Init(
             &pending.brushes, pAllocator, cBrushes ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize i = 0u; i < cBrushes; ++i ) {
        const brush_solid_t *pSrcBrush = pDocument->brushes.pData[i];

        brush_solid_t *pCopy = nullptr;
        const geometry_status_t copyStatus = AllocateAndCopyBrush(
            pSrcBrush,
            pAllocator,
            pDocument->policy.limits,
            &pCopy );
        if ( copyStatus != geometry_status_t::OK ) {
            // Roll back: free all brushes copied so far.
            for ( common::usize j = 0u; j < pending.brushes.nCount; ++j ) {
                FreeBrush( pAllocator, pending.brushes.pData[j] );
            }
            common::Vector_Shutdown( &pending.brushes );
            return copyStatus;
        }

        if ( !common::Vector_PushBack( &pending.brushes, pCopy ) ) {
            FreeBrush( pAllocator, pCopy );
            for ( common::usize j = 0u; j < pending.brushes.nCount; ++j ) {
                FreeBrush( pAllocator, pending.brushes.pData[j] );
            }
            common::Vector_Shutdown( &pending.brushes );
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }

    // Mesh copies, all-or-nothing together with the brushes above.
    const common::usize cMeshes = pDocument->meshes.nCount;
    if ( !common::Vector_Init( &pending.meshes, pAllocator, cMeshes ) ) {
        for ( common::usize j = 0u; j < pending.brushes.nCount; ++j ) {
            FreeBrush( pAllocator, pending.brushes.pData[j] );
        }
        common::Vector_Shutdown( &pending.brushes );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < cMeshes; ++i ) {
        mesh_source_t *pCopy = nullptr;
        geometry_status_t copyStatus = AllocateAndCopyMesh(
            pDocument->meshes.pData[i], pAllocator, &pCopy );
        if ( copyStatus == geometry_status_t::OK &&
             !common::Vector_PushBack( &pending.meshes, pCopy ) ) {
            FreeMeshCopy( pAllocator, pCopy );
            copyStatus = geometry_status_t::ALLOCATION_FAILED;
        }
        if ( copyStatus != geometry_status_t::OK ) {
            FreeMeshCopies( pAllocator, &pending.meshes );
            for ( common::usize j = 0u; j < pending.brushes.nCount; ++j ) {
                FreeBrush( pAllocator, pending.brushes.pData[j] );
            }
            common::Vector_Shutdown( &pending.brushes );
            return copyStatus;
        }
    }

    common::Vector_Move( &pSnapshot->brushes, &pending.brushes );
    common::Vector_Move( &pSnapshot->meshes, &pending.meshes );
    pSnapshot->policy = pDocument->policy;
    pSnapshot->revision = pDocument->revision;
    pSnapshot->pAllocator = pAllocator;

    return geometry_status_t::OK;
}

void GeometrySnapshot_Shutdown(
    geometry_snapshot_t *pSnapshot ) noexcept
{
    if ( pSnapshot == nullptr || pSnapshot->pAllocator == nullptr ) {
        return;
    }

    for ( common::usize i = 0u; i < pSnapshot->brushes.nCount; ++i ) {
        FreeBrush( pSnapshot->pAllocator, pSnapshot->brushes.pData[i] );
    }
    common::Vector_Shutdown( &pSnapshot->brushes );
    FreeMeshCopies( pSnapshot->pAllocator, &pSnapshot->meshes );

    pSnapshot->policy = {};
    pSnapshot->revision = GEOMETRY_REVISION_INITIAL;
    pSnapshot->pAllocator = nullptr;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool GeometrySnapshot_IsInitialized(
    const geometry_snapshot_t *pSnapshot ) noexcept
{
    return pSnapshot != nullptr &&
           common::Allocator_IsValid( pSnapshot->pAllocator ) &&
           common::Vector_IsValid( &pSnapshot->brushes ) &&
           pSnapshot->brushes.pAllocator == pSnapshot->pAllocator &&
           GeometryPolicy_IsValid( pSnapshot->policy );
}

geometry_revision_t GeometrySnapshot_GetRevision(
    const geometry_snapshot_t *pSnapshot ) noexcept
{
    if ( !GeometrySnapshot_IsInitialized( pSnapshot ) ) {
        return GEOMETRY_REVISION_INITIAL;
    }
    return pSnapshot->revision;
}

const geometry_policy_t *GeometrySnapshot_GetPolicy(
    const geometry_snapshot_t *pSnapshot ) noexcept
{
    return GeometrySnapshot_IsInitialized( pSnapshot )
        ? &pSnapshot->policy
        : nullptr;
}

common::usize GeometrySnapshot_BrushCount(
    const geometry_snapshot_t *pSnapshot ) noexcept
{
    if ( !GeometrySnapshot_IsInitialized( pSnapshot ) ) {
        return 0u;
    }
    return common::Vector_Count( &pSnapshot->brushes );
}

const brush_solid_t *GeometrySnapshot_FindBrush(
    const geometry_snapshot_t *pSnapshot,
    geometry_source_id_t brushId ) noexcept
{
    if ( !GeometrySnapshot_IsInitialized( pSnapshot ) ||
         !GeometrySourceId_IsValid( brushId ) ) {
        return nullptr;
    }

    for ( common::usize i = 0u; i < pSnapshot->brushes.nCount; ++i ) {
        if ( pSnapshot->brushes.pData[i] != nullptr &&
             pSnapshot->brushes.pData[i]->sourceId.value == brushId.value ) {
            return pSnapshot->brushes.pData[i];
        }
    }
    return nullptr;
}

common::usize GeometrySnapshot_MeshCount(
    const geometry_snapshot_t *pSnapshot ) noexcept
{
    return GeometrySnapshot_IsInitialized( pSnapshot )
        ? pSnapshot->meshes.nCount
        : 0u;
}

const mesh_source_t *GeometrySnapshot_MeshAt(
    const geometry_snapshot_t *pSnapshot,
    common::usize iMesh ) noexcept
{
    if ( !GeometrySnapshot_IsInitialized( pSnapshot ) ||
         iMesh >= pSnapshot->meshes.nCount ) {
        return nullptr;
    }
    return pSnapshot->meshes.pData[iMesh];
}

const mesh_source_t *GeometrySnapshot_FindMesh(
    const geometry_snapshot_t *pSnapshot,
    geometry_source_id_t meshId ) noexcept
{
    if ( !GeometrySnapshot_IsInitialized( pSnapshot ) ||
         !GeometrySourceId_IsValid( meshId ) ) {
        return nullptr;
    }
    for ( common::usize i = 0u; i < pSnapshot->meshes.nCount; ++i ) {
        const mesh_source_t *pMesh = pSnapshot->meshes.pData[i];
        if ( pMesh != nullptr && pMesh->sourceId.value == meshId.value ) {
            return pMesh;
        }
    }
    return nullptr;
}

} // namespace cypher::editor::geometry
