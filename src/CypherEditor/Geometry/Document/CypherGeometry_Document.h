//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Document.h
//  Purpose: Declares the geometry-local aggregate store: committed brush
//           values, source identity, policy, and monotonic revisions.
//  Details: The document is the single writer of committed geometry. It
//           never edits a brush in place. Every change arrives as a change
//           set of immutable values applied atomically against an expected
//           revision: either the whole set publishes and the revision
//           advances by exactly one, or nothing changes at all.
//
//           This is not the Mason or TileEditor scene document. Host scene
//           objects map to geometry through source IDs; the document knows
//           nothing about entities, layers, materials, or editor history.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushValue.h"
#include "CypherGeometry_SourceIdRegistry.h"

#include "CypherCommon_GenerationPool.h"
#include "CypherCommon_HashMap.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

struct geometry_document_snapshot_t;

// Maps every committed source ID -- brush roots and brush sides alike -- to
// the live handle of the brush that owns it. It is the document's answer
// to "who owns this identity right now", which the registry alone cannot
// give: the registry also holds IDs that were allocated for a pending edit
// but not yet committed.
using geometry_source_owner_map_t = common::hash_map_t<
    geometry_source_id_t,
    geometry_brush_handle_t,
    geometry_source_id_hasher_t,
    geometry_source_id_equal_t>;

// Single-writer. Every call, including queries, happens on the owner
// thread; other threads read through snapshots. The struct is neither
// copyable nor movable because committed values and snapshots record its
// address as their identity-domain token.
struct geometry_document_t {
    geometry_document_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( geometry_document_t );

    common::generation_pool_t<const geometry_brush_value_t *, geometry_brush_tag_t>
        brushes{};
    geometry_source_owner_map_t owners{};
    geometry_source_id_registry_t registry{};
    geometry_policy_t policy{};

    // Starts at 1 on Init and advances by exactly one per successful apply.
    // Zero is never a valid revision, so a zeroed "expected revision" can
    // never accidentally match.
    common::u64 revision{ 0u };

    // Total sides across committed brushes, checked against
    // policy.limits.cBrushSidesMax on every apply.
    common::u64 cSides{ 0u };

    const common::allocator_t *pAllocator{ nullptr };

    // Snapshot of the current revision, retained so repeated acquisition
    // without an intervening commit costs one reference increment.
    const geometry_document_snapshot_t *pCachedSnapshot{ nullptr };

    common::bool_t bInitialized{ false };
};

struct geometry_document_desc_t {
    // Must outlive the document, every value it creates, and every snapshot
    // it publishes, and must be thread-safe if snapshots cross threads.
    const common::allocator_t *pAllocator{ nullptr };
    geometry_policy_t policy{};
    // High-water source ID persisted with the document; 1 for a new one.
    geometry_source_id_t firstSourceId{ 1u };
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Validates the policy (INVALID_ARGUMENT when GeometryPolicy_IsValid fails)
// and initializes an empty document at revision 1. On failure the document
// is left in its default state.
CYPHER_NODISCARD geometry_status_t GeometryDocument_Init(
    geometry_document_t *pDocument,
    const geometry_document_desc_t &desc ) noexcept;

// Drops the document's references to every committed value and to the
// cached snapshot. Values and snapshots still referenced elsewhere stay
// alive and valid. Safe on a default or already-shut-down document.
void GeometryDocument_Shutdown( geometry_document_t *pDocument ) noexcept;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

// Zero for an uninitialized document.
CYPHER_NODISCARD common::u64 GeometryDocument_Revision(
    const geometry_document_t *pDocument ) noexcept;

CYPHER_NODISCARD common::usize GeometryDocument_BrushCount(
    const geometry_document_t *pDocument ) noexcept;

// Resolves a brush root source ID to its live handle. INVALID_ARGUMENT when
// the ID is not a committed brush root (including when it names a side).
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryFindBrush(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    geometry_brush_handle_t *pHandleOut ) noexcept;

// Returns the committed value behind a live handle. The pointer is
// borrowed: it stays valid until the next successful apply or Shutdown
// unless the caller takes its own reference with BrushValue_AddRef.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryGetBrush(
    const geometry_document_t *pDocument,
    geometry_brush_handle_t handle,
    const geometry_brush_value_t **ppValueOut ) noexcept;

