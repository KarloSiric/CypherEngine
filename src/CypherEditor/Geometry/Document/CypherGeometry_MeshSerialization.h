//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSerialization.h
//  Purpose: Declares the mesh section of the geometry document format
//           (schema version 3): writing document meshes into, and reading
//           them back from, the CYKV tree the document serializer owns.
//  Details: Each mesh is written as its canonical description in flat
//           per-channel arrays rather than one object per vertex/face:
//
//             source_id           u64
//             vertex_ids          [u64]            one per vertex
//             positions           [f64]            3 per vertex
//             face_ids            [u64]            one per face
//             face_corner_counts  [u64]            one per face
//             corners             [u64]            vertex index per corner
//             face_materials      [u64]  optional  one per face
//             face_smoothing      [u64]  optional  one per face
//             corner_uv0          [f64]  optional  2 per corner
//             corner_uv1          [f64]  optional  2 per corner
//             corner_colors       [u64]  optional  one per corner
//             edge_vertices       [u64]  optional  2 per edge entry
//             edge_flags          [u64]  optional  one per edge entry
//             edge_creases        [f64]  optional  one per edge entry
//
//           Flat arrays keep the CYKV node count close to the scalar count
//           (no per-element object overhead), which matters because node
//           budgets bound parsing. Optional channels are written only when
//           some element differs from its default, so a plain mesh stays
//           small; a missing channel reads back as all defaults. Because
//           the description is canonical, saving the same document always
//           produces the same text.
//
//           Reading builds each mesh through MeshSource_TryBuild (the one
//           validated construction path) and adds it with the document's
//           atomic mesh step, so a loaded mesh obeys exactly the same rules
//           as an edited one.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SERIALIZATION_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SERIALIZATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_DocumentMeshes.h"

namespace cypher::editor::geometry
{

// First schema version that carries a "meshes" array.
inline constexpr common::u32 GEOMETRY_SCHEMA_VERSION_MESHES = 3u;

inline constexpr common::usize GEOMETRY_SERIALIZATION_MAX_MESHES = 4096u;

// Extra CYKV node budget reserved for mesh data when parsing. One node per
// scalar plus small per-mesh overhead; the 32 MiB text cap bounds real
// documents well below this.
inline constexpr common::usize GEOMETRY_SERIALIZATION_MAX_MESH_NODES = 16u * 1024u * 1024u;

// Writes every document mesh (document order) into a "meshes" array on
// pRoot. Returns OK, OUT_OF_MEMORY, or CORRUPT_DOCUMENT (a mesh that cannot
// be described).
CYPHER_NODISCARD geometry_serialization_result_t MeshSerialization_TryWriteMeshes(
    common::key_value_document_t *pKvDocument,
    common::key_value_t *pRoot,
    const geometry_document_t *pDocument,
    const common::allocator_t *pScratchAllocator ) noexcept;

// Scans the serialized mesh channel layout and rejects document-wide mesh
// budgets that are already provably exceeded, without allocating or mutating
// a destination document. Vertex, half-edge, loop, and face counts are exact.
// Edge and shell checks use safe lower bounds because those exact counts require
// topology reconstruction.
CYPHER_NODISCARD geometry_serialization_result_t
MeshSerialization_TryPreflightReadBudgets(
    const common::key_value_t *pRoot,
    const geometry_policy_t &policy ) noexcept;

// Reads the "meshes" array (required) and adds every mesh to *pDocument.
// With bRequireClaimed, every mesh ID must already be claimed in the
// document registry (true for files that persist identity state). Aggregate
// budgets detectable from the wire layout fail before the first mesh is
// allocated or published.
CYPHER_NODISCARD geometry_serialization_result_t MeshSerialization_TryReadMeshes(
    const common::key_value_t *pRoot,
    geometry_document_t *pDocument,
    bool bRequireClaimed ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SERIALIZATION_H
