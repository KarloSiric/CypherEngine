//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSource.h
//  Purpose: Declares the document-level authored brush ownership unit.
//  Details: A BrushSolid stores stable indices into a side-attribute table,
//           so publishing only the solid loses the material and UV meaning
//           of those indices. brush_source_t owns both pieces together and
//           provides failure-atomic construction, cloning, validation, and
//           exact comparison. The geometry document will adopt this unit in
//           a later migration; this file establishes the ownership boundary
//           without changing the current document ABI.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_SOURCE_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_SOURCE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_BrushSideStore.h"
#include "CypherGeometry_BrushSolid.h"

namespace cypher::editor::geometry
{

// Canonical authored brush state. Every side in solid addresses exactly one
// record in attributes through brush_solid_side_t::iAttributeIndex. Several
// sides may intentionally share one record; indices are local to this source.
struct brush_source_t {
    brush_solid_t solid{};
    geometry_brush_side_attribute_store_t attributes{};
};

enum class brush_source_fault_t : common::u8 {
    NONE = 0u,
    NOT_INITIALIZED,
    INVALID_STORAGE,
    INVALID_SOLID,
    INVALID_ATTRIBUTES,
    DANGLING_ATTRIBUTE_INDEX
};

// `status` preserves the lower-level reason for INVALID_SOLID or
// INVALID_ATTRIBUTES. `iSide` identifies a dangling side binding and is
// CY_USIZE_MAX for faults that do not address one side.
struct brush_source_validation_t {
    brush_source_fault_t fault{ brush_source_fault_t::NONE };
    geometry_status_t status{ geometry_status_t::OK };
    common::usize iSide{ common::CY_USIZE_MAX };
};

// Builds an owned source from a legacy solid that has no accompanying
// attribute store. One usable default surface record is created for every
// destination side, preserving independent face editing from the first
// authored operation. The input solid is unchanged.
// pOut must be canonical empty; failure leaves it canonical empty.
CYPHER_NODISCARD geometry_status_t BrushSource_TryBuildDefault(
    const brush_solid_t *pSolid,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    brush_source_t *pOut ) noexcept;

// Builds an owned source by copying a solid and its complete attribute store.
// All side indices must resolve in pAttributes. Shared and unused records are
// allowed because positional sharing is an authored choice and removal is a
// separate compaction operation. pOut must be canonical empty; failure leaves
// it canonical empty.
CYPHER_NODISCARD geometry_status_t BrushSource_TryBuildWithStore(
    const brush_solid_t *pSolid,
    const geometry_brush_side_attribute_store_t *pAttributes,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    brush_source_t *pOut ) noexcept;

// Deep copy preserving brush/side identity, side order, attribute order, and
// every side-to-record binding. pOut must be canonical empty.
CYPHER_NODISCARD geometry_status_t BrushSource_TryClone(
    const brush_source_t *pSource,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    brush_source_t *pOut ) noexcept;

// Safe for zeroed, partially initialized, and already-shut-down sources.
void BrushSource_Shutdown( brush_source_t *pSource ) noexcept;

// True only when both owned members are initialized through one allocator.
CYPHER_NODISCARD bool BrushSource_IsInitialized(
    const brush_source_t *pSource ) noexcept;

// Validates storage ownership, the BrushSolid plane/identity contract, every
// attribute record, and every side-to-record binding. This does not rebuild
// the derived boundary; callers that need a watertightness proof additionally
// run BrushValidation_Deep.
CYPHER_NODISCARD brush_source_validation_t BrushSource_Validate(
    const brush_source_t *pSource,
    const geometry_policy_t &policy ) noexcept;

// Exact authored-state comparison. Allocation identity and capacity do not
// participate; planes, source IDs, bindings, and attribute values do.
CYPHER_NODISCARD bool BrushSource_Equal(
    const brush_source_t *pA,
    const brush_source_t *pB ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_SOURCE_H
