//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshQuadSlice.h
//  Purpose: Declares Hammer's Quad Slice on the editable mesh: cutting
//           selected quads into a grid of cU x cV quads.
//  Details: Each quad's grid runs U along its first edge (corner 0 -> 1)
//           and V along its second (1 -> 2); interior points are bilinear
//           in the four corners, so a planar quad slices into planar cells
//           and a twisted quad into the matching bilinear patch.
//
//           No T-junctions: every sliced edge gets one set of new vertices,
//           shared by both of its faces. A neighbouring face that was not
//           selected is rebuilt with those vertices inserted along the
//           shared edge (it gains corners but keeps its shape and
//           identity), so the result stays watertight. Two selected quads
//           sharing an edge must agree on how many pieces it gets: with
//           cU != cV, a shared edge that is a U edge for one quad and a V
//           edge for the other is INVALID_ARGUMENT.
//
//           Planned first, applied as one MeshBoundary_ReplaceFaces call:
//           failure-atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_QUAD_SLICE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_QUAD_SLICE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kMeshQuadSliceCutsMax = 64u;

enum class mesh_quad_slice_role_t : common::u8 {
    CELL = 0u, // a grid cell of a sliced quad
    NEIGHBOR   // an unselected face rebuilt with the new edge vertices
};

// hSource: the quad the cell came from, or the neighbour's old handle (both
// removed by the slice). bKeepsIdentity: the neighbour itself, or a quad's
// corner-0 cell - one new face per old face carries its identity.
struct mesh_quad_slice_face_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_face_handle_t hSource{};
    mesh_quad_slice_role_t role{ mesh_quad_slice_role_t::CELL };
    bool bKeepsIdentity{ false };
};

struct mesh_quad_slice_result_t {
    common::u32 cQuadsSliced{ 0u };
    common::u32 cNeighborsRebuilt{ 0u };
    common::u32 cVerticesCreated{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Slices each face in `faces` into cU x cV cells (see the file comment).
// cU = cV = 1 is OK with no change. Rejected (mesh unchanged): a count of 0
// or above kMeshQuadSliceCutsMax, no faces, a face that is not a quad, or
// a disagreeing shared edge -> INVALID_ARGUMENT; stale or repeated faces ->
// INVALID_HANDLE; a neighbour pushed past the source's corner limit ->
// LIMIT_EXCEEDED; a cell too small to describe -> DEGENERATE. pFacesOut
// (optional, initialized) receives every new face.
CYPHER_NODISCARD mesh_quad_slice_result_t MeshQuadSlice_Faces(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    common::u32 cU,
    common::u32 cV,
    common::vector_t<mesh_quad_slice_face_t> *pFacesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_QUAD_SLICE_H
