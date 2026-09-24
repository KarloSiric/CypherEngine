//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_RenderMeshCook.h
//  Purpose: Declares cooking of a document snapshot into a neutral,
//           renderer-agnostic triangle mesh with material batches and
//           per-triangle source mapping.
//  Details: The cook is the boundary where binary64 authoring data becomes
//           f32 runtime data, and every conversion is checked. Faces are
//           seam-expanded (each face owns its vertices) because brush faces
//           are flat and carry independent projections; normals are the
//           exact side normals and UVs come from each side's planar
//           projection, with tangents taken from its U axis.
//
//           Output order is canonical: batches by ascending material
//           reference, then brushes by ascending ID, then faces in side
//           order, then a fan per face. The same snapshot and policy always
//           produce the same bytes, which ContentHash summarizes. The mesh
//           records the snapshot revision it came from. GPU buffers and
//           draw submission belong to the renderer, not here.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_RENDER_MESH_COOK_H
#define CYPHER_EDITOR_GEOMETRY_RENDER_MESH_COOK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CookSourceMap.h"
#include "CypherGeometry_Snapshot.h"

namespace cypher::editor::geometry
{

struct geometry_render_vertex_t {
    math::vec3_t position;
    math::vec3_t normal;
    // xyz = tangent (projection U axis in the face plane), w = handedness
    // (+1 when normal x tangent points along the projection V axis).
    math::f32 tangent[4];
    math::vec2_t uv;
};

struct geometry_render_batch_t {
    geometry_material_ref_t material;
    common::u32 iFirstIndex;
    common::u32 cIndices;
};

struct geometry_render_mesh_t {
    geometry_render_mesh_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( geometry_render_mesh_t );

    common::vector_t<geometry_render_vertex_t> vertices{};
    common::vector_t<common::u32> indices{};
    common::vector_t<geometry_render_batch_t> batches{};
    // One entry per triangle (indices / 3), in index order.
    common::vector_t<geometry_cook_source_t> triangleSources{};
    math::aabb_t bounds{};
    common::u64 sourceRevision{ 0u };
    common::u64 contentHash{ 0u };
};

CYPHER_NODISCARD geometry_status_t RenderMeshCook_Init(
    geometry_render_mesh_t *pMesh, const common::allocator_t *pAllocator ) noexcept;

void RenderMeshCook_Shutdown( geometry_render_mesh_t *pMesh ) noexcept;

// Rebuilds the mesh from the snapshot. Failure-atomic: on any error the
// mesh is left empty (sourceRevision 0). NUMERIC_FAILURE when a value does
// not survive f32 conversion; LIMIT_EXCEEDED past 2^32 - 1 vertices.
CYPHER_NODISCARD geometry_status_t RenderMeshCook_TryBuild(
    geometry_render_mesh_t *pMesh,
    const geometry_document_snapshot_t *pSnapshot ) noexcept;

CYPHER_NODISCARD common::usize RenderMeshCook_TriangleCount(
    const geometry_render_mesh_t *pMesh ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_RENDER_MESH_COOK_H
