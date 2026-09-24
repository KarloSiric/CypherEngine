//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SourceIdRegistry.h
//  Purpose: Declares document-local persistent geometry identity ownership.
//  Details: The registry rejects duplicate authored IDs, advances one monotonic
//           allocator past loaded IDs, and creates deterministic transfer remaps.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SOURCEIDREGISTRY_H
#define CYPHER_EDITOR_GEOMETRY_SOURCEIDREGISTRY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_HashSet.h"
#include "CypherCommon_Vector.h"
#include "CypherGeometry_IdAllocator.h"

namespace cypher::editor::geometry
{

// Hash and equality operate only on the value field. Source IDs have no
// padding-dependent byte representation in registry lookup policy.
struct geometry_source_id_hasher_t {
    CYPHER_NODISCARD common::hash64_t operator()(
        const geometry_source_id_t &id ) const noexcept;
};

struct geometry_source_id_equal_t {
    CYPHER_NODISCARD bool operator()(
        const geometry_source_id_t &left,
        const geometry_source_id_t &right ) const noexcept;
};

using geometry_source_id_set_t = common::hash_set_t<
    geometry_source_id_t,
    geometry_source_id_hasher_t,
    geometry_source_id_equal_t>;

// One registry is owned by one editable document. Every access, including
// read-only queries, is confined to that document's owner thread; background
// work consumes immutable document snapshots instead. The allocator descriptor
// and its backend state must outlive the registry and every remap created from
// it.
//
// claimedIds records every identity ever admitted into this document identity
// domain. liveIds is its currently live subset. Release and Clear change only
// liveIds, so stale transaction, selection, and provenance records can never
// alias a newly admitted identity. cEntriesMax is the hard lifetime claim limit,
// not merely a simultaneous-live-element limit.
struct geometry_source_id_registry_t {
    geometry_source_id_set_t claimedIds{};
    geometry_source_id_set_t liveIds{};
    geometry_source_id_allocator_t allocator{};
    common::usize cEntriesMax{ 0u };
    const common::allocator_t *pAllocator{ nullptr };
    bool bLoadRegistrationOpen{ false };
};

struct geometry_source_id_remap_entry_t {
    geometry_source_id_t source{};
    geometry_source_id_t destination{};
};

// Entries are sorted by source.value and freshly allocated destination values
// are strictly increasing. That canonical order is part of this runtime
// contract, so import, clipboard, and duplicate operations never depend on hash
// slot order or on the order in which the caller presented the source IDs.
struct geometry_source_id_remap_t {
    common::vector_t<geometry_source_id_remap_entry_t> entries{};
};

// Initializes a new document identity domain and opens its one-time loaded-ID
// registration phase. `first` is the next/high-water identity persisted by the
// document, not a value reconstructed only from currently live elements. Zero
// is accepted as the persisted exhausted state. A failed initialization restores
// the registry to its canonical default state.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdRegistry_Init(
    geometry_source_id_registry_t *pRegistry,
    const common::allocator_t *pAllocator,
    common::usize cEntriesMax,
    common::usize cInitialCapacity = 0u,
    geometry_source_id_t first = geometry_source_id_t{ 1u } ) noexcept;

void GeometrySourceIdRegistry_Shutdown(
    geometry_source_id_registry_t *pRegistry ) noexcept;

// Clears live membership but deliberately preserves claimed membership and
// allocator.next. Previously admitted IDs stay retired even when a document
// clears all current geometry.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdRegistry_Clear(
    geometry_source_id_registry_t *pRegistry ) noexcept;

CYPHER_NODISCARD bool GeometrySourceIdRegistry_IsValid(
    const geometry_source_id_registry_t *pRegistry ) noexcept;

// Performs the linear-time identity audit used at serialization, transaction,
// and debug boundaries. It verifies nonzero claims, the live-subset relation,
// stored counts, and monotonic allocator advancement.
CYPHER_NODISCARD bool GeometrySourceIdRegistry_ValidateDeep(
    const geometry_source_id_registry_t *pRegistry ) noexcept;

