//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSelection.h
//  Purpose: Declares component selection on mesh sources (vertices, edges,
//           faces by persistent ID), topological selection tools (grow,
//           shrink, connected, edge loop/ring, mode conversion), and remap
//           of a selection across an edit with explicit ambiguity reporting.
//  Details: A selection stores source IDs only - never pool handles - so it
//           survives undo, redo, save/load, and snapshot rebuilds untouched.
//           Edges are vertex-ID pairs (MeshSource edge identity), stored
//           with the smaller ID first. All sets are kept sorted and unique,
//           which makes every operation deterministic and every comparison
//           a plain array compare.
//
//           Remap: most selections need no remap at all because IDs
//           persist through edits. What changes is lineage - an edit can
//           split a selected face into several, split a selected edge, or
//           merge a selected vertex into another. Given the edit's lineage
//           (converted from the handle-based provenance once new elements
//           have IDs), Remap:
//             faces:    keeps live faces; adds children of selected faces
//                       (SPLIT); drops dead faces without children (LOST);
//             edges:    keeps live edges; replaces a split edge by its
//                       halves (SPLIT); follows vertex merges (MERGED);
//                       otherwise LOST;
//             vertices: keeps live vertices; follows merges to the survivor
//                       (MERGED); otherwise LOST.
//           The report lists every non-trivial outcome so a tool can tell
//           the user what happened instead of silently changing the set.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SELECTION_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SELECTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshAttributeTransfer.h"

