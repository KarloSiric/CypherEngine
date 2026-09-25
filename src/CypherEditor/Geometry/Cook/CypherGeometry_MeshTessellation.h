//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshTessellation.h
//  Purpose: Declares deterministic tessellation of an EditableMesh into
//           indexed triangles with complete triangle -> face source mapping.
//  Details: Each face is ear-clipped in its own dominant projection, so
//           convex, concave, and collinear-cornered polygons all produce
//           non-degenerate triangles with the face's winding. Vertices are
//           shared (one output position per mesh vertex); seam expansion
//           for normals/UVs is Cook/RenderMesh's job, not this module's
//           (Tessellation README: derives triangles, never replaces source).
//
//           Output order is deterministic: vertices in pool slot order,
//           faces in pool slot order, triangles within a face in ear order.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_TESSELLATION_H
#define CYPHER_EDITOR_GEOMETRY_MESH_TESSELLATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

struct mesh_tessellation_t {
    common::vector_t<math::vec3d_t> positions{};                  // one per mesh vertex used
    common::vector_t<geometry_mesh_vertex_handle_t> vertexSource{}; // parallel to positions
    common::vector_t<common::u32> indices{};                      // 3 per triangle
    common::vector_t<geometry_mesh_face_handle_t> triangleFace{}; // 1 per triangle
    // The face corner (half-edge whose origin is the vertex, in the
    // triangle's face) behind each index — what per-corner attributes and
    // split normals are keyed by. 3 per triangle, parallel to indices.
    common::vector_t<geometry_mesh_half_edge_handle_t> triangleCorners{};
};

CYPHER_NODISCARD geometry_status_t MeshTessellation_Init(
    mesh_tessellation_t *pOut,
    const common::allocator_t *pAllocator ) noexcept;

void MeshTessellation_Shutdown( mesh_tessellation_t *pOut ) noexcept;

CYPHER_NODISCARD common::usize MeshTessellation_TriangleCount(
    const mesh_tessellation_t *pTess ) noexcept;

// Rebuilds pOut (initialized) from pMesh, replacing previous contents.
// Failure-atomic with respect to pOut's previous contents only in the
// sense that pOut is cleared on failure (never half-filled).
//
// Returns NOT_INITIALIZED, INVALID_ARGUMENT, LIMIT_EXCEEDED (face with more
// than 256 corners), DEGENERATE (a face that cannot be ear-clipped in its
// plane — zero area or self-overlapping; the failing face is written to
// *phFailedFaceOut when non-null), or ALLOCATION_FAILED.
CYPHER_NODISCARD geometry_status_t MeshTessellation_TryBuild(
    const editable_mesh_t *pMesh,
    mesh_tessellation_t *pOut,
    geometry_mesh_face_handle_t *phFailedFaceOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_TESSELLATION_H
