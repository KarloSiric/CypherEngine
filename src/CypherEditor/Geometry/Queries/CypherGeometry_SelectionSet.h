//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SelectionSet.h
//  Purpose: Declares typed geometry component selection sets.
//  Details: A selection set tracks a flat, unordered collection of
//           geometry source IDs representing selected components. The
//           same data structure serves brush, side, edge, and vertex
//           selection — the selection level is a tag, not a type
//           parameter, so one set can be passed through generic editing
//           paths without templates.
//
//           Selection sets use source IDs (not handles or indices) so
//           selections survive undo, redo, and document mutations that
//           reallocate storage.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SELECTION_SET_H
#define CYPHER_EDITOR_GEOMETRY_SELECTION_SET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Which component domain the selection operates on. The level determines
// how pick results and editing operations interpret the selected IDs.
enum class geometry_selection_level_t : common::u8 {
    INVALID = 0u,
    BRUSH   = 1u,
    SIDE    = 2u,
    EDGE    = 3u,
    VERTEX  = 4u,
    COUNT   = 5u
};

// An unordered set of geometry source IDs at one selection level.
// Duplicate IDs are rejected on insertion. Iteration order is not
// guaranteed to be stable across add/remove operations.
struct geometry_selection_set_t {
    common::vector_t<geometry_source_id_t> ids{};
    geometry_selection_level_t level{ geometry_selection_level_t::INVALID };
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CYPHER_NODISCARD geometry_status_t GeometrySelectionSet_Init(
    geometry_selection_set_t *pSet,
    const common::allocator_t *pAllocator,
    geometry_selection_level_t level ) noexcept;

void GeometrySelectionSet_Shutdown(
    geometry_selection_set_t *pSet ) noexcept;

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

// Adds an ID to the set. Returns IDENTITY_CONFLICT if already present.
CYPHER_NODISCARD geometry_status_t GeometrySelectionSet_TryAdd(
    geometry_selection_set_t *pSet,
    geometry_source_id_t id ) noexcept;

// Removes an ID from the set. Returns INVALID_ARGUMENT if not present.
CYPHER_NODISCARD geometry_status_t GeometrySelectionSet_TryRemove(
    geometry_selection_set_t *pSet,
    geometry_source_id_t id ) noexcept;

// Adds the ID if absent, removes it if present. Reports the resulting
// membership in *pIsSelectedOut (if non-null). The output is set to false
// before validation so failures never expose a stale caller value.
CYPHER_NODISCARD geometry_status_t GeometrySelectionSet_Toggle(
    geometry_selection_set_t *pSet,
    geometry_source_id_t id,
    bool *pIsSelectedOut ) noexcept;

// Removes all IDs without releasing the allocation.
void GeometrySelectionSet_Clear(
    geometry_selection_set_t *pSet ) noexcept;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize GeometrySelectionSet_Count(
    const geometry_selection_set_t *pSet ) noexcept;

CYPHER_NODISCARD bool GeometrySelectionSet_Contains(
    const geometry_selection_set_t *pSet,
    geometry_source_id_t id ) noexcept;

CYPHER_NODISCARD bool GeometrySelectionSet_IsEmpty(
    const geometry_selection_set_t *pSet ) noexcept;

CYPHER_NODISCARD geometry_selection_level_t GeometrySelectionSet_GetLevel(
    const geometry_selection_set_t *pSet ) noexcept;

// Copies all selected IDs into the output array. Reports actual count
// in *pCountOut. Passing pIdsOut == nullptr with nCapacity == 0 is a valid
// count-only query. Returns INSUFFICIENT_CAPACITY if the output is too small.
CYPHER_NODISCARD geometry_status_t GeometrySelectionSet_TryGetAll(
    const geometry_selection_set_t *pSet,
    CY_OUT_WRITES( nCapacity ) geometry_source_id_t *pIdsOut,
    common::usize nCapacity,
    common::usize *pCountOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SELECTION_SET_H