namespace cypher::editor::geometry
{

struct mesh_edge_ref_t {
    geometry_source_id_t a{}; // a.value < b.value
    geometry_source_id_t b{};
};

CYPHER_NODISCARD mesh_edge_ref_t MeshEdgeRef_Make( geometry_source_id_t x, geometry_source_id_t y ) noexcept;

// ---------------------------------------------------------------------------
// Lineage (ID-based provenance of one edit)
// ---------------------------------------------------------------------------

struct mesh_lineage_face_t {
    geometry_source_id_t childId{};
    geometry_source_id_t parentId{};
};

struct mesh_lineage_edge_t {
    mesh_edge_ref_t child{};
    mesh_edge_ref_t parent{};
};

struct mesh_lineage_vertex_t {
    geometry_source_id_t removedId{};
    geometry_source_id_t survivorId{};
};

struct mesh_edit_lineage_t {
    common::vector_t<mesh_lineage_face_t> faces{};
    common::vector_t<mesh_lineage_edge_t> edges{};
    common::vector_t<mesh_lineage_vertex_t> merges{};
};

CYPHER_NODISCARD geometry_status_t MeshEditLineage_Init(
    mesh_edit_lineage_t *pLineage,
    const common::allocator_t *pAllocator ) noexcept;

void MeshEditLineage_Shutdown( mesh_edit_lineage_t *pLineage ) noexcept;

// Converts handle-based provenance to IDs using *pSource, the edited source
// after its new elements received IDs (MeshSource_TryAssignMissingIds or a
// transaction commit's assignment). INVALID_ARGUMENT if a referenced element
// has no ID yet. The lineage is replaced, not appended.
CYPHER_NODISCARD geometry_status_t MeshEditProvenance_TryToLineage(
    const mesh_edit_provenance_t *pProvenance,
    const mesh_source_t *pSource,
    mesh_edit_lineage_t *pLineage ) noexcept;

// ---------------------------------------------------------------------------
// Selection sets
// ---------------------------------------------------------------------------

enum class mesh_selection_mode_t : common::u8 {
    VERTEX = 0u,
    EDGE   = 1u,
    FACE   = 2u
};

struct mesh_selection_t {
    geometry_source_id_t meshId{};
    common::vector_t<geometry_source_id_t> vertices{};
    common::vector_t<mesh_edge_ref_t> edges{};
    common::vector_t<geometry_source_id_t> faces{};
};

CYPHER_NODISCARD geometry_status_t MeshSelection_Init(
    mesh_selection_t *pSelection,
    const common::allocator_t *pAllocator,
    geometry_source_id_t meshId ) noexcept;

void MeshSelection_Shutdown( mesh_selection_t *pSelection ) noexcept;

void MeshSelection_Clear( mesh_selection_t *pSelection ) noexcept;

CYPHER_NODISCARD geometry_status_t MeshSelection_TryAddVertex( mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept;
CYPHER_NODISCARD geometry_status_t MeshSelection_TryAddFace( mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept;
CYPHER_NODISCARD geometry_status_t MeshSelection_TryAddEdge( mesh_selection_t *pSelection, mesh_edge_ref_t edge ) noexcept;
bool MeshSelection_RemoveVertex( mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept;
bool MeshSelection_RemoveFace( mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept;
bool MeshSelection_RemoveEdge( mesh_selection_t *pSelection, mesh_edge_ref_t edge ) noexcept;
CYPHER_NODISCARD bool MeshSelection_HasVertex( const mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept;
CYPHER_NODISCARD bool MeshSelection_HasFace( const mesh_selection_t *pSelection, geometry_source_id_t id ) noexcept;
CYPHER_NODISCARD bool MeshSelection_HasEdge( const mesh_selection_t *pSelection, mesh_edge_ref_t edge ) noexcept;

// ---------------------------------------------------------------------------
// Topological tools (all need the mesh the selection refers to)
// ---------------------------------------------------------------------------

// One ring outward: faces sharing an edge with a selected face; vertices
// joined by an edge to a selected vertex; edges sharing a vertex with a
// selected edge.
CYPHER_NODISCARD geometry_status_t MeshSelection_TryGrow(
    mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_selection_mode_t mode ) noexcept;

// Inverse of grow: removes selected elements with an unselected neighbour
// (for faces: across an edge; a face on the mesh boundary counts as having
// an unselected neighbour).
CYPHER_NODISCARD geometry_status_t MeshSelection_TryShrink(
    mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_selection_mode_t mode ) noexcept;

// Adds everything connected to the current selection in that mode (faces
// through shared edges, vertices and edges through shared vertices).
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectConnected(
    mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_selection_mode_t mode ) noexcept;

// Adds the edge loop / edge ring through edge (a, b).
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectEdgeLoop(
    mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_edge_ref_t edge ) noexcept;
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectEdgeRing(
    mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_edge_ref_t edge ) noexcept;

// Replaces the `to` set from the `from` set: faces -> their vertices /
// edges; edges -> their vertices; vertices -> edges with both ends
// selected; vertices or edges -> faces fully covered by them.
CYPHER_NODISCARD geometry_status_t MeshSelection_TryConvert(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    mesh_selection_mode_t from,
    mesh_selection_mode_t to ) noexcept;

// Removes references that no longer name live elements of pMesh; returns
// how many were removed in *pRemovedOut.
CYPHER_NODISCARD geometry_status_t MeshSelection_TryPrune(
    mesh_selection_t *pSelection, const mesh_source_t *pMesh, common::u32 *pRemovedOut ) noexcept;

// ---------------------------------------------------------------------------
// Remap across an edit
// ---------------------------------------------------------------------------

enum class mesh_selection_outcome_t : common::u8 {
    SPLIT  = 1u, // a selected element became several; all are selected now
    MERGED = 2u, // a selected element merged into another, now selected
    LOST   = 3u  // a selected element vanished without a successor
};

struct mesh_selection_remap_entry_t {
    mesh_selection_outcome_t outcome{ mesh_selection_outcome_t::LOST };
    mesh_selection_mode_t mode{ mesh_selection_mode_t::FACE };
    geometry_source_id_t id{};  // vertex/face ID, or edge endpoint a
    geometry_source_id_t id2{}; // edge endpoint b
};

struct mesh_selection_remap_report_t {
    common::u32 cKept{ 0u };
    common::u32 cSplit{ 0u };
    common::u32 cMerged{ 0u };
    common::u32 cLost{ 0u };
    common::vector_t<mesh_selection_remap_entry_t> entries{}; // every non-kept outcome
};

CYPHER_NODISCARD geometry_status_t MeshSelectionRemapReport_Init(
    mesh_selection_remap_report_t *pReport, const common::allocator_t *pAllocator ) noexcept;
void MeshSelectionRemapReport_Shutdown( mesh_selection_remap_report_t *pReport ) noexcept;

// True when the remap did anything but keep elements as they were.
CYPHER_NODISCARD bool MeshSelectionRemapReport_IsAmbiguous( const mesh_selection_remap_report_t *pReport ) noexcept;

// Remaps the selection onto pMesh (post-edit, IDs assigned) using the
// edit's lineage. The report (optional, initialized) is replaced.
CYPHER_NODISCARD geometry_status_t MeshSelection_TryRemap(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    const mesh_edit_lineage_t *pLineage,
    mesh_selection_remap_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SELECTION_H
