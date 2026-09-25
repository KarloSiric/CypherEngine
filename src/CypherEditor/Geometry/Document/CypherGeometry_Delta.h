//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Delta.h
//  Purpose: Declares typed geometry change records with inverse computation.
//  Details: A delta records one atomic change to the geometry document. Gate 3
//           supports three delta kinds: BRUSH_ADDED, BRUSH_REMOVED, and
//           BRUSH_SIDE_PLANE_CHANGED. Each delta carries enough data to
//           compute its inverse, enabling undo without full-state snapshots.
//
//           A change set aggregates the source IDs affected by a committed
//           transaction, used for invalidation notification.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DELTA_H
#define CYPHER_EDITOR_GEOMETRY_DELTA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Discriminates the type of mutation a delta records. Each kind determines
// which fields in geometry_delta_t are populated and how the inverse is
// computed.
enum class geometry_delta_kind_t : common::u8 {
    INVALID = 0u,

    // A whole brush was added to the document. The delta stores the full
    // brush data so the inverse (removal) can find it by source ID.
    BRUSH_ADDED,

    // A whole brush was removed. The delta stores the full brush data so
    // the inverse (re-addition) can restore exact authored state.
    BRUSH_REMOVED,

    // One side's plane was changed in place. The delta stores the brush
    // source ID, side index, old plane, and new plane. The inverse swaps
    // old and new.
    BRUSH_SIDE_PLANE_CHANGED,

    // The brush was fully replaced — all planes may have changed, sides
    // may have been added or removed. Used by transforms (all planes
    // rotate/translate simultaneously), clip (adds a side), and any edit
    // that mutates a brush structurally. Stores deep copies of the brush
    // before and after the edit; inverse swaps them.
    BRUSH_REPLACED,

    // One surface record (material + UV projection) of a brush was changed.
    // sideIndex holds the record index; oldAttribute/newAttribute the
    // values. The inverse swaps them.
    BRUSH_ATTRIBUTE_CHANGED,

    COUNT
};

// One atomic change record. Gate 3 uses a tagged union style — the kind
// discriminates which fields are valid.
//
// For BRUSH_ADDED and BRUSH_REMOVED, brushData is a deep copy of the
// full brush at the time of the mutation. For BRUSH_SIDE_PLANE_CHANGED,
// only brushId / sideIndex / oldPlane / newPlane are populated.
struct geometry_delta_t {
    geometry_delta_kind_t kind{ geometry_delta_kind_t::INVALID };

    // Identifies the brush this delta applies to.
    geometry_source_id_t brushId{};

    // For BRUSH_SIDE_PLANE_CHANGED: which side within the brush.
    common::usize sideIndex{ 0u };

    // For BRUSH_SIDE_PLANE_CHANGED: the plane values before and after.
    math::planed_t oldPlane{};
    math::planed_t newPlane{};

    // For BRUSH_ADDED / BRUSH_REMOVED: deep copy of the full brush.
    // For BRUSH_REPLACED: the brush state BEFORE the edit (old state).
    // Owned by whoever owns this delta; must be shut down with
    // GeometryDelta_Shutdown.
    brush_solid_t brushData{};

    // For BRUSH_REPLACED only: the brush state AFTER the edit (new state).
    // Applying the delta restores newBrushData; the inverse swaps
    // brushData and newBrushData.
    brush_solid_t newBrushData{};

    // For BRUSH_ADDED / BRUSH_REMOVED: the brush's surface records; for
    // BRUSH_REPLACED: the records BEFORE the edit (newAttributes holds them
    // after). Left uninitialized by producers that did not capture them;
    // applying such a delta gives the brush default records (ADDED) or keeps
    // its current ones (REPLACED), extended to cover every side either way.
    geometry_brush_side_attribute_store_t attributes{};
    geometry_brush_side_attribute_store_t newAttributes{};

