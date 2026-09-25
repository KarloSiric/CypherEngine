//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSelectionQueries.h
//  Purpose: Declares attribute, geometric, and spatial selection queries on
//           mesh sources (select by material, coplanar, facing, sharp edges,
//           boundary, face size, invert, inside a convex volume) and the
//           vertex set / pivot a component transform acts on.
//  Details: Every query ADDS to the selection (the host clears first for
//           "replace" behaviour) and is failure-atomic: matches are
//           collected, sorted, and merged in one step, so an allocation
//           failure leaves the selection exactly as it was, and bulk queries
//           cost O(n log n) rather than n sorted inserts.
//
//           Queries read geometry only; screen-space concerns (marquee to
//           frustum, occlusion, pixel tolerances) belong to the host, which
//           hands the volume query a set of planes. Elements without a
//           source ID yet (created since the last ID assignment) are skipped:
//           a selection can only hold persistent identities.
//
//           Angles are in radians. Normals are the mesh's stored unit face
//           normals.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SELECTION_QUERIES_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SELECTION_QUERIES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSelection.h"
#include "CypherMath_Plane.h"

namespace cypher::editor::geometry
{

// Faces whose material is `material`.
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectFacesByMaterial(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    geometry_material_ref_t material ) noexcept;

// Hammer's "select coplanar": flood from `seedFace` across shared edges to
// every face whose normal is within maxAngle of the seed's and whose corners
// all lie within maxDistance of the seed's plane. The seed is included.
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectCoplanar(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    geometry_source_id_t seedFace,
    common::f64 maxAngle,
    common::f64 maxDistance ) noexcept;

// Every face (connected or not) whose normal is within maxAngle of
// `direction` - e.g. all floors with direction +Z.
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectFacesFacing(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    math::vec3d_t direction,
    common::f64 maxAngle ) noexcept;

// Interior edges whose two faces' normals differ by at least minAngle.
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectSharpEdges(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    common::f64 minAngle ) noexcept;

// Edges with only one face (the open rim of a mesh).
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectBoundaryEdges(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh ) noexcept;

// Faces with between minCorners and maxCorners corners (triangles: 3, 3;
// n-gons: 5, UINT32_MAX).
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectFacesBySize(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    common::u32 minCorners,
    common::u32 maxCorners ) noexcept;

// Replaces the `mode` set by every element of the mesh not in it (other
// modes are untouched).
CYPHER_NODISCARD geometry_status_t MeshSelection_TryInvert(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    mesh_selection_mode_t mode ) noexcept;

// How a face or edge is judged against a volume.
enum class mesh_volume_rule_t : common::u8 {
    CONTAINED = 0u, // every vertex inside
    ANY_VERTEX,     // at least one vertex inside
    CENTROID        // the vertex average inside
};

// Elements of `mode` inside the convex volume bounded by `planes`, whose
// normals point OUT of the volume: a point p is inside when
// dot(n, p) + d <= 0 for every plane (a marquee frustum or a box). Vertices
// are judged directly; edges and faces by `rule`. No planes is
// INVALID_ARGUMENT (it would select everything by accident).
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectInVolume(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    common::span_t<const math::planed_t> planes,
    mesh_selection_mode_t mode,
    mesh_volume_rule_t rule ) noexcept;

// The vertices a component operation on the `mode` set acts on: selected
// vertices; the ends of selected edges; the corners of selected faces.
// Unique, in handle order. IDs that no longer resolve are INVALID_HANDLE
// (prune the selection after an edit first).
CYPHER_NODISCARD geometry_status_t MeshSelection_TryGatherVertices(
    const mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    mesh_selection_mode_t mode,
    common::vector_t<geometry_mesh_vertex_handle_t> *pVerticesOut ) noexcept;

enum class mesh_pivot_mode_t : common::u8 {
    CENTROID = 0u,  // mean of the gathered vertices
    BOUNDS_CENTER   // centre of their axis-aligned bounds
};

// The pivot for transforming the `mode` set. An empty set is
// INVALID_ARGUMENT.
CYPHER_NODISCARD geometry_status_t MeshSelection_TryComputePivot(
    const mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    mesh_selection_mode_t mode,
    mesh_pivot_mode_t pivotMode,
    math::vec3d_t *pPivotOut ) noexcept;

// How "select path" measures a path.
enum class mesh_path_metric_t : common::u8 {
    LENGTH = 0u, // total edge length (vertex path) or centroid-to-centroid
                 // distance (face path): the geometrically shortest route
    STEPS        // fewest edges / faces
};

// Hammer's "select path" between two vertices: adds the vertices and edges
// of the shortest route along edges (ties broken by lower vertex slot, so
// the result is deterministic). *pFoundOut (optional) is false when the two
// are not connected, which is not an error (nothing is added). The two
// vertices may be the same (just that vertex). Unknown IDs ->
// INVALID_HANDLE; an unknown metric -> INVALID_ARGUMENT.
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectVertexPath(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    geometry_source_id_t fromVertex,
    geometry_source_id_t toVertex,
    mesh_path_metric_t metric,
    bool *pFoundOut ) noexcept;

// The same between two faces, across shared edges; adds the faces.
CYPHER_NODISCARD geometry_status_t MeshSelection_TrySelectFacePath(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    geometry_source_id_t fromFace,
    geometry_source_id_t toFace,
    mesh_path_metric_t metric,
    bool *pFoundOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SELECTION_QUERIES_H
