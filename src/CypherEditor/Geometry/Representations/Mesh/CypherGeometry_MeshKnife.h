//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshKnife.h
//  Purpose: Declares the knife cut on the editable half-edge mesh: a path of
//           points on vertices, on edges, and inside faces that splits every
//           face it crosses.
//  Details: The host (viewport tool) produces the path by ray-picking; this
//           module owns the topology and the exact validity decision.
//
//           A path is a sequence of boundary points (on a vertex or an edge)
//           with optional runs of interior points (inside one face) between
//           them. Each pair of consecutive boundary points, together with
//           the interior points between them, is one cut segment and splits
//           exactly one face. The path must start and end on a boundary
//           point: a cut that stops inside a face would leave a dangling
//           edge, which a face loop cannot represent.
//
//           Validity is decided exactly. Each crossed face is projected by
//           dropping its dominant normal axis (a projection that introduces
//           no rounding), and both resulting pieces must be simple polygons
//           with the original face's orientation. Together those conditions
//           mean the cut runs strictly inside the face and does not cross
//           itself; they are tested with the exact Orient2D predicate, so a
//           cut that grazes a vertex or runs along an edge is rejected
//           rather than producing a sliver or overlapping faces.
//
//           Failure-atomic like the other mesh operations: the whole path is
//           validated and every record reserved before the first mutation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_KNIFE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_KNIFE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Most points one knife path may have. A viewport stroke is tens of points;
// the bound keeps preflight scratch predictable.
inline constexpr common::u32 kMeshKnifePathMax = 1024u;

enum class mesh_knife_point_kind_t : common::u8 {
    VERTEX = 0u, // on an existing vertex
    EDGE,        // on an existing edge, strictly between its endpoints
    FACE         // strictly inside a face
};

struct mesh_knife_point_t {
    mesh_knife_point_kind_t kind{ mesh_knife_point_kind_t::VERTEX };
    geometry_mesh_vertex_handle_t hVertex{}; // VERTEX
    // EDGE: t in (0, 1) is measured from the origin of the edge record's
    // hHalfEdge (the MeshOps_SplitEdge convention). The same (edge, t)
    // appearing twice in a path is one point.
    geometry_mesh_edge_handle_t hEdge{};
    common::f64 t{ 0.5 };
    // FACE: the face the point is inside and its position. The position is
    // not forced onto the face plane (faces may be slightly non-planar);
    // containment is judged in the face's projection.
    geometry_mesh_face_handle_t hFace{};
    math::vec3d_t position{};
};

// One face the cut split: hFace kept one piece, hNewFace is the other.
struct mesh_knife_split_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_face_handle_t hNewFace{};
};

struct mesh_knife_result_t {
    common::u32 cVerticesCreated{ 0u };
    common::u32 cEdgesCreated{ 0u };
    common::u32 cFacesCreated{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Cuts the mesh along `path`. For segment (A, interior points..., B):
//   - with interior points, they must all name the same face F and A, B
//     must lie on F's boundary;
//   - without, the segment splits the one face that has both A and B on its
//     boundary and not next to each other; a segment between two vertices
//     already joined by an edge runs along that edge and is skipped.
// The face keeps the piece that starts at A's side of its loop (the piece
// containing the loop from A forward to B); the other piece is a new face
// with the same source side.
//
// Rejected (mesh unchanged):
//   - fewer than 2 points, more than kMeshKnifePathMax, a path that starts
//     or ends inside a face, bad t, non-finite positions -> INVALID_ARGUMENT
//     / NUMERIC_FAILURE; stale handles -> INVALID_HANDLE;
//   - a segment whose face cannot be determined (none or several faces
//     qualify), or a face crossed twice in one path -> INVALID_ARGUMENT
//     (cut the resulting piece with a second path instead);
//   - a segment from a point back to itself, or consecutive identical
//     positions -> DEGENERATE;
//   - a cut that leaves the face, crosses itself, or touches another corner
//     of the face -> SELF_INTERSECTING;
//   - a piece whose area is at or below the source's minimum face area
//     (typically a sliver between two pieces of one straight edge, whose
//     split points are only rounded onto the line) -> DEGENERATE;
//   - any face (cut, or neighbouring a split edge) that would end up with
//     more than kMeshSourceCornersPerFaceMax corners -> LIMIT_EXCEEDED, so
//     every result stays describable as a mesh source.
//
// pPointVerticesOut (optional, initialized) receives one vertex per path
// point, in path order (existing vertices for VERTEX points). pSplitsOut
// (optional, initialized) receives the split faces in path order.
CYPHER_NODISCARD mesh_knife_result_t MeshKnife_Cut(
    editable_mesh_t *pMesh,
    common::span_t<const mesh_knife_point_t> path,
    common::vector_t<geometry_mesh_vertex_handle_t> *pPointVerticesOut,
    common::vector_t<mesh_knife_split_t> *pSplitsOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_KNIFE_H