// Convenience composition of TryFindBrush and TryGetBrush.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryGetBrushById(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    const geometry_brush_value_t **ppValueOut ) noexcept;

// Finds the brush root that owns a committed source ID (the brush's own ID
// or any of its side IDs). INVALID_ARGUMENT when no committed brush owns it.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryFindOwner(
    const geometry_document_t *pDocument,
    geometry_source_id_t id,
    geometry_source_id_t *pBrushIdOut ) noexcept;

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

// Allocates idsOut.count fresh source IDs from the document's registry.
// All-or-nothing: on failure no ID is handed out, though IDs claimed during
// a partially failed attempt stay retired forever, as the registry's
// never-reuse contract requires. Fresh IDs are live but unowned until a
// committed value uses them; release unused ones with ReleasePendingId.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryAllocateSourceIds(
    geometry_document_t *pDocument,
    common::span_t<geometry_source_id_t> idsOut ) noexcept;

// Retires a fresh ID that no committed brush uses (a cancelled edit's
// allocation). IDENTITY_CONFLICT when a committed brush owns it;
// INVALID_ARGUMENT when it is not live in this document.
CYPHER_NODISCARD geometry_status_t GeometryDocument_ReleasePendingId(
    geometry_document_t *pDocument,
    geometry_source_id_t id ) noexcept;

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

// Creates an immutable value bound to this document's allocator, policy,
// and identity domain. See BrushValue_TryCreate for statuses. The value is
// not published until a change set that references it is applied.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryCreateBrushValue(
    const geometry_document_t *pDocument,
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pAttributes,
    const geometry_brush_value_t **ppValueOut,
    brush_validation_result_t *pValidationOut ) noexcept;

// ---------------------------------------------------------------------------
// Change sets
// ---------------------------------------------------------------------------

enum class geometry_document_change_kind_t : common::u8 {
    INVALID = 0u,
    INSERT_BRUSH,   // publish pValue as a new brush named brushId
    REPLACE_BRUSH,  // point existing brush brushId at pValue
    REMOVE_BRUSH,   // unpublish brush brushId; pValue must be null
    COUNT
};

// Changes target brushes by persistent source ID, never by handle, so an
// undo record stays valid after the brush it names was removed and
// re-inserted under a new handle. pValue is borrowed: a successful apply
// takes the document's own reference.
struct geometry_document_change_t {
    geometry_document_change_kind_t kind{ geometry_document_change_kind_t::INVALID };
    geometry_source_id_t brushId{};
    const geometry_brush_value_t *pValue{ nullptr };
};

// Applies a change set atomically.
//
// Preconditions checked, in order, with nothing mutated on failure:
//   NOT_INITIALIZED    document not initialized
//   STALE_REVISION     expectedRevision != current revision
//   INVALID_ARGUMENT   empty set, set larger than cJournalRecordsMax, a
//                      malformed change, a value from another document,
//                      a value whose brush ID differs from brushId, or the
//                      same brushId named twice
//   INVALID_HANDLE     REPLACE/REMOVE of a brush that is not committed
//   IDENTITY_CONFLICT  INSERT of an existing brush; a new value using an
//                      ID foreign to this document, an ID used twice
//                      across the set, or an ID owned by a brush the set
//                      does not also replace or remove
//   LIMIT_EXCEEDED     brush-count or total-side limit
//   ALLOCATION_FAILED  reserving pool, owner-map, or registry capacity
//
// Identity rules: a REPLACE may add, keep, or drop side IDs; dropped IDs
// are retired in the registry. New values may use fresh IDs (live and
// unowned), IDs retired earlier (restored, which is how undo brings back a
// removed brush), or IDs freed by another change in the same set.
//
// On success the revision advances by exactly one, the cached snapshot is
// dropped, and *pNewRevisionOut (optional) receives the new revision.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryApply(
    geometry_document_t *pDocument,
    common::u64 expectedRevision,
    common::span_t<const geometry_document_change_t> changes,
    common::u64 *pNewRevisionOut ) noexcept;

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

// Linear audit of every document invariant: pool and registry structure,
// every committed value from this domain, owner map exactly covering the
// committed IDs and pointing at the right brush, every owned ID live in the
// registry, and the side total. For debug, transaction, and serialization
// boundaries; never required for correctness of the public API.
CYPHER_NODISCARD common::bool_t GeometryDocument_ValidateDeep(
    const geometry_document_t *pDocument ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_H
