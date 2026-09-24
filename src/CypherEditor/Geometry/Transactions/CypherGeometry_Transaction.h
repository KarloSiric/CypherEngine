//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Transaction.h
//  Purpose: Declares preview/commit/cancel transactions over a geometry
//           document.
//  Details: A transaction records, per brush, the committed value at its
//           base revision and the current preview value. Previews never
//           touch the document: an interactive drag replaces the preview
//           value many times, and commit publishes only the net change as
//           one revision and one invertible record. Cancel drops the
//           previews and retires any source IDs the transaction allocated,
//           leaving the document exactly as it was.
//
//           Every preview call is failure-atomic. When a candidate value
//           is rejected, the transaction keeps its last valid preview so a
//           host can show diagnostics without losing the user's work.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_TRANSACTION_H
#define CYPHER_EDITOR_GEOMETRY_TRANSACTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_ChangeRecord.h"

namespace cypher::editor::geometry
{

// One writer, on the document's owner thread. The document must outlive
// every active transaction on it. Destroying an active transaction
// cancels it.
struct geometry_transaction_t {
    geometry_transaction_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( geometry_transaction_t );
    ~geometry_transaction_t() noexcept;

    geometry_document_t *pDocument{ nullptr };
    common::u64 baseRevision{ 0u };
    // One entry per touched brush, in first-touch order. Entries whose
    // before and after are equal (for example insert-then-remove) stay as
    // placeholders and are dropped at commit.
    common::vector_t<geometry_change_entry_t> entries{};
    // Source IDs allocated through this transaction. Those no committed
    // value uses are retired at commit or cancel.
    common::vector_t<geometry_source_id_t> pendingIds{};
    common::bool_t bActive{ false };
};

// Starts a transaction against the document's current revision.
// ALREADY_INITIALIZED if the transaction is already active.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_Begin(
    geometry_transaction_t *pTransaction,
    geometry_document_t *pDocument ) noexcept;

CYPHER_NODISCARD common::bool_t GeometryTransaction_IsActive(
    const geometry_transaction_t *pTransaction ) noexcept;

// Allocates fresh source IDs for values this transaction will create.
// All-or-nothing, like GeometryDocument_TryAllocateSourceIds.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryAllocateSourceIds(
    geometry_transaction_t *pTransaction,
    common::span_t<geometry_source_id_t> idsOut ) noexcept;

// Every preview call fails with NOT_INITIALIZED when the transaction is
// inactive, STALE_REVISION when the document advanced past the base
// revision, and INVALID_ARGUMENT for a null value or one created by a
// different document. On any failure the transaction is unchanged.

// Previews publication of a new brush. IDENTITY_CONFLICT when the brush
// exists in the preview (committed and not removed, or already inserted).
// A brush removed earlier in this transaction may be re-inserted.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryPreviewInsert(
    geometry_transaction_t *pTransaction,
    const geometry_brush_value_t *pValue ) noexcept;

// Previews a new value for an existing brush; repeated calls replace the
// preview. INVALID_HANDLE when the brush does not exist in the preview.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryPreviewReplace(
    geometry_transaction_t *pTransaction,
    const geometry_brush_value_t *pValue ) noexcept;

// Previews removal. INVALID_HANDLE when the brush does not exist in the
// preview. Removing a brush inserted by this transaction cancels the
// insertion.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryPreviewRemove(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId ) noexcept;

// The brush as the preview currently shows it: the preview value if the
// transaction touched it, otherwise the committed value. INVALID_ARGUMENT
// when the brush does not exist in the preview. The pointer is borrowed.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryGetPreview(
    const geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    const geometry_brush_value_t **ppValueOut ) noexcept;

// Number of brushes whose preview differs from the base revision.
CYPHER_NODISCARD common::usize GeometryTransaction_ChangeCount(
    const geometry_transaction_t *pTransaction ) noexcept;

// Publishes the net change as exactly one document revision.
//
// On OK the transaction ends and *ppRecordOut receives the invertible
// record (caller-owned; destroy it or push it into a history). When the
// net change is empty the commit is a no-op: OK, no revision, and
// *ppRecordOut is null.
//
// On failure (STALE_REVISION, IDENTITY_CONFLICT, LIMIT_EXCEEDED,
// ALLOCATION_FAILED, ...) nothing is published, *ppRecordOut is null, and
// the transaction stays active with its previews intact, so the host can
// adjust the preview or cancel.
CYPHER_NODISCARD geometry_status_t GeometryTransaction_TryCommit(
    geometry_transaction_t *pTransaction,
    geometry_change_record_t **ppRecordOut ) noexcept;

// Ends the transaction without publishing. Releases every preview value
// and retires every source ID the transaction allocated. Safe on an
// inactive transaction.
void GeometryTransaction_Cancel( geometry_transaction_t *pTransaction ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_TRANSACTION_H
