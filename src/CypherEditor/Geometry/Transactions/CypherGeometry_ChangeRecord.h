//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_ChangeRecord.h
//  Purpose: Declares the invertible, typed delta produced by one committed
//           geometry transaction.
//  Details: Each entry names one brush by persistent source ID and holds a
//           reference to the committed value before and after the change.
//           A null "before" is an insertion and a null "after" a removal.
//           Because both sides are immutable values, the same record
//           applies forward (redo) or inverse (undo) and restores the exact
//           prior values -- the same pointers, planes, attributes, and
//           identities -- rather than an approximation rebuilt from
//           parameters.
//
//           The record is also the change set hosts consume to update
//           selection, spatial indices, and render caches: every affected
//           brush and the kind of change is listed explicitly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CHANGE_RECORD_H
#define CYPHER_EDITOR_GEOMETRY_CHANGE_RECORD_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"

#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Both value pointers own one reference while the entry is held by a
// transaction or a record. pBefore == pAfter is a no-op and never appears
// in a committed record.
struct geometry_change_entry_t {
    geometry_source_id_t brushId{};
    const geometry_brush_value_t *pBefore{ nullptr };
    const geometry_brush_value_t *pAfter{ nullptr };
};

enum class geometry_change_kind_t : common::u8 {
    NONE = 0u,  // before == after
    INSERTED,   // before null, after present
    MODIFIED,   // both present and different
    REMOVED,    // before present, after null
    COUNT
};

CYPHER_NODISCARD geometry_change_kind_t GeometryChangeEntry_Kind(
    const geometry_change_entry_t &entry ) noexcept;

enum class geometry_change_direction_t : common::u8 {
    FORWARD = 0u, // before -> after (commit, redo)
    INVERSE,      // after -> before (undo)
    COUNT
};

// Heap-allocated and owned by exactly one holder at a time: the caller of
// GeometryTransaction_TryCommit until it hands the record to a history.
// Not copyable or movable; transfer ownership by pointer.
struct geometry_change_record_t {
    geometry_change_record_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( geometry_change_record_t );

    const common::allocator_t *pAllocator{ nullptr };
    const geometry_document_t *pDomain{ nullptr };
    // Entries in the order the transaction first touched each brush.
    common::vector_t<geometry_change_entry_t> entries{};
    // Document revision the record was committed against, and the revision
    // its commit produced.
    common::u64 revisionBefore{ 0u };
    common::u64 revisionAfter{ 0u };
};

// Allocates an empty record able to hold cEntries entries without further
// allocation. Internal to the Transactions module; exposed for tests and
// for future importers of serialized undo payloads.
CYPHER_NODISCARD geometry_status_t GeometryChangeRecord_TryCreate(
    const common::allocator_t *pAllocator,
    const geometry_document_t *pDomain,
    common::usize cEntries,
    geometry_change_record_t **ppRecordOut ) noexcept;

// Releases every value reference and frees the record. Null is ignored.
void GeometryChangeRecord_Destroy( geometry_change_record_t *pRecord ) noexcept;

CYPHER_NODISCARD common::usize GeometryChangeRecord_Count(
    const geometry_change_record_t *pRecord ) noexcept;

// Copies entry iIndex. The copied value pointers are borrowed from the
// record.
CYPHER_NODISCARD geometry_status_t GeometryChangeRecord_TryGetEntry(
    const geometry_change_record_t *pRecord,
    common::usize iIndex,
    geometry_change_entry_t *pEntryOut ) noexcept;

// Applies the record to its document in the given direction against the
// expected revision, with GeometryDocument_TryApply's atomicity: on any
// failure the document is unchanged. The record itself is never modified.
CYPHER_NODISCARD geometry_status_t GeometryChangeRecord_TryApply(
    const geometry_change_record_t *pRecord,
    geometry_document_t *pDocument,
    geometry_change_direction_t direction,
    common::u64 expectedRevision,
    common::u64 *pNewRevisionOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CHANGE_RECORD_H
