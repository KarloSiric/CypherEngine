//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushEditPipeline.h
//  Purpose: Declares the orchestrated brush edit workflow.
//  Details: Every brush mutation follows: preflight → begin → preview →
//           validate → commit/cancel. This pipeline composes the
//           transaction system with boundary validation, spatial index
//           invalidation, and selection remapping.
//
//           The pipeline owns a transaction, a working boundary, and
//           caches the edit result for the caller to inspect before
//           deciding to commit or cancel.
//
//           One pipeline instance handles one edit at a time. Nested
//           pipelines are not supported — the underlying transaction
//           system enforces one-at-a-time per document.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_EDIT_PIPELINE_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_EDIT_PIPELINE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Transaction.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_SpatialIndex.h"
#include "CypherGeometry_SelectionSet.h"

namespace cypher::editor::geometry
{

// Result of a committed edit pipeline. Contains the delta, changeset,
// new revision, and whether the spatial index and selection were
// updated. The caller owns the delta and changeset and must shut
// them down when done.
struct brush_edit_result_t {
    geometry_delta_t delta{};
    geometry_changeset_t changeset{};
    geometry_revision_t newRevision{ GEOMETRY_REVISION_INITIAL };
    bool bSpatialRefitted{ false };
};

// Active edit pipeline state.
struct brush_edit_pipeline_t {
    geometry_transaction_t transaction{};
    brush_boundary_t workingBoundary{};
    bool bBoundaryValid{ false };
    bool bInitialized{ false };
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Initializes the pipeline's working boundary with the given allocator.
// Must be called once before any edit begins.
CYPHER_NODISCARD geometry_status_t BrushEditPipeline_Init(
    brush_edit_pipeline_t *pPipeline,
    const common::allocator_t *pAllocator ) noexcept;

void BrushEditPipeline_Shutdown(
    brush_edit_pipeline_t *pPipeline ) noexcept;

// ---------------------------------------------------------------------------
// Edit protocol
// ---------------------------------------------------------------------------

// Begins an edit on a target brush. Captures baseline state.
CYPHER_NODISCARD geometry_status_t BrushEditPipeline_Begin(
    brush_edit_pipeline_t *pPipeline,
    geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept;

// Validates the current brush state by reconstructing the boundary.
// Must be called after applying preview mutations (through the
// transaction) and before commit. Sets bBoundaryValid on success.
//
// The caller applies preview mutations directly through the
// transaction (TryPreviewSidePlane, TryPreviewAllPlanes, etc.)
// between Begin and Validate.
CYPHER_NODISCARD geometry_status_t BrushEditPipeline_Validate(
    brush_edit_pipeline_t *pPipeline ) noexcept;

// Commits the edit, produces a delta and changeset, advances the
// revision, and optionally refits the spatial index.
//
// pSpatialIndex may be null if no spatial tracking is active.
// The working boundary from Validate is used to compute new bounds.
// Commit is rejected with INVALID_TOPOLOGY until Validate has succeeded for
// the current preview.  A rejected commit leaves the transaction active so
// the caller can repair/revalidate the preview or cancel it.
//
// pResultOut must point to a default-initialized result. The caller
// owns the delta and changeset on success.
CYPHER_NODISCARD geometry_status_t BrushEditPipeline_Commit(
    brush_edit_pipeline_t *pPipeline,
    geometry_spatial_index_t *pSpatialIndex,
    brush_edit_result_t *pResultOut ) noexcept;

// Cancels the edit, restoring the brush to its exact baseline state.
CYPHER_NODISCARD geometry_status_t BrushEditPipeline_Cancel(
    brush_edit_pipeline_t *pPipeline ) noexcept;

// Returns the pipeline's internal transaction for preview mutations.
// The caller should use GeometryTransaction_TryPreviewSidePlane,
// TryPreviewAllPlanes, or TryPreviewAddSide on this transaction.
CYPHER_NODISCARD geometry_transaction_t *BrushEditPipeline_GetTransaction(
    brush_edit_pipeline_t *pPipeline ) noexcept;

// Returns the working boundary after a successful Validate call.
// The boundary is valid only between Validate and Commit/Cancel.
CYPHER_NODISCARD const brush_boundary_t *BrushEditPipeline_GetBoundary(
    const brush_edit_pipeline_t *pPipeline ) noexcept;

CYPHER_NODISCARD bool BrushEditPipeline_IsActive(
    const brush_edit_pipeline_t *pPipeline ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_EDIT_PIPELINE_H