    // For BRUSH_ATTRIBUTE_CHANGED: the record's value before and after.
    geometry_brush_side_attributes_t oldAttribute{};
    geometry_brush_side_attributes_t newAttribute{};
};

// A set of source IDs affected by a committed transaction. Consumers use
// this for invalidation (e.g. boundary reconstruction, tessellation cache,
// spatial index) without having to inspect every delta.
struct geometry_changeset_t {
    common::vector_t<geometry_source_id_t> affectedBrushIds{};
};

// ---------------------------------------------------------------------------
// Delta lifecycle
// ---------------------------------------------------------------------------

void GeometryDelta_Shutdown(
    geometry_delta_t *pDelta ) noexcept;

// Creates the inverse of a delta. The inverse undoes the original:
//   BRUSH_ADDED     -> BRUSH_REMOVED  (same brush data)
//   BRUSH_REMOVED   -> BRUSH_ADDED    (same brush data)
//   SIDE_PLANE_CHANGED -> SIDE_PLANE_CHANGED (old/new swapped)
//
// pInverseOut must be in canonical default state. On failure it remains
// default. The brush data in pInverseOut is a fresh deep copy.
CYPHER_NODISCARD geometry_status_t GeometryDelta_TryComputeInverse(
    const geometry_delta_t *pDelta,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits,
    geometry_delta_t *pInverseOut ) noexcept;

// Applies a delta to a document. Used by undo/redo to replay recorded
// changes. BRUSH_REPLACED publishes the complete side records and matching
// source-ID registry state only after all allocation and identity checks
// succeed. A failed replay leaves the document, identity registry, and
// revision unchanged. The document revision is NOT advanced — that is the
// caller's responsibility.
CYPHER_NODISCARD geometry_status_t GeometryDelta_TryApplyToDocument(
    const geometry_delta_t *pDelta,
    geometry_document_t *pDocument ) noexcept;

// Builds a BRUSH_ATTRIBUTE_CHANGED delta setting record iRecord of a
// document brush to newValue (the old value is read from the document). The
// document is not changed; apply the delta to perform the edit. Unknown
// brush or record -> INVALID_ARGUMENT; an invalid record -> its validation
// status. pDeltaOut must be default.
CYPHER_NODISCARD geometry_status_t GeometryDelta_TryMakeAttributeChange(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    common::usize iRecord,
    const geometry_brush_side_attributes_t &newValue,
    geometry_delta_t *pDeltaOut ) noexcept;

// Captures a document brush with its surface records as a BRUSH_ADDED or
// BRUSH_REMOVED delta (deep copies), so removing it and undoing the removal
// restores materials and UVs exactly. The document is not changed.
// pDeltaOut must be default.
CYPHER_NODISCARD geometry_status_t GeometryDelta_TryCaptureBrush(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    geometry_delta_kind_t kind,
    const common::allocator_t *pAllocator,
    geometry_delta_t *pDeltaOut ) noexcept;

// ---------------------------------------------------------------------------
// Change set lifecycle
// ---------------------------------------------------------------------------

CYPHER_NODISCARD geometry_status_t GeometryChangeset_Init(
    geometry_changeset_t *pChangeset,
    const common::allocator_t *pAllocator ) noexcept;

void GeometryChangeset_Shutdown(
    geometry_changeset_t *pChangeset ) noexcept;

// Adds a brush source ID to the affected set. Duplicates are silently
// ignored.
CYPHER_NODISCARD geometry_status_t GeometryChangeset_TryAddBrush(
    geometry_changeset_t *pChangeset,
    geometry_source_id_t brushId ) noexcept;

CYPHER_NODISCARD common::usize GeometryChangeset_Count(
    const geometry_changeset_t *pChangeset ) noexcept;

CYPHER_NODISCARD bool GeometryChangeset_Contains(
    const geometry_changeset_t *pChangeset,
    geometry_source_id_t brushId ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DELTA_H
