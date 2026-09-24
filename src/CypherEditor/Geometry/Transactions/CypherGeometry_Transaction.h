//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Transaction.h
//  Purpose: Declares the begin/preview/commit/cancel transaction protocol.
//  Details: Every mutation to a geometry document goes through a transaction.
//           The transaction captures a baseline snapshot at begin, applies
//           preview mutations without advancing the revision, and on commit
//           produces a single delta plus changeset that describes the net
//           effect. Cancel restores the document to exact baseline state.
//
//           One transaction at a time per document — no nesting. This
//           matches how editors work: one active tool operation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_TRANSACTION_H
#define CYPHER_EDITOR_GEOMETRY_TRANSACTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Delta.h"
#include "CypherGeometry_Snapshot.h"

namespace cypher::editor::geometry
{

// Active transaction state. Owned by the code driving the edit (e.g. a
// side-plane drag handler). One transaction at a time per document.
struct geometry_transaction_t {
    geometry_document_t *pDocument{ nullptr };

    // Baseline: the brush state before the transaction began. Used to
    // restore on cancel and to compute the net delta on commit.
    brush_solid_t baselineBrush{};

    // Which brush this transaction is editing.
    geometry_source_id_t targetBrushId{};

    // The document revision when the transaction began. Commit rejects
    // if the document's revision has advanced since.
    geometry_revision_t baselineRevision{ GEOMETRY_REVISION_INITIAL };

    bool bActive{ false };
};

// ---------------------------------------------------------------------------
// Transaction protocol
// ---------------------------------------------------------------------------

// Begins a transaction targeting one brush for side-plane edits.
// Captures the brush's current state as the baseline.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_Begin(
    geometry_transaction_t *pTransaction,
    geometry_document_t *pDocument,
    geometry_source_id_t targetBrushId ) noexcept;

// Applies a preview mutation: sets one side's plane to a new value.
// The document is mutated directly so rendering sees the change, but
// the revision is NOT advanced.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryPreviewSidePlane(
    geometry_transaction_t *pTransaction,
    common::usize sideIndex,
    math::planed_t newPlane ) noexcept;

// Applies a preview mutation to ALL side planes at once. Used by
// transform operations (translate, rotate, scale) that modify every
// plane simultaneously. pNewPlanes must contain exactly
// BrushSolid_SideCount planes, one per side in index order.
// The replacement is atomic: invalid input leaves every live plane unchanged.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryPreviewAllPlanes(
    geometry_transaction_t *pTransaction,
    const math::planed_t *pNewPlanes,
    common::usize cPlanes ) noexcept;

// Applies a preview that adds a fully formed side to the brush. Used by clip
// operations. The caller supplies the side's persistent source ID.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryPreviewAddSide(
    geometry_transaction_t *pTransaction,
    const brush_solid_side_t &newSide,
    const geometry_limit_policy_t &limits ) noexcept;

// Commits the transaction. Computes the net delta from baseline to
// current state, advances the revision, and writes results to the
// output parameters.
//
// pDeltaOut must point to a default-initialized delta. On success with
// one plane change it receives BRUSH_SIDE_PLANE_CHANGED; multi-plane or
// structural changes receive BRUSH_REPLACED. On no-op (nothing changed),
// kind stays INVALID and revision is not advanced.
//
// pChangesetOut must point to a default-initialized changeset. On
// success it receives the affected brush IDs.
//
// pNewRevisionOut receives the new revision (or current if no-op).
//
// Returns STALE_REVISION if the document was mutated since Begin.
// On any construction/allocation failure, the live brush is rolled back to
// its complete baseline and both output objects remain default-initialized.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_Commit(
    geometry_transaction_t *pTransaction,
    geometry_delta_t *pDeltaOut,
    geometry_changeset_t *pChangesetOut,
    geometry_revision_t *pNewRevisionOut ) noexcept;

// Cancels the transaction: restores the brush to its exact baseline
// state and deactivates the transaction.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_Cancel(
    geometry_transaction_t *pTransaction ) noexcept;

CYPHER_NODISCARD bool GeometryTransaction_IsActive(
    const geometry_transaction_t *pTransaction ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_TRANSACTION_H
