//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Transaction.cpp
//  Purpose: Implements the begin/preview/commit/cancel transaction protocol.
//  Details: The transaction deep-copies the target brush at begin to serve as
//           the baseline. Preview mutations are applied directly to the
//           document's live brush so rendering sees the change immediately.
//           On commit, the net delta is computed by comparing baseline to
//           current state. On cancel, the baseline is restored verbatim.
//
//           Gate 3 transactions target a single brush's side planes. More
//           complex multi-brush and structural mutations will extend the
//           delta vocabulary in later gates.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Transaction.h"
#include "CypherGeometry_ReplacementIdentity.h"

namespace cypher::editor::geometry
{

namespace
{

// Compares two planed_t values component-wise. Exact equality — no
// tolerance — because the baseline is a verbatim copy and we need to
// detect whether ANY change occurred.
bool PlanedEqual(
    const math::planed_t &a,
    const math::planed_t &b ) noexcept
{
    return a.normal.x == b.normal.x &&
           a.normal.y == b.normal.y &&
           a.normal.z == b.normal.z &&
           a.d == b.d;
}

bool IsCanonicalEmpty(
    const common::vector_t<brush_solid_side_t> &sides ) noexcept
{
    return sides.pData == nullptr &&
           sides.nCount == 0u &&
           sides.nCapacity == 0u &&
           sides.pAllocator == nullptr;
}

bool IsCanonicalEmpty(
    const common::vector_t<geometry_source_id_t> &ids ) noexcept
{
    return ids.pData == nullptr &&
           ids.nCount == 0u &&
           ids.nCapacity == 0u &&
           ids.pAllocator == nullptr;
}

bool CommitOutputsAreDefault(
    const geometry_delta_t &delta,
    const geometry_changeset_t &changeset ) noexcept
{
    return delta.kind == geometry_delta_kind_t::INVALID &&
           IsCanonicalEmpty( delta.brushData.sides ) &&
           IsCanonicalEmpty( delta.newBrushData.sides ) &&
           IsCanonicalEmpty( changeset.affectedBrushIds );
}

// Restores the complete authored brush by transferring ownership of the
// baseline storage into the document. This is allocation-free, so rollback
// still succeeds when the operation that triggered it exhausted the allocator.
// The baseline is consumed and left in canonical default state.
geometry_status_t RestoreBrushFromBaseline(
    geometry_document_t *pDocument,
    brush_solid_t *pBaseline,
    geometry_source_id_t targetBrushId ) noexcept
{
    if ( pBaseline == nullptr ||
         pBaseline->sides.pAllocator == nullptr ||
         pBaseline->sourceId.value != targetBrushId.value ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    brush_solid_t *pLiveBrush =
        GeometryDocument_FindBrushMutable( pDocument, targetBrushId );
    if ( pLiveBrush == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    BrushSolid_Shutdown( pLiveBrush );
    pLiveBrush->sourceId = pBaseline->sourceId;
    common::Vector_Move( &pLiveBrush->sides, &pBaseline->sides );
    pBaseline->sourceId = GEOMETRY_SOURCE_ID_INVALID;

    return geometry_status_t::OK;
}

// Cleans up the transaction's internal state.
void DeactivateTransaction(
    geometry_transaction_t *pTransaction ) noexcept
{
    BrushSolid_Shutdown( &pTransaction->baselineBrush );
    pTransaction->baselineBrush.sourceId = GEOMETRY_SOURCE_ID_INVALID;
    pTransaction->bActive = false;
    pTransaction->pDocument = nullptr;
    pTransaction->targetBrushId = {};
    pTransaction->baselineRevision = GEOMETRY_REVISION_INITIAL;
}

geometry_status_t RollbackAndDeactivate(
    geometry_transaction_t *pTransaction,
    geometry_status_t operationStatus ) noexcept
{
    const geometry_status_t restoreStatus = RestoreBrushFromBaseline(
        pTransaction->pDocument,
        &pTransaction->baselineBrush,
        pTransaction->targetBrushId );

    DeactivateTransaction( pTransaction );
    return restoreStatus == geometry_status_t::OK
        ? operationStatus
        : restoreStatus;
}

void MoveDelta(
    geometry_delta_t *pDestination,
    geometry_delta_t *pSource ) noexcept
{
    pDestination->kind = pSource->kind;
    pDestination->brushId = pSource->brushId;
    pDestination->sideIndex = pSource->sideIndex;
    pDestination->oldPlane = pSource->oldPlane;
    pDestination->newPlane = pSource->newPlane;

    if ( pSource->kind == geometry_delta_kind_t::BRUSH_ADDED ||
         pSource->kind == geometry_delta_kind_t::BRUSH_REMOVED ||
         pSource->kind == geometry_delta_kind_t::BRUSH_REPLACED ) {
        pDestination->brushData.sourceId = pSource->brushData.sourceId;
        common::Vector_Move(
            &pDestination->brushData.sides,
            &pSource->brushData.sides );
    }
    if ( pSource->kind == geometry_delta_kind_t::BRUSH_REPLACED ) {
        pDestination->newBrushData.sourceId =
            pSource->newBrushData.sourceId;
        common::Vector_Move(
            &pDestination->newBrushData.sides,
            &pSource->newBrushData.sides );
    }

    pSource->kind = geometry_delta_kind_t::INVALID;
    pSource->brushId = {};
    pSource->sideIndex = 0u;
    pSource->oldPlane = {};
    pSource->newPlane = {};
    pSource->brushData.sourceId = GEOMETRY_SOURCE_ID_INVALID;
    pSource->newBrushData.sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

} // namespace

// ---------------------------------------------------------------------------
// Transaction protocol
// ---------------------------------------------------------------------------

geometry_status_t GeometryTransaction_Begin(
    geometry_transaction_t *pTransaction,
    geometry_document_t *pDocument,
    geometry_source_id_t targetBrushId ) noexcept
{
    if ( pTransaction == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pTransaction->bActive ) {
        return geometry_status_t::TRANSACTION_ACTIVE;
    }
    if ( !IsCanonicalEmpty( pTransaction->baselineBrush.sides ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( targetBrushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const brush_solid_t *pBrush =
        GeometryDocument_FindBrush( pDocument, targetBrushId );
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Deep-copy the brush as the baseline.
    brush_solid_t baseline{};
    const geometry_status_t copyStatus = BrushSolid_DeepCopy(
        &baseline, pBrush, pDocument->pAllocator,
        pDocument->policy.limits );
    if ( copyStatus != geometry_status_t::OK ) {
        return copyStatus;
    }

    pTransaction->pDocument = pDocument;
    pTransaction->targetBrushId = targetBrushId;
    pTransaction->baselineRevision = pDocument->revision;
    pTransaction->baselineBrush.sourceId = baseline.sourceId;
    common::Vector_Move(
        &pTransaction->baselineBrush.sides, &baseline.sides );
    pTransaction->bActive = true;

    return geometry_status_t::OK;
}

geometry_status_t GeometryTransaction_TryPreviewSidePlane(
    geometry_transaction_t *pTransaction,
    common::usize sideIndex,
    math::planed_t newPlane ) noexcept
{
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return geometry_status_t::NO_ACTIVE_TRANSACTION;
    }

    brush_solid_t *pLiveBrush = GeometryDocument_FindBrushMutable(
        pTransaction->pDocument, pTransaction->targetBrushId );
    if ( pLiveBrush == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    return BrushSolid_TrySetSidePlane( pLiveBrush, sideIndex, newPlane );
}

geometry_status_t GeometryTransaction_TryPreviewAllPlanes(
    geometry_transaction_t *pTransaction,
    const math::planed_t *pNewPlanes,
    common::usize cPlanes ) noexcept
{
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return geometry_status_t::NO_ACTIVE_TRANSACTION;
    }
    if ( pNewPlanes == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    brush_solid_t *pLiveBrush = GeometryDocument_FindBrushMutable(
        pTransaction->pDocument, pTransaction->targetBrushId );
    if ( pLiveBrush == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const common::usize cSides = BrushSolid_SideCount( pLiveBrush );
    if ( cPlanes != cSides ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Validate the whole replacement before publishing any part of it.
    // BrushSolid_TrySetSidePlane performs the same finite-plane check, but
    // calling it in a single loop would leave an earlier plane changed when
    // a later plane is invalid.
    for ( common::usize i = 0u; i < cSides; ++i ) {
        if ( !math::Planed_IsFinite( pNewPlanes[i] ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }

    // Every possible failure has been ruled out, so publication cannot stop
    // halfway through the plane array.
    for ( common::usize i = 0u; i < cSides; ++i ) {
        pLiveBrush->sides.pData[i].plane = pNewPlanes[i];
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryTransaction_TryPreviewAddSide(
    geometry_transaction_t *pTransaction,
    const brush_solid_side_t &newSide,
    const geometry_limit_policy_t &limits ) noexcept
{
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return geometry_status_t::NO_ACTIVE_TRANSACTION;
    }

    brush_solid_t *pLiveBrush = GeometryDocument_FindBrushMutable(
        pTransaction->pDocument, pTransaction->targetBrushId );
    if ( pLiveBrush == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    return BrushSolid_TryAddSide( pLiveBrush, limits, newSide, nullptr );
}

geometry_status_t GeometryTransaction_Commit(
    geometry_transaction_t *pTransaction,
    geometry_delta_t *pDeltaOut,
    geometry_changeset_t *pChangesetOut,
    geometry_revision_t *pNewRevisionOut ) noexcept
{
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return geometry_status_t::NO_ACTIVE_TRANSACTION;
    }
    if ( pDeltaOut == nullptr || pChangesetOut == nullptr ||
         pNewRevisionOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !CommitOutputsAreDefault( *pDeltaOut, *pChangesetOut ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    geometry_document_t *pDocument = pTransaction->pDocument;

    // Stale revision check.
    if ( pDocument->revision != pTransaction->baselineRevision ) {
        return geometry_status_t::STALE_REVISION;
    }

    const brush_solid_t *pLiveBrush =
        GeometryDocument_FindBrush( pDocument, pTransaction->targetBrushId );
    if ( pLiveBrush == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    // Compare baseline to live state: count changed planes and detect
    // structural differences (side count changed by clip/slice).
    const common::usize cBaselineSides =
        BrushSolid_SideCount( &pTransaction->baselineBrush );
    const common::usize cLiveSides = BrushSolid_SideCount( pLiveBrush );

    bool bStructuralChange = ( cBaselineSides != cLiveSides );

    common::usize cChangedPlanes = 0u;
    common::usize iFirstChangedSide = cBaselineSides;
    math::planed_t firstOldPlane{};
    math::planed_t firstNewPlane{};

    if ( !bStructuralChange ) {
        for ( common::usize i = 0u; i < cBaselineSides; ++i ) {
            const brush_solid_side_t &baselineSide =
                pTransaction->baselineBrush.sides.pData[i];
            const brush_solid_side_t &liveSide =
                pLiveBrush->sides.pData[i];

            if ( baselineSide.sourceId.value != liveSide.sourceId.value ||
                 baselineSide.iAttributeIndex !=
                     liveSide.iAttributeIndex ) {
                bStructuralChange = true;
            }

            if ( !PlanedEqual( baselineSide.plane, liveSide.plane ) ) {
                ++cChangedPlanes;
                if ( iFirstChangedSide >= cBaselineSides ) {
                    iFirstChangedSide = i;
                    firstOldPlane = baselineSide.plane;
                    firstNewPlane = liveSide.plane;
                }
            }
        }
    }

    // No-op commit: nothing changed and no structural edit.
    if ( !bStructuralChange && cChangedPlanes == 0u ) {
        *pNewRevisionOut = pDocument->revision;
        DeactivateTransaction( pTransaction );
        return geometry_status_t::OK;
    }

    // Build both outputs privately. Caller-visible state is published only
    // after all allocations and the changeset insertion have succeeded.
    geometry_delta_t pendingDelta{};
    geometry_changeset_t pendingChangeset{};

    // Choose delta kind based on the nature of the change.
    if ( !bStructuralChange && cChangedPlanes == 1u ) {
        // Single-side plane edit — use the lightweight delta.
        pendingDelta.kind =
            geometry_delta_kind_t::BRUSH_SIDE_PLANE_CHANGED;
        pendingDelta.brushId = pTransaction->targetBrushId;
        pendingDelta.sideIndex = iFirstChangedSide;
        pendingDelta.oldPlane = firstOldPlane;
        pendingDelta.newPlane = firstNewPlane;
    }
    else {
        // Multiple planes changed or structural change — deep-copy both
        // states into a BRUSH_REPLACED delta.
        pendingDelta.kind = geometry_delta_kind_t::BRUSH_REPLACED;
        pendingDelta.brushId = pTransaction->targetBrushId;

        geometry_status_t copyStatus = BrushSolid_DeepCopy(
            &pendingDelta.brushData,
            &pTransaction->baselineBrush,
            pDocument->pAllocator, pDocument->policy.limits );
        if ( copyStatus != geometry_status_t::OK ) {
            GeometryDelta_Shutdown( &pendingDelta );
            return RollbackAndDeactivate( pTransaction, copyStatus );
        }

        copyStatus = BrushSolid_DeepCopy(
            &pendingDelta.newBrushData,
            pLiveBrush,
            pDocument->pAllocator, pDocument->policy.limits );
        if ( copyStatus != geometry_status_t::OK ) {
            GeometryDelta_Shutdown( &pendingDelta );
            return RollbackAndDeactivate( pTransaction, copyStatus );
        }
    }

    // Build the changeset.
    const geometry_status_t changesetStatus = GeometryChangeset_Init(
        &pendingChangeset, pDocument->pAllocator );
    if ( changesetStatus != geometry_status_t::OK ) {
        GeometryDelta_Shutdown( &pendingDelta );
        return RollbackAndDeactivate( pTransaction, changesetStatus );
    }

    const geometry_status_t addStatus = GeometryChangeset_TryAddBrush(
        &pendingChangeset, pTransaction->targetBrushId );
    if ( addStatus != geometry_status_t::OK ) {
        GeometryChangeset_Shutdown( &pendingChangeset );
        GeometryDelta_Shutdown( &pendingDelta );
        return RollbackAndDeactivate( pTransaction, addStatus );
    }

    // Preview applies structural edits directly to the live brush so tools can
    // render them immediately, but persistent identity ownership remains at
    // the committed baseline until this point. Prepare the complete registry
    // transition privately; a failure rolls the preview back and publishes
    // neither outputs nor identity membership.
    geometry_source_id_registry_t preparedRegistry{};
    bool bRegistryChanges = false;
    if ( bStructuralChange ) {
        const geometry_status_t identityStatus =
            GeometryReplacementIdentity_TryPrepare(
                pDocument,
                pTransaction->targetBrushId,
                &pTransaction->baselineBrush,
                pLiveBrush,
                &preparedRegistry,
                &bRegistryChanges );
        if ( identityStatus != geometry_status_t::OK ) {
            GeometryChangeset_Shutdown( &pendingChangeset );
            GeometryDelta_Shutdown( &pendingDelta );
            return RollbackAndDeactivate(
                pTransaction, identityStatus );
        }
    }

    // Every remaining operation is allocation-free. Publish the canonical
    // registry, outputs, and revision as one owner-thread commit sequence.
    if ( bRegistryChanges ) {
        GeometryReplacementIdentity_Publish(
            pDocument, &preparedRegistry );
    }

    MoveDelta( pDeltaOut, &pendingDelta );
    common::Vector_Move(
        &pChangesetOut->affectedBrushIds,
        &pendingChangeset.affectedBrushIds );

    if ( bRegistryChanges ) {
        GeometrySourceIdRegistry_Shutdown( &preparedRegistry );
    }

    // Advance the document revision.
    pDocument->revision += 1u;
    *pNewRevisionOut = pDocument->revision;

    DeactivateTransaction( pTransaction );
    return geometry_status_t::OK;
}

geometry_status_t GeometryTransaction_Cancel(
    geometry_transaction_t *pTransaction ) noexcept
{
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return geometry_status_t::NO_ACTIVE_TRANSACTION;
    }

    const geometry_status_t restoreStatus = RestoreBrushFromBaseline(
        pTransaction->pDocument,
        &pTransaction->baselineBrush,
        pTransaction->targetBrushId );

    DeactivateTransaction( pTransaction );
    return restoreStatus;
}

bool GeometryTransaction_IsActive(
    const geometry_transaction_t *pTransaction ) noexcept
{
    return pTransaction != nullptr && pTransaction->bActive;
}

} // namespace cypher::editor::geometry
