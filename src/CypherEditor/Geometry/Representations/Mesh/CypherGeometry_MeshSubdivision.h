//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSubdivision.h
//  Purpose: Declares mesh subdivision surface operations.
//  Details: Catmull-Clark subdivision takes a polygonal mesh and
//           produces a smoother mesh with quadrilateral faces. One
//           iteration replaces each N-sided face with N quads by
//           inserting face points, edge points, and repositioning
//           original vertices according to the Catmull-Clark rules.
//
//           The operation builds a new mesh from scratch rather than
//           editing in place, because the topology change is too
//           large for incremental patching. The caller owns both
//           the input and output meshes.
//
//           Linear subdivision (no vertex smoothing) is also provided
//           for cases where the user wants to add detail without
//           changing the shape.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SUBDIVISION_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SUBDIVISION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

// Performs one level of Catmull-Clark subdivision on the input mesh,
// producing a new output mesh. Each N-sided face becomes N quads.
//
// Algorithm:
//   1. Face point = centroid of the face's vertices.
//   2. Edge point = average of the edge's two endpoints and the two
//      adjacent face points.
//   3. Vertex point = (F + 2R + (n-3)P) / n, where:
//        F = average of face points for faces adjacent to the vertex
//        R = average of edge midpoints for edges adjacent to the vertex
//        P = original vertex position
//        n = valence of the vertex
//   4. Connect: for each original face, create one quad per original
//      edge, with corners: face point, edge point on one edge, moved
//      vertex at the corner, edge point on the next edge.
//
// The input must be a structurally and geometrically valid closed manifold.
// The output mesh must be initialized, empty, distinct from the input, and
// have limits large enough for the exact subdivided element counts. On every
// failure after a valid output argument is accepted, the output is shut down
// and owns no storage. Null, aliased, and already-uninitialized output
// arguments are rejected without mutation. The input mesh is never modified.
CYPHER_NODISCARD geometry_status_t MeshSubdivision_TryCatmullClark(
    const editable_mesh_t *pMeshIn,
    editable_mesh_t *pMeshOut ) noexcept;

// Performs one level of linear subdivision (midpoint refinement). Each
// N-sided face is split into N quads using face and edge midpoints,
// but original vertex positions are NOT smoothed. This adds geometric
// detail without altering the silhouette.
CYPHER_NODISCARD geometry_status_t MeshSubdivision_TryLinear(
    const editable_mesh_t *pMeshIn,
    editable_mesh_t *pMeshOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SUBDIVISION_H
