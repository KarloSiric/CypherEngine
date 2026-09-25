//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshMerge.h
//  Purpose: Declares vertex merging on the editable mesh, and the two
//           Hammer workflows built on it: sewing open edges together, and
//           merging vertices that lie within a distance of each other.
//  Details: MeshMerge_Vertices is the primitive: each group of vertices
//           becomes its first vertex (which keeps its handle, so a mesh
//           source keeps its ID) at the group's target position. Unlike
//           MeshOps_WeldVertices, the vertices need not share an edge -
//           merging is how two separate parts, or the two sides of a slit,
//           are joined.
//
//           How: every face touching a merged vertex is rebuilt with the
//           survivors in one MeshBoundary_ReplaceFaces call, which stitches
//           any two open edges that now run between the same vertices in
//           opposite directions. Corners that merge into their neighbour
//           drop out of the face (so merging an edge's two ends collapses
//           it), and a face left with fewer than three corners disappears.
//           Merges that would fold the surface are rejected, not repaired:
//           a face that would visit a vertex twice, two faces running along
//           the same edge the same way, or a vertex left with two separate
//           fans are NON_MANIFOLD. The survivors move to their targets
//           before the rebuild so its area checks see the final shape, and
//           move back if it fails - the merge is failure-atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_MERGE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_MERGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Where a merged group ends up.
enum class mesh_merge_target_t : common::u8 {
    CENTER = 0u, // the average of the group's positions
    FIRST        // the first vertex's position (a "target weld")
};

// A face rebuilt by a merge: the same face with corners replaced. hSource
// is the face before the merge (it no longer exists afterwards).
struct mesh_merge_face_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_face_handle_t hSource{};
};

struct mesh_merge_result_t {
    common::u32 cVerticesMerged{ 0u }; // vertices removed into survivors
    common::u32 cFacesRebuilt{ 0u };
    common::u32 cFacesCollapsed{ 0u }; // faces left with under 3 corners
    common::u32 cEdgesJoined{ 0u };    // pairs of open edges stitched into one
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Merges each group into its first vertex. groupVertices holds the groups
// back to back; groupSizes the size of each (at least 2). Rejected (mesh
// unchanged): empty input or a group under 2 -> INVALID_ARGUMENT; a stale
// handle or a vertex in two groups (or twice in one) -> INVALID_HANDLE; a
// non-finite target -> NUMERIC_FAILURE; folds (see the file comment) ->
// NON_MANIFOLD; a rebuilt face too small to describe, or merging away every
// face -> DEGENERATE. pFacesOut (optional, initialized) receives the
// rebuilt faces.
CYPHER_NODISCARD mesh_merge_result_t MeshMerge_Vertices(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_vertex_handle_t> groupVertices,
    common::span_t<const common::u32> groupSizes,
    mesh_merge_target_t target,
    common::vector_t<mesh_merge_face_t> *pFacesOut ) noexcept;

// Sews open edges together in pairs (edges[0] with edges[1], edges[2] with
// edges[3], ...). Both edges of a pair must be boundary edges; they join
// crosswise - the start of one with the end of the other - which is the
// only pairing whose windings agree. A pair whose edges run the same way
// (the other pairing would bring closer ends together) belongs to faces
// facing opposite ways; joining it would twist the surface, so it is
// NON_MANIFOLD - flip one side first. Pairs that share vertices (sewing a
// chain of edges, or zipping a slit shut from a shared end) merge
// transitively. Survivors come from the first edge of each pair. A
// non-boundary edge, an edge used twice, or an odd count is
// INVALID_ARGUMENT; the rest as MeshMerge_Vertices.
CYPHER_NODISCARD mesh_merge_result_t MeshMerge_SewEdges(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_edge_handle_t> edges,
    mesh_merge_target_t target,
    common::vector_t<mesh_merge_face_t> *pFacesOut ) noexcept;

// Merges every set of vertices chained together by distances of at most
// `tolerance` (clean-up after import or duplication), connected by edges or
// not. Each group keeps its lowest-handle vertex, at its own position
// (FIRST), so exact duplicates weld without moving anything. Nothing within
// tolerance is OK with no change. tolerance must be finite and >= 0
// (0 merges exact duplicates only).
CYPHER_NODISCARD mesh_merge_result_t MeshMerge_ByDistance(
    editable_mesh_t *pMesh,
    common::f64 tolerance,
    common::vector_t<mesh_merge_face_t> *pFacesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_MERGE_H
