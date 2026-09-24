//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Snapshot.h
//  Purpose: Declares immutable, reference-counted views of one committed
//           document revision.
//  Details: A snapshot records the revision, the policy it was validated
//           under, and one reference to every committed brush value,
//           sorted by brush source ID. Because values are immutable and the
//           snapshot holds references, later commits cannot change what a
//           snapshot shows, and the snapshot stays valid after the
//           document itself shuts down.
//
//           Snapshots are the cross-thread boundary: once acquired they
//           may be read and released from any thread. Acquisition itself
//           happens on the document's owner thread.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SNAPSHOT_H
#define CYPHER_EDITOR_GEOMETRY_SNAPSHOT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"

namespace cypher::editor::geometry
{

struct geometry_snapshot_brush_t {
    geometry_source_id_t brushId{};
    // Live handle at publication time. Resolving it against the document
    // later may report STALE_HANDLE; the snapshot's own value never goes
    // stale.
    geometry_brush_handle_t handle{};
    const geometry_brush_value_t *pValue{ nullptr };
};

struct geometry_document_snapshot_t {
    mutable common::ref_count_t refs{};
    const common::allocator_t *pAllocator{ nullptr };
    const geometry_document_t *pDomain{ nullptr };
    common::u64 revision{ 0u };
    geometry_policy_t policy{};
    common::usize cBrushes{ 0u };
    // Ascending by brushId.value; each entry holds one value reference.
    geometry_snapshot_brush_t *pBrushes{ nullptr };
};

// Returns a snapshot of the current revision with one reference owned by
// the caller. Repeated calls without an intervening apply return the same
// snapshot. On failure *ppSnapshotOut is null and the document is
// unchanged.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryAcquireSnapshot(
    geometry_document_t *pDocument,
    const geometry_document_snapshot_t **ppSnapshotOut ) noexcept;

// Null is ignored.
void GeometrySnapshot_AddRef( const geometry_document_snapshot_t *pSnapshot ) noexcept;

// The last release drops every value reference and frees the snapshot.
// Null is ignored.
void GeometrySnapshot_Release( const geometry_document_snapshot_t *pSnapshot ) noexcept;

// Zero for null.
CYPHER_NODISCARD common::u64 GeometrySnapshot_Revision(
    const geometry_document_snapshot_t *pSnapshot ) noexcept;

// Zero for null.
CYPHER_NODISCARD common::usize GeometrySnapshot_BrushCount(
    const geometry_document_snapshot_t *pSnapshot ) noexcept;

// Copies entry iIndex (ascending brush ID order). The entry's value
// pointer is borrowed from the snapshot.
CYPHER_NODISCARD geometry_status_t GeometrySnapshot_TryGetBrush(
    const geometry_document_snapshot_t *pSnapshot,
    common::usize iIndex,
    geometry_snapshot_brush_t *pEntryOut ) noexcept;

// Binary search by brush source ID. INVALID_ARGUMENT when absent.
CYPHER_NODISCARD geometry_status_t GeometrySnapshot_TryFindBrush(
    const geometry_document_snapshot_t *pSnapshot,
    geometry_source_id_t brushId,
    const geometry_brush_value_t **ppValueOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SNAPSHOT_H
