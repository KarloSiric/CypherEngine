//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshVertexMove.h
//  Purpose: Declares the validated multi-vertex move: the mesh-level core of
//           component transforms (move / rotate / scale / offset a selection).
//  Details: Moving vertices never changes topology, but it can ruin faces: a
//           dragged corner can fold a quad into a bow-tie or squash a face
//           to nothing. So the whole move is checked first with the same
//           rules the other reshaping operations use - every face touching a
//           moved vertex must stay a simple polygon (exact test, in the
//           projection of its own new normal) and clear the source's
//           minimum face area - and only then are positions written and the
//           affected normals recomputed. A rejected move changes nothing.
//
//           Faces may become non-planar; that is allowed (mesh faces are
//           not required to be planar), only folding and collapse are not.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_VERTEX_MOVE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_VERTEX_MOVE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

// Moves vertices[i] to positions[i]. Rejected (mesh unchanged): mismatched
// or empty spans -> INVALID_ARGUMENT; stale or repeated handles ->
// INVALID_HANDLE / INVALID_ARGUMENT; non-finite positions ->
// NUMERIC_FAILURE; a face that would fold -> SELF_INTERSECTING; a face that
// would fall below the minimum area -> DEGENERATE. *pFacesUpdatedOut
// (optional) receives how many faces had their normal recomputed.
CYPHER_NODISCARD geometry_status_t MeshVertices_TryMove(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_vertex_handle_t> vertices,
    common::span_t<const math::vec3d_t> positions,
    common::u32 *pFacesUpdatedOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_VERTEX_MOVE_H
