//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushEditPipeline.cpp
//  Purpose: Implements the orchestrated brush edit workflow.
//  Details: The pipeline wraps the transaction system and adds boundary
//           validation and spatial index integration. The workflow is:
//
//           1. Init — allocates the working boundary
//           2. Begin — starts a transaction on the target brush
//           3. (caller applies preview mutations via the transaction)
//           4. Validate — reconstructs boundary, checks for degeneracy
//           5. Commit — produces delta/changeset, refits spatial index
//              OR Cancel — restores baseline state
//
//           The pipeline does not own the document, spatial index, or
//           selection set — it borrows them for the duration of the edit.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushEditPipeline.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t BrushEditPipeline_Init(
    brush_edit_pipeline_t *pPipeline,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pPipeline == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pPipeline->bInitialized ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    const geometry_status_t status =
        BrushBoundary_Init( &pPipeline->workingBoundary, pAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    pPipeline->bInitialized = true;
    pPipeline->bBoundaryValid = false;
    return geometry_status_t::OK;
}

void BrushEditPipeline_Shutdown(
    brush_edit_pipeline_t *pPipeline ) noexcept
{
    if ( pPipeline == nullptr ) {
        return;
    }

    if ( GeometryTransaction_IsActive( &pPipeline->transaction ) ) {
        (void)GeometryTransaction_Cancel( &pPipeline->transaction );
    }

    BrushBoundary_Shutdown( &pPipeline->workingBoundary );
    pPipeline->bBoundaryValid = false;
    pPipeline->bInitialized = false;
}

// ---------------------------------------------------------------------------
// Edit protocol
// ---------------------------------------------------------------------------

geometry_status_t BrushEditPipeline_Begin(
    brush_edit_pipeline_t *pPipeline,
    geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept
{
    if ( pPipeline == nullptr || !pPipeline->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    pPipeline->bBoundaryValid = false;

    return GeometryTransaction_Begin(
        &pPipeline->transaction, pDocument, brushId );
}

geometry_status_t BrushEditPipeline_Validate(
    brush_edit_pipeline_t *pPipeline ) noexcept
{
    if ( pPipeline == nullptr || !pPipeline->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryTransaction_IsActive( &pPipeline->transaction ) ) {
        return geometry_status_t::NO_ACTIVE_TRANSACTION;
    }

    pPipeline->bBoundaryValid = false;

    // Find the live brush through the transaction's document pointer.
    const brush_solid_t *pLiveBrush = GeometryDocument_FindBrush(
        pPipeline->transaction.pDocument,
        pPipeline->transaction.targetBrushId );
    if ( pLiveBrush == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    // Reconstruct boundary from the current (preview-mutated) brush.
    const geometry_status_t status = BrushBoundary_TryReconstruct(
        &pPipeline->workingBoundary, pLiveBrush,
        pPipeline->transaction.pDocument->policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    pPipeline->bBoundaryValid = true;
    return geometry_status_t::OK;
}

geometry_status_t BrushEditPipeline_Commit(
    brush_edit_pipeline_t *pPipeline,
    geometry_spatial_index_t *pSpatialIndex,
    brush_edit_result_t *pResultOut ) noexcept
{
    if ( pPipeline == nullptr || !pPipeline->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pResultOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometryTransaction_IsActive( &pPipeline->transaction ) ) {
        return geometry_status_t::NO_ACTIVE_TRANSACTION;
    }
    if ( !pPipeline->bBoundaryValid ) {
        // Validation is the publication gate for preview mutations.  Keep the
        // transaction active so the caller can repair the preview, validate it,
        // or cancel it without losing the captured baseline.
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    // Commit the transaction — produces delta, changeset, revision.
    const geometry_status_t commitStatus = GeometryTransaction_Commit(
        &pPipeline->transaction,
        &pResultOut->delta,
        &pResultOut->changeset,
        &pResultOut->newRevision );

    if ( commitStatus != geometry_status_t::OK ) {
        pPipeline->bBoundaryValid = false;
        return commitStatus;
    }

    // Refit spatial index if we have a valid boundary and the edit
    // produced an actual change (non-INVALID delta).
    if ( pSpatialIndex != nullptr &&
         pPipeline->bBoundaryValid &&
         pResultOut->delta.kind != geometry_delta_kind_t::INVALID ) {

        const math::aabbd_t newBounds =
            BrushQueries_ComputeBoundsd( &pPipeline->workingBoundary );

        const geometry_status_t refitStatus =
            GeometrySpatialIndex_TryRefit(
                pSpatialIndex,
                pResultOut->delta.brushId,
                newBounds );

        pResultOut->bSpatialRefitted =
            ( refitStatus == geometry_status_t::OK );
    }

    pPipeline->bBoundaryValid = false;
    return geometry_status_t::OK;
}

geometry_status_t BrushEditPipeline_Cancel(
    brush_edit_pipeline_t *pPipeline ) noexcept
{
    if ( pPipeline == nullptr || !pPipeline->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    pPipeline->bBoundaryValid = false;

    return GeometryTransaction_Cancel( &pPipeline->transaction );
}

geometry_transaction_t *BrushEditPipeline_GetTransaction(
    brush_edit_pipeline_t *pPipeline ) noexcept
{
    if ( pPipeline == nullptr || !pPipeline->bInitialized ) {
        return nullptr;
    }
    return &pPipeline->transaction;
}

const brush_boundary_t *BrushEditPipeline_GetBoundary(
    const brush_edit_pipeline_t *pPipeline ) noexcept
{
    if ( pPipeline == nullptr || !pPipeline->bBoundaryValid ) {
        return nullptr;
    }
    return &pPipeline->workingBoundary;
}

bool BrushEditPipeline_IsActive(
    const brush_edit_pipeline_t *pPipeline ) noexcept
{
    return pPipeline != nullptr &&
           pPipeline->bInitialized &&
           GeometryTransaction_IsActive( &pPipeline->transaction );
}

} // namespace cypher::editor::geometry
