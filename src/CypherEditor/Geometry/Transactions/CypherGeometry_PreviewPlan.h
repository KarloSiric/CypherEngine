//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PreviewPlan.h
//  Purpose: Declares an all-or-nothing batch of preview changes for
//           multi-brush operations.
//  Details: Operations build a plan of replace/insert/remove steps with
//           owned value references, then apply it. If any step is
//           refused, every brush the plan already touched is restored to
//           the preview it had before the plan ran. Restoration only
//           rewrites existing transaction entries and therefore cannot fail
//           for lack of memory, which is what makes the batch atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PREVIEW_PLAN_H
#define CYPHER_EDITOR_GEOMETRY_PREVIEW_PLAN_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Transaction.h"

namespace cypher::editor::geometry
{

enum class geometry_preview_step_kind_t : common::u8 {
    REPLACE = 0u,
    INSERT,
    REMOVE,
    COUNT
};

struct geometry_preview_step_t {
    geometry_preview_step_kind_t kind{ geometry_preview_step_kind_t::REPLACE };
    geometry_source_id_t brushId{};
    const geometry_brush_value_t *pValue{ nullptr }; // owned; null for REMOVE
};

struct geometry_preview_plan_t {
    geometry_preview_plan_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( geometry_preview_plan_t );
    ~geometry_preview_plan_t() noexcept;

    common::vector_t<geometry_preview_step_t> steps{};
};

CYPHER_NODISCARD geometry_status_t PreviewPlan_Init(
    geometry_preview_plan_t *pPlan, const common::allocator_t *pAllocator ) noexcept;

// Releases every owned value. Safe when uninitialized.
void PreviewPlan_Shutdown( geometry_preview_plan_t *pPlan ) noexcept;

// Adds a step. For REPLACE and INSERT the plan takes ownership of the
// caller's reference to pValue on success only; on failure the caller
// still owns it.
CYPHER_NODISCARD geometry_status_t PreviewPlan_TryAddValue(
    geometry_preview_plan_t *pPlan,
    geometry_preview_step_kind_t kind,
    const geometry_brush_value_t *pValue ) noexcept;

CYPHER_NODISCARD geometry_status_t PreviewPlan_TryAddRemove(
    geometry_preview_plan_t *pPlan, geometry_source_id_t brushId ) noexcept;

// Applies every step in order; all or nothing.
CYPHER_NODISCARD geometry_status_t PreviewPlan_TryApply(
    geometry_preview_plan_t *pPlan, geometry_transaction_t *pTransaction ) noexcept;

// Pins the current preview values of the given brushes (one reference per
// entry, parallel to ids). INVALID_HANDLE when any brush is missing from
// the preview. The caller releases the references with ReleasePinned.
CYPHER_NODISCARD geometry_status_t PreviewPlan_TryPin(
    const geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    common::vector_t<const geometry_brush_value_t *> *pPinnedOut ) noexcept;

void PreviewPlan_ReleasePinned( common::vector_t<const geometry_brush_value_t *> *pPinned ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PREVIEW_PLAN_H