CYPHER_NODISCARD bool GeometrySourceIdRegistry_IsInitialized(
    const geometry_source_id_registry_t *pRegistry ) noexcept;

// Closes the one-time document-load registration phase. The operation is
// idempotent. Fresh allocation and successful editing mutations also seal the
// phase automatically. Once sealed, Register cannot admit arbitrary old IDs;
// only RestoreRetired may reactivate an identity already present in claimedIds.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdRegistry_SealLoadedIds(
    geometry_source_id_registry_t *pRegistry ) noexcept;

// Reserves both ownership sets. Identity membership and allocator.next remain
// unchanged on failure; either backing set may retain capacity grown by an
// earlier successful allocation in the same call.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdRegistry_Reserve(
    geometry_source_id_registry_t *pRegistry,
    common::usize cEntries ) noexcept;

// Claims and activates an ID observed while bootstrapping authored document
// data. Zero is invalid. Any identity claimed earlier in this domain, live or
// retired, is reported as IDENTITY_CONFLICT. Import, duplication, clipboard,
// and merge operations must create a remap instead of registering foreign IDs.
// Success advances the monotonic allocator beyond id when necessary.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdRegistry_Register(
    geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept;

// Allocates and registers one fresh ID. Capacity is secured before allocator.next
// advances, so ordinary allocation failure leaves membership and sequence intact.
CYPHER_NODISCARD geometry_source_id_result_t
GeometrySourceIdRegistry_Allocate(
    geometry_source_id_registry_t *pRegistry ) noexcept;

// Release affects live membership only. It never rewinds the monotonic allocator.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdRegistry_Release(
    geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept;

// Reactivates an identity that this domain already claims but that is currently
// retired. This is the explicit undo/transaction-restore path; it never admits a
// foreign identity and never changes allocator.next.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdRegistry_RestoreRetired(
    geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept;

CYPHER_NODISCARD bool GeometrySourceIdRegistry_Contains(
    const geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept;

// True when this identity domain has ever admitted id, whether it is live or
// retired. A claimed-but-retired ID is restorable with RestoreRetired; an
// unclaimed ID is foreign to this document and must go through a remap.
CYPHER_NODISCARD bool GeometrySourceIdRegistry_IsClaimed(
    const geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept;

CYPHER_NODISCARD common::usize GeometrySourceIdRegistry_Count(
    const geometry_source_id_registry_t *pRegistry ) noexcept;

CYPHER_NODISCARD common::usize GeometrySourceIdRegistry_ClaimedCount(
    const geometry_source_id_registry_t *pRegistry ) noexcept;

// Creates new destination identities for the unique nonzero source IDs. The
// output must be canonical empty. On failure it remains canonical empty and no
// identity claim, live membership, or allocator sequence change is committed.
// On success the caller owns the remap and must call
// GeometrySourceIdRemap_Shutdown before the borrowed allocator expires.
CYPHER_NODISCARD geometry_status_t
GeometrySourceIdRegistry_CreateDeterministicRemap(
    geometry_source_id_registry_t *pRegistry,
    common::span_t<const geometry_source_id_t> sourceIds,
    geometry_source_id_remap_t *pRemapOut ) noexcept;

void GeometrySourceIdRemap_Shutdown(
    geometry_source_id_remap_t *pRemap ) noexcept;

CYPHER_NODISCARD bool GeometrySourceIdRemap_IsValid(
    const geometry_source_id_remap_t *pRemap ) noexcept;

CYPHER_NODISCARD bool GeometrySourceIdRemap_IsInitialized(
    const geometry_source_id_remap_t *pRemap ) noexcept;

CYPHER_NODISCARD common::usize GeometrySourceIdRemap_Count(
    const geometry_source_id_remap_t *pRemap ) noexcept;

// Uses binary search over the canonical source-sorted entry array.
CYPHER_NODISCARD bool GeometrySourceIdRemap_Find(
    const geometry_source_id_remap_t *pRemap,
    geometry_source_id_t source,
    geometry_source_id_t *pDestinationOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SOURCEIDREGISTRY_H
