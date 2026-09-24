//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBevel.h
//  Purpose: Declares the multi-edge, multi-segment edge bevel (chamfer for
//           one segment, rounded profile for more) on the editable mesh.
//  Details: Width is the offset from each beveled edge measured inside each
//           adjacent face (Blender's "offset" mode). Every beveled edge
//           becomes a strip of `cSegments` faces whose cross-section is a
//           quarter-circle profile (an affine quarter-circle for non-square
//           corners); the faces around it shrink.
//
//           What happens at a vertex depends on how many beveled edges meet
//           there (b):
//             - b = 1 (the end of a beveled edge, vertex valence 3): the two
//               new corners slide along the two side edges, and the third
//               face (the end cap) takes the profile;
//             - b = 2: the two strips meet in one shared, mitered profile, so
//               a chain of beveled edges runs through the vertex without a
//               seam;
//             - b >= 3: each face between two beveled edges gets a miter
//               corner, and a corner patch closes the gap (one polygon for a
//               chamfer, a triangle fan for a rounded bevel).
//           Between two beveled edges a vertex's faces must be one face
//           (miter corner) or two faces sharing a non-beveled edge (the
//           corner slides along that edge). Other configurations - open
//           boundary edges, a single beveled edge at a vertex of valence
//           other than 3, reflex corners, three or more faces between
//           beveled edges - are UNSUPPORTED in this version and leave the
//           mesh unchanged.
//
//           The whole result is validated before anything changes: every
//           reshaped face must stay a simple polygon with its orientation
//           (exact test), and every face must clear the source's minimum
//           area. A width too large for the geometry is therefore rejected
//           instead of producing folded faces. The mutation itself is one
//           MeshBoundary_ReplaceFaces call, so the bevel is failure-atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_BEVEL_H
#define CYPHER_EDITOR_GEOMETRY_MESH_BEVEL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kMeshBevelSegmentsMax = 64u;

struct mesh_bevel_params_t {
    common::f64 width{ 0.1 };  // offset from the edge inside each adjacent face
    common::u32 cSegments{ 1u }; // 1 = chamfer, more = rounded profile
};

enum class mesh_bevel_face_role_t : common::u8 {
    RESHAPED = 0u, // an original face with its corners moved in
    STRIP,         // a face of an edge's bevel strip
    CORNER         // a face of a corner patch
};

// Where a new face came from, for attribute transfer. hSource is a handle
// from before the bevel (those faces no longer exist afterwards):
//   RESHAPED: the face it replaces (same face, new shape);
//   STRIP:    the face on the edge record's half-edge side;
//   CORNER:   the face of the corner vertex's stored outgoing half-edge.
struct mesh_bevel_face_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_face_handle_t hSource{};
    mesh_bevel_face_role_t role{ mesh_bevel_face_role_t::RESHAPED };
};

struct mesh_bevel_result_t {
    common::u32 cFacesReshaped{ 0u };
    common::u32 cStripFaces{ 0u };
    common::u32 cCornerFaces{ 0u };
    common::u32 cVerticesCreated{ 0u };
    common::u32 cVerticesRemoved{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Bevels `edges` (see the file comment for supported configurations).
// Rejected (mesh unchanged): no edges, duplicates, bad width (non-finite ->
// NUMERIC_FAILURE, not positive -> INVALID_ARGUMENT), cSegments outside
// [1, kMeshBevelSegmentsMax] -> INVALID_ARGUMENT; stale handles ->
// INVALID_HANDLE; unsupported configurations -> UNSUPPORTED; a width that
// would fold or collapse a face -> SELF_INTERSECTING / DEGENERATE.
// pFacesOut (optional, initialized) receives every new face with its role
// and source.
CYPHER_NODISCARD mesh_bevel_result_t MeshBevel_Edges(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_edge_handle_t> edges,
    const mesh_bevel_params_t &params,
    common::vector_t<mesh_bevel_face_t> *pFacesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_BEVEL_H
