//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Snapshot.h
//  Purpose: Declares immutable deep-frozen geometry document snapshots.
//  Details: A snapshot captures the complete brush pool and revision at the
//           moment it was taken. Once created, the snapshot's contents never
//           change — subsequent document mutations do not affect it. This
//           gives undo, Cook, and preview baselines a stable reference point
//           without locking the mutable document.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SNAPSHOT_H
#define CYPHER_EDITOR_GEOMETRY_SNAPSHOT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"

namespace cypher::editor::geometry
{

// Immutable snapshot of a geometry document's committed state.
//
// The snapshot owns deep copies of every brush in the document at the time
// it was taken. Like the document, brushes are individually heap-allocated
// and stored as pointers. Numerical/complexity policy is copied with the
// geometry so background consumers never combine a frozen brush set with a
// later document policy. The revision records which committed revision
// produced this state.
struct geometry_snapshot_t {
    common::vector_t<brush_solid_t *> brushes{};
    // Deep copies of the document's mesh sources at snapshot time. Later
    // document edits replace the document's mesh objects, never these.
    common::vector_t<mesh_source_t *> meshes{};
    // Deep copies of the brushes' surface records, parallel to `brushes`
    // (see DocumentBrushAttributes.h), so a snapshot cooks and saves with
    // the materials and UVs it was taken with.
    common::vector_t<geometry_brush_side_attribute_store_t *> brushAttributes{};
    // Deep copies of the document's patches and heightfields.
    common::vector_t<patch_surface_t *> patches{};
    common::vector_t<heightfield_t *> heightFields{};

    geometry_policy_t policy{};
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };

    const common::allocator_t *pAllocator{ nullptr };
};

CYPHER_NODISCARD geometry_status_t GeometrySnapshot_TakeFromDocument(
    geometry_snapshot_t *pSnapshot,
    const geometry_document_t *pDocument ) noexcept;

void GeometrySnapshot_Shutdown(
    geometry_snapshot_t *pSnapshot ) noexcept;

// Surface records of a snapshot brush (by source ID, or by snapshot
// position), or nullptr.
CYPHER_NODISCARD const geometry_brush_side_attribute_store_t *GeometrySnapshot_FindBrushAttributes(
    const geometry_snapshot_t *pSnapshot,
    geometry_source_id_t brushId ) noexcept;
CYPHER_NODISCARD const geometry_brush_side_attribute_store_t *GeometrySnapshot_BrushAttributesAt(
    const geometry_snapshot_t *pSnapshot,
    common::usize iBrush ) noexcept;

// Snapshot patches and heightfields, by position or root source ID.
CYPHER_NODISCARD common::usize GeometrySnapshot_PatchCount( const geometry_snapshot_t *pSnapshot ) noexcept;
CYPHER_NODISCARD const patch_surface_t *GeometrySnapshot_PatchAt( const geometry_snapshot_t *pSnapshot, common::usize i ) noexcept;
CYPHER_NODISCARD common::usize GeometrySnapshot_HeightFieldCount( const geometry_snapshot_t *pSnapshot ) noexcept;
CYPHER_NODISCARD const heightfield_t *GeometrySnapshot_HeightFieldAt( const geometry_snapshot_t *pSnapshot, common::usize i ) noexcept;

CYPHER_NODISCARD bool GeometrySnapshot_IsInitialized(
    const geometry_snapshot_t *pSnapshot ) noexcept;

CYPHER_NODISCARD geometry_revision_t GeometrySnapshot_GetRevision(
    const geometry_snapshot_t *pSnapshot ) noexcept;

CYPHER_NODISCARD const geometry_policy_t *GeometrySnapshot_GetPolicy(
    const geometry_snapshot_t *pSnapshot ) noexcept;

CYPHER_NODISCARD common::usize GeometrySnapshot_BrushCount(
    const geometry_snapshot_t *pSnapshot ) noexcept;

CYPHER_NODISCARD const brush_solid_t *GeometrySnapshot_FindBrush(
    const geometry_snapshot_t *pSnapshot,
    geometry_source_id_t brushId ) noexcept;

CYPHER_NODISCARD common::usize GeometrySnapshot_MeshCount(
    const geometry_snapshot_t *pSnapshot ) noexcept;

// Mesh at position iMesh (document order at snapshot time).
CYPHER_NODISCARD const mesh_source_t *GeometrySnapshot_MeshAt(
    const geometry_snapshot_t *pSnapshot,
    common::usize iMesh ) noexcept;

CYPHER_NODISCARD const mesh_source_t *GeometrySnapshot_FindMesh(
    const geometry_snapshot_t *pSnapshot,
    geometry_source_id_t meshId ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SNAPSHOT_H
