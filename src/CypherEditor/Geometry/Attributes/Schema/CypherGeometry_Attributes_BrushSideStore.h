//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_BrushSideStore.h
//  Purpose: Declares bounded, failure-atomic storage for brush-side attributes.
//  Details: Records are stored positionally and enumerated in insertion order.
//           The store does not own side identity -- mapping a brush side to an
//           index is the representation's job, not the attribute layer's.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Brush Side Store Contract

Every mutating call is failure-atomic: on any non-OK status the store is exactly
as it was before the call. That is not incidental -- an attribute store is
mutated inside a transaction that may roll back, and a half-grown store would
survive the rollback and desynchronize from the topology it describes.

Growth is bounded by geometry_limit_policy_t::cBrushSidesPerBrushMax. The bound
is checked before any allocation is attempted, so an unreasonable request fails
predictably instead of succeeding slowly and exhausting memory.

Enumeration is by index over [0, count), which is insertion order and stable
across reads. Indices are positional only: they are not identity and do not
survive removal or reordering. Persistent identity is geometry_source_id_t,
owned by the representation.
================
*/

#ifndef CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_BRUSHSIDESTORE_H
#define CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_BRUSHSIDESTORE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Schema.h"

#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

using common::allocator_t;
using common::usize;

// Non-copyable by construction, because vector_t is. Duplication goes through
// BrushSideAttributeStore_TryCopyFrom, which can report allocation failure --
// an implicit copy constructor could not, and would have to either throw or
// silently produce a truncated store.
struct geometry_brush_side_attribute_store_t {
    common::vector_t<geometry_brush_side_attributes_t> records{};
};

CYPHER_NODISCARD geometry_status_t BrushSideAttributeStore_Init(
    CY_OUT geometry_brush_side_attribute_store_t *pStore,
    const allocator_t *pAllocator ) noexcept;

// Safe to call on a zeroed or already-shut-down store, so an operation that
// fails partway through construction can unwind without tracking how far it got.
void BrushSideAttributeStore_Shutdown(
    CY_INOUT geometry_brush_side_attribute_store_t *pStore ) noexcept;

CYPHER_NODISCARD usize BrushSideAttributeStore_Count(
    const geometry_brush_side_attribute_store_t *pStore ) noexcept;

// Reserves capacity without changing the element count. Bounded by
// limits.cBrushSidesPerBrushMax and failure-atomic.
CYPHER_NODISCARD geometry_status_t BrushSideAttributeStore_TryReserve(
    CY_INOUT geometry_brush_side_attribute_store_t *pStore,
    const geometry_limit_policy_t &limits,
    usize nCapacity ) noexcept;

// Validates the record, then appends it. pIndexOut receives the new index and
// is left untouched on failure. Rejecting an invalid record before it enters
// storage is the point: a store that only holds valid records means every later
// read can skip revalidation.
CYPHER_NODISCARD geometry_status_t BrushSideAttributeStore_TryAppend(
    CY_INOUT geometry_brush_side_attribute_store_t *pStore,
    const geometry_policy_t &policy,
    const geometry_brush_side_attributes_t &attributes,
    CY_OUT_OPTIONAL usize *pIndexOut ) noexcept;

// Appends cCount copies of BrushSideAttributes_MakeDefault(). All-or-nothing:
// the bound is checked and capacity reserved before any record is appended,
// so on failure the store keeps exactly its original records. Used to give a
// freshly generated brush one record per side.
CYPHER_NODISCARD geometry_status_t BrushSideAttributeStore_TryAppendDefaults(
    CY_INOUT geometry_brush_side_attribute_store_t *pStore,
    const geometry_policy_t &policy,
    usize cCount ) noexcept;

CYPHER_NODISCARD geometry_status_t BrushSideAttributeStore_TryGet(
    const geometry_brush_side_attribute_store_t *pStore,
    usize iIndex,
    CY_OUT geometry_brush_side_attributes_t *pAttributesOut ) noexcept;

// Replaces an existing record. The existing value is preserved untouched if the
// replacement does not validate.
CYPHER_NODISCARD geometry_status_t BrushSideAttributeStore_TrySet(
    CY_INOUT geometry_brush_side_attribute_store_t *pStore,
    const geometry_numerical_policy_t &policy,
    usize iIndex,
    const geometry_brush_side_attributes_t &attributes ) noexcept;

// Replaces the destination's contents with a copy of the source. On failure the
// destination keeps its original contents, which is why this cannot simply
// clear and then append.
CYPHER_NODISCARD geometry_status_t BrushSideAttributeStore_TryCopyFrom(
    CY_INOUT geometry_brush_side_attribute_store_t *pDestination,
    const geometry_brush_side_attribute_store_t *pSource,
    const geometry_limit_policy_t &limits ) noexcept;

// Revalidates every stored record. Storage rejects invalid records on the way
// in, so this exists for input that bypassed that path -- deserialization,
// clipboard transfer, or a policy that has since been tightened.
CYPHER_NODISCARD geometry_status_t BrushSideAttributeStore_Validate(
    const geometry_brush_side_attribute_store_t *pStore,
    const geometry_policy_t &policy ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_BRUSHSIDESTORE_H
