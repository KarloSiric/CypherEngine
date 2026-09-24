//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushValue.h
//  Purpose: Declares the immutable, reference-counted committed value of
//           one brush: its planes, side attributes, and derived boundary.
//  Details: A brush value is created once, validated once, and never
//           mutated afterwards. The document publishes a brush by pointing
//           its live table at a value; an edit publishes a NEW value and
//           drops its reference to the old one. Snapshots and undo records
//           hold their own references, so an old value stays alive and
//           byte-for-byte unchanged for as long as anything observes it.
//
//           This copy-on-write shape is what lets a snapshot be an O(n)
//           pointer copy instead of a deep copy of every plane, and what
//           makes "an older snapshot remains unchanged after a commit" a
//           structural guarantee rather than a test-enforced convention.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_VALUE_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_VALUE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_BrushSideStore.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushValidation.h"

#include "CypherCommon_RefCount.h"

namespace cypher::editor::geometry
{

struct geometry_document_t;

// Every field is fixed at creation. Consumers receive `const` pointers
// only; the reference count is the single mutable member and is atomic,
// so a value may be read and released from any thread once published.
//
// Invariants established by BrushValue_TryCreate and never broken:
//   - brush passes BrushValidation_Quick and the full boundary checks
//   - boundary is the canonical reconstruction of brush under the policy
//     of the owning document
//   - attributes holds exactly one record per side, every record is valid,
//     and the sides' iAttributeIndex values are a permutation of
//     [0, side count)
//   - bounds encloses every boundary vertex
struct geometry_brush_value_t {
    mutable common::ref_count_t refs{};

    // Allocator that owns this block and every member's storage. It must
    // be thread-safe if any reference is released off the owner thread.
    const common::allocator_t *pAllocator{ nullptr };

    // Identity-domain token of the document that created the value. Only
    // compared, never dereferenced, so a value may outlive its document.
    const geometry_document_t *pDomain{ nullptr };

    brush_solid_t brush{};
    geometry_brush_side_attribute_store_t attributes{};
    brush_boundary_t boundary{};
    math::aabbd_t bounds{};
};

// Deep-copies the brush and its attribute store into a new value, then
// validates it. On success *ppValueOut receives a value with one reference
// owned by the caller. On any failure *ppValueOut is null and nothing is
// allocated. pValidationOut, when non-null, receives the brush validation
// result (counts and status) whether or not creation succeeds.
//
// Failure statuses:
//   INVALID_ARGUMENT     null inputs, or attribute binding is not a
//                        one-to-one mapping of sides to records
//   NOT_INITIALIZED      brush or attribute store not initialized
//   (validation status)  whatever quick/deep brush validation reported
//   (attribute status)   an attribute record failed validation
//   ALLOCATION_FAILED    any allocation failed
//
// Use GeometryDocument_TryCreateBrushValue rather than calling this
// directly: it supplies the document's allocator, policy, and domain.
CYPHER_NODISCARD geometry_status_t BrushValue_TryCreate(
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    const geometry_document_t *pDomain,
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pAttributes,
    const geometry_brush_value_t **ppValueOut,
    brush_validation_result_t *pValidationOut ) noexcept;

// Adds one reference. Null is ignored.
void BrushValue_AddRef( const geometry_brush_value_t *pValue ) noexcept;

// Drops one reference; the last release destroys the value and frees its
// block through the value's allocator. Null is ignored.
void BrushValue_Release( const geometry_brush_value_t *pValue ) noexcept;

// Current reference count, for diagnostics and tests. Racy by nature when
// other threads hold references.
CYPHER_NODISCARD common::u32 BrushValue_RefCount(
    const geometry_brush_value_t *pValue ) noexcept;

// Validates that sides and attribute records are bound one-to-one: equal
// counts, every iAttributeIndex in range, and no record shared by two sides.
// Exposed so operations can check a candidate brush before paying for a
// full value creation.
CYPHER_NODISCARD geometry_status_t BrushValue_ValidateAttributeBinding(
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pAttributes ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_VALUE_H
