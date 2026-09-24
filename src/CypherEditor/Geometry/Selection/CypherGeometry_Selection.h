//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Selection.h
//  Purpose: Declares persistent geometry-component references and sorted
//           selection sets with pruning and explicit remapping.
//  Details: A component reference names geometry by persistent source IDs
//           only, never by boundary index:
//
//             BRUSH         brushId
//             BRUSH_SIDE    brushId + side ID (a)
//             BRUSH_EDGE    brushId + the two side IDs whose faces meet
//                           along the edge (a < b)
//             BRUSH_VERTEX  brushId + the three lowest side IDs whose
//                           faces meet at the vertex (a < b < c)
//
//           Because derived edges and vertices are named by the sides that
//           create them, a reference survives boundary reconstruction,
//           plane drags that keep the same adjacency, undo, and
//           serialization. A drag that changes adjacency makes the
//           reference unresolvable, and Prune reports it rather than
//           silently re-targeting it.
//
//           Sets are sorted and unique by (kind, brush, a, b, c), so set
//           operations and iteration order are deterministic. Global
//           selection across entities and other scene objects stays in the
//           host.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SELECTION_H
#define CYPHER_EDITOR_GEOMETRY_SELECTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Snapshot.h"

namespace cypher::editor::geometry
{

enum class geometry_component_kind_t : common::u8 {
    INVALID = 0u,
    BRUSH,
    BRUSH_SIDE,
    BRUSH_EDGE,
    BRUSH_VERTEX,
    COUNT
};

struct geometry_component_ref_t {
    geometry_component_kind_t kind{ geometry_component_kind_t::INVALID };
    geometry_source_id_t brushId{};
    geometry_source_id_t a{};
    geometry_source_id_t b{};
    geometry_source_id_t c{};
};

// Strict total order used by selection sets.
CYPHER_NODISCARD common::bool_t ComponentRef_Less(
    const geometry_component_ref_t &left, const geometry_component_ref_t &right ) noexcept;

CYPHER_NODISCARD common::bool_t ComponentRef_Equals(
    const geometry_component_ref_t &left, const geometry_component_ref_t &right ) noexcept;

// Structural validity: kind in range, brush ID valid, the kind's side IDs
// valid and strictly ascending, unused slots zero.
CYPHER_NODISCARD common::bool_t ComponentRef_IsWellFormed(
    const geometry_component_ref_t &ref ) noexcept;

CYPHER_NODISCARD geometry_component_ref_t ComponentRef_Brush( geometry_source_id_t brushId ) noexcept;
CYPHER_NODISCARD geometry_component_ref_t ComponentRef_Side(
    geometry_source_id_t brushId, geometry_source_id_t sideId ) noexcept;

// Names boundary edge iEdge / vertex iVertex of a value by side identity.
// CORRUPT_STATE when the boundary topology is inconsistent (an edge not
// bordered by exactly two faces, a vertex with fewer than three).
CYPHER_NODISCARD geometry_status_t ComponentRef_TryFromEdge(
    const geometry_brush_value_t *pValue, common::usize iEdge,
    geometry_component_ref_t *pRefOut ) noexcept;
CYPHER_NODISCARD geometry_status_t ComponentRef_TryFromVertex(
    const geometry_brush_value_t *pValue, common::usize iVertex,
    geometry_component_ref_t *pRefOut ) noexcept;

// Resolves a reference against a value: side index for sides, boundary
// edge/vertex index for edges/vertices (0 for brushes). INVALID_ARGUMENT
// when the value is not the referenced brush or the component no longer
// exists.
CYPHER_NODISCARD geometry_status_t ComponentRef_TryResolve(
    const geometry_brush_value_t *pValue, const geometry_component_ref_t &ref,
    common::usize *pIndexOut ) noexcept;

// World-space position summarizing the component: brush and side use the
// bounds/face centroid, edges their midpoint, vertices the vertex.
CYPHER_NODISCARD geometry_status_t ComponentRef_TryCenter(
    const geometry_brush_value_t *pValue, const geometry_component_ref_t &ref,
    math::vec3d_t *pCenterOut ) noexcept;

// ---------------------------------------------------------------------------
// Sets
// ---------------------------------------------------------------------------

struct geometry_selection_t {
    common::vector_t<geometry_component_ref_t> items{};
};

CYPHER_NODISCARD geometry_status_t Selection_Init(
    geometry_selection_t *pSelection, const common::allocator_t *pAllocator ) noexcept;
void Selection_Shutdown( geometry_selection_t *pSelection ) noexcept;
void Selection_Clear( geometry_selection_t *pSelection ) noexcept;
CYPHER_NODISCARD common::usize Selection_Count( const geometry_selection_t *pSelection ) noexcept;
CYPHER_NODISCARD geometry_status_t Selection_TryGet(
    const geometry_selection_t *pSelection, common::usize iIndex,
    geometry_component_ref_t *pRefOut ) noexcept;
CYPHER_NODISCARD common::bool_t Selection_Contains(
    const geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept;

// Insert keeps the set sorted; adding a present item is OK and a no-op.
// Malformed references are INVALID_ARGUMENT. Failure-atomic.
CYPHER_NODISCARD geometry_status_t Selection_TryAdd(
    geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept;
// Removing an absent item is OK and a no-op.
CYPHER_NODISCARD geometry_status_t Selection_TryRemove(
    geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept;
CYPHER_NODISCARD geometry_status_t Selection_TryToggle(
    geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept;

// Replaces every component with the brush it belongs to.
CYPHER_NODISCARD geometry_status_t Selection_TryPromoteToBrushes(
    geometry_selection_t *pSelection ) noexcept;

// Adds every side / edge / vertex of the brush as it exists in pValue.
CYPHER_NODISCARD geometry_status_t Selection_TryAddAllComponents(
    geometry_selection_t *pSelection, const geometry_brush_value_t *pValue,
    geometry_component_kind_t kind ) noexcept;

// Drops every reference that no longer resolves in the snapshot.
// *pDroppedOut (optional) counts them.
CYPHER_NODISCARD geometry_status_t Selection_TryPrune(
    geometry_selection_t *pSelection, const geometry_document_snapshot_t *pSnapshot,
    common::usize *pDroppedOut ) noexcept;

// Bounds of every selected component's center (and every selected brush's
// bounds) in the snapshot. DEGENERATE when nothing resolves.
CYPHER_NODISCARD geometry_status_t Selection_TryBounds(
    const geometry_selection_t *pSelection, const geometry_document_snapshot_t *pSnapshot,
    math::aabbd_t *pBoundsOut ) noexcept;

// One brush-level remap: source brush -> destination brushes (for example
// the fragments of a CSG subtraction). Zero destinations drops the source.
struct geometry_selection_remap_t {
    geometry_source_id_t source{};
    const geometry_source_id_t *pDestinations{ nullptr };
    common::u32 cDestinations{ 0u };
};

struct geometry_selection_remap_report_t {
    common::u32 cDropped{ 0u };    // references whose brush had no destination
    common::u32 cAmbiguous{ 0u };  // component references whose brush split
    common::u32 cRemapped{ 0u };   // references moved to a new brush
};

// Applies brush remaps. BRUSH references expand to every destination.
// Component references follow a single destination; when their brush
// split into several, they are replaced by BRUSH references to all
// destinations and counted as ambiguous, because which fragment now owns a
// side cannot be decided from brush-level provenance alone. Failure-atomic.
CYPHER_NODISCARD geometry_status_t Selection_TryRemap(
    geometry_selection_t *pSelection,
    common::span_t<const geometry_selection_remap_t> remaps,
    geometry_selection_remap_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SELECTION_H
