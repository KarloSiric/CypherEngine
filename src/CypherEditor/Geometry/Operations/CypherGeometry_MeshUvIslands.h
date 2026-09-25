//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshUvIslands.h
//  Purpose: Declares source-ID-addressed UV seam editing and deterministic
//           UV-island discovery for editable mesh sources.
//  Details: A UV island is a face-connected component across interior,
//           manifold, non-seam edges whose two endpoint corner UVs agree
//           within the caller's tolerance. Boundary and non-manifold edges
//           always stop traversal. Results are canonical: islands are
//           ordered by their lowest face source ID and every island's face
//           IDs are ascending.
//
//           This is the connectivity foundation for a future UV editor. It
//           deliberately does not unwrap, parameterize, relax, pack, or
//           change texel density.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_UV_ISLANDS_H
#define CYPHER_EDITOR_GEOMETRY_MESH_UV_ISLANDS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSource.h"
#include "CypherGeometry_MeshSurfacing.h"

namespace cypher::editor::geometry
{

// Persistent edge address. Orientation is irrelevant: (a,b) and (b,a)
// name the same edge.
struct mesh_source_edge_key_t {
    geometry_source_id_t vertexA{};
    geometry_source_id_t vertexB{};
};

enum class mesh_uv_seam_edit_t : common::u8 {
    SET = 0u,
    CLEAR,
    TOGGLE
};

// Applies one seam operation to the complete edge set. Every edge and every
// source ID is resolved before the first write; duplicate keys (including
// reversed duplicates) are rejected. Consequently a bad/stale ID or an
// allocation failure leaves every edge unchanged. Other edge flags and
// crease weights are preserved. Empty input is a successful no-op.
CYPHER_NODISCARD geometry_status_t MeshUv_TryEditSeams(
    mesh_source_t *pSource,
    common::span_t<const mesh_source_edge_key_t> edges,
    mesh_uv_seam_edit_t edit,
    common::u32 *pcChangedOut = nullptr ) noexcept;

struct mesh_uv_island_range_t {
    common::u32 iFirstFace{ 0u };
    common::u32 cFaces{ 0u };
};

// faceIds stores the canonical, contiguous membership runs described by
// islands. A face appears exactly once.
struct mesh_uv_island_result_t {
    common::vector_t<geometry_source_id_t> faceIds{};
    common::vector_t<mesh_uv_island_range_t> islands{};
};

CYPHER_NODISCARD geometry_status_t MeshUvIslandResult_Init(
    mesh_uv_island_result_t *pResult,
    const common::allocator_t *pAllocator ) noexcept;

void MeshUvIslandResult_Shutdown(
    mesh_uv_island_result_t *pResult ) noexcept;

CYPHER_NODISCARD bool MeshUvIslandResult_IsInitialized(
    const mesh_uv_island_result_t *pResult ) noexcept;

// Replaces an initialized result only after discovery succeeds completely.
// On any failure (including scratch/output allocation failure), the previous
// result remains byte-for-byte unchanged. uvSet selects UV0/MATERIAL or
// UV1/LIGHTMAP. tolerance must be finite and non-negative and is applied to
// each UV coordinate at both oriented edge endpoints.
CYPHER_NODISCARD geometry_status_t MeshUv_TryDiscoverIslands(
    const mesh_source_t *pSource,
    mesh_uv_set_t uvSet,
    common::f64 tolerance,
    mesh_uv_island_result_t *pResult ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_UV_ISLANDS_H
