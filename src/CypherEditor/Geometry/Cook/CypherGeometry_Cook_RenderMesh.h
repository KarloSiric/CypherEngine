//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Cook_RenderMesh.h
//  Purpose: Declares the render-mesh cook product for EditableMesh: seam-
//           expanded float vertices, indices, tangents, material batches,
//           bounds, triangle -> face source map, and a content hash.
//  Details: Cook contract (ARCHITECTURE.md): immutable, disposable,
//           reproducible from source, float conversion only here and only
//           checked. Pipeline:
//
//             tessellate (MeshTessellation, corner-aware)
//               -> split normals (MeshNormals: smoothing groups, hard edges)
//               -> one render vertex per distinct (mesh vertex, smooth fan,
//                  uv0, uv1, colour) — the seam expansion
//               -> per-vertex tangents from UV0 derivatives
//               -> triangles stably sorted by material into batches
//               -> bounds and content hash
//
//           Tangents follow the classic per-triangle UV-derivative
//           accumulation with Gram-Schmidt against the normal and a
//           handedness sign in w. They are deterministic but NOT bit-equal
//           to MikkTSpace; a baker that requires MikkTSpace must use it on
//           the cooked triangles (the tangent basis is a Cook/policy choice,
//           recorded in the header).
//
//           The content hash covers a canonical little-endian encoding of
//           vertices, indices, batches, and bounds, so equal inputs give
//           equal hashes on every supported platform.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_RENDER_MESH_H
#define CYPHER_EDITOR_GEOMETRY_COOK_RENDER_MESH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_MeshStore.h"
#include "CypherCommon/Tier1/CypherCommon_ContentHash.h"

namespace cypher::editor::geometry
{

// Bump when the vertex layout, tangent basis, or hash encoding changes, so
// dependency keys built from (source revision, policy, version) invalidate.
inline constexpr common::u32 kRenderMeshCookVersion = 1u;

struct render_vertex_t {
    common::f32 position[3]{};
    common::f32 normal[3]{};
    common::f32 tangent[4]{};   // xyz tangent, w = bitangent handedness (+1 / -1)
    common::f32 uv0[2]{};
    common::f32 uv1[2]{};
    common::u32 colorRgba{ 0xFFFFFFFFu };
};

struct render_batch_t {
    geometry_material_ref_t material{};
    common::u32 iFirstIndex{ 0u };
    common::u32 cIndices{ 0u };
};

struct render_mesh_t {
    common::vector_t<render_vertex_t> vertices{};
    common::vector_t<common::u32> indices{};
    common::vector_t<render_batch_t> batches{};
    common::vector_t<geometry_mesh_face_handle_t> triangleFace{}; // after batch sort
    common::f32 boundsMin[3]{};
    common::f32 boundsMax[3]{};
    common::content_hash_t contentHash{};
};

CYPHER_NODISCARD geometry_status_t RenderMesh_Init(
    render_mesh_t *pOut,
    const common::allocator_t *pAllocator ) noexcept;

void RenderMesh_Shutdown( render_mesh_t *pOut ) noexcept;

// Cooks pMesh (+ optional attribute store; null = defaults: smooth, no
// UVs, white, unassigned material) into pOut (initialized; replaced).
// Fails with NUMERIC_FAILURE if any position/UV does not fit binary32,
// DEGENERATE if a face cannot be tessellated, or the usual
// NOT_INITIALIZED / INVALID_ARGUMENT / ALLOCATION_FAILED. pOut is cleared
// on failure.
CYPHER_NODISCARD geometry_status_t RenderMesh_TryCook(
    const editable_mesh_t *pMesh,
    const mesh_attribute_store_t *pStore,
    render_mesh_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_RENDER_MESH_H
