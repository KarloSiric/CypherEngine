//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshTopologyOps.h
//  Purpose: Declares topology-editing operations on the editable mesh.
//  Details: Each operation modifies the half-edge mesh in place while
//           maintaining the two-manifold invariant. Operations are
//           atomic: on failure live records, handles, and topology are left
//           unchanged. A preflight reserve that succeeds before a later
//           reserve fails may leave backing pool capacity grown; successful
//           reserve can also relocate records and invalidate raw pointers.
//
//           MoveVertex — repositions a vertex without changing topology.
//           SplitEdge — inserts a new vertex at a parametric point on an
//              edge, splitting it into two edges.
//           SplitFace — inserts a new edge between two vertices of the
//              same face, dividing it into two faces.
//           CollapseEdge — merges an edge's two endpoints into one vertex,
//              removing the edge and any resulting degenerate faces.
//           DissolveEdge — removes an edge and merges its two adjacent
//              faces into a single face.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_TOPOLOGY_OPS_H
#define CYPHER_EDITOR_GEOMETRY_MESH_TOPOLOGY_OPS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

// Moves a vertex to a new position without changing topology. Updates
// the face normals of all faces adjacent to the vertex.
CYPHER_NODISCARD geometry_status_t MeshOps_MoveVertex(
    editable_mesh_t *pMesh,
    geometry_mesh_vertex_handle_t hVertex,
    math::vec3d_t newPosition ) noexcept;

// Result of a split-edge operation. Returns the handle of the new vertex
// inserted at the split point, and the handle of the new edge created on
// the "second half" of the original edge.
struct mesh_split_edge_result_t {
    geometry_mesh_vertex_handle_t hNewVertex{};
    geometry_mesh_edge_handle_t hNewEdge{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Splits an edge by inserting a new vertex at the parametric position
// t ∈ (0, 1) along the edge (0 = start vertex, 1 = end vertex). The
// original edge keeps one half and a new edge covers the other. All
// adjacent face loops are updated to include the new vertex. Failure leaves
// live records, handles, and topology unchanged, although backing capacity
// reserved during preflight may remain grown.
CYPHER_NODISCARD mesh_split_edge_result_t MeshOps_SplitEdge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge,
    common::f64 t ) noexcept;

// Result of a split-face operation. Returns the new edge that divides the
// face and the handle of the new face created on one side of that edge.
struct mesh_split_face_result_t {
    geometry_mesh_edge_handle_t hNewEdge{};
    geometry_mesh_face_handle_t hNewFace{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Splits a face by inserting a new edge between two non-adjacent vertices
// of the face. The original face keeps one portion; a new face is created
// for the other. Both vertices must belong to the same face and must not
// be adjacent (already sharing an edge on the face boundary). Failure leaves
// live records, handles, and topology unchanged, although backing capacity
// reserved during preflight may remain grown.
CYPHER_NODISCARD mesh_split_face_result_t MeshOps_SplitFace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    geometry_mesh_vertex_handle_t hVertexA,
    geometry_mesh_vertex_handle_t hVertexB ) noexcept;

// Collapses an edge by merging its two endpoints into the first vertex.
// The edge is removed, and any face that would become degenerate (fewer
// than 3 edges) is also removed. Returns the surviving vertex handle.
struct mesh_collapse_edge_result_t {
    geometry_mesh_vertex_handle_t hSurvivor{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

CYPHER_NODISCARD mesh_collapse_edge_result_t MeshOps_CollapseEdge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept;

// Dissolves an edge, merging its two adjacent faces into one. The edge
// is removed and the surviving face's loop absorbs both old loops. If the
// edge is on the boundary of the mesh (only one adjacent face), the
// operation fails with INVALID_ARGUMENT.
CYPHER_NODISCARD geometry_status_t MeshOps_DissolveEdge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept;

// ---------------------------------------------------------------------------
// Face extrusion (Gate 12)
// ---------------------------------------------------------------------------

// Result of a face extrusion. Returns the handle of the extruded face
// (the new top face at the offset position).
struct mesh_extrude_face_result_t {
    geometry_mesh_face_handle_t hExtrudedFace{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Extrudes a face by duplicating its boundary vertices, offsetting the
// copies along the face normal by `distance`, and connecting the original
// boundary to the new boundary with side quads. The original face is
// removed and replaced by the extruded (offset) face.
//
// Negative distance extrudes inward. Zero distance returns DEGENERATE without
// changing the mesh because it would create zero-area side faces and violate
// the closed solid mesh validation contract.
//
// The operation adds N new vertices, N new quad faces (the sides), and
// one replacement face (the extruded top). N is the number of edges in
// the original face loop.
CYPHER_NODISCARD mesh_extrude_face_result_t MeshOps_ExtrudeFace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    common::f64 distance ) noexcept;

// ---------------------------------------------------------------------------
// Face inset (Gate 12)
// ---------------------------------------------------------------------------

// Result of a face inset. Returns the handle of the inset face (the
// smaller face at the center).
struct mesh_inset_face_result_t {
    geometry_mesh_face_handle_t hInsetFace{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Insets a face by shrinking it inward by `margin` (in world units).
// New vertices are created at the inset positions, and a ring of quads
// connects the original boundary to the inset boundary. The original
// face is removed and replaced by the smaller inset face.
//
// margin must be strictly positive. If the margin is too large (would
// collapse the face), returns DEGENERATE.
CYPHER_NODISCARD mesh_inset_face_result_t MeshOps_InsetFace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    common::f64 margin ) noexcept;

// ---------------------------------------------------------------------------
// Loop cut (Gate 12)
// ---------------------------------------------------------------------------

// Result of a loop cut. Returns the number of faces that were split.
struct mesh_loop_cut_result_t {
    common::u32 cFacesSplit{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Inserts an edge loop across a ring of quad faces by splitting each
// quad at parametric position t ∈ (0, 1) along the edge ring. The
// edge ring is discovered by following the quad strip starting from
// hStartEdge — the loop continues through each quad's opposite edge
// until it returns to the start or hits a non-quad face.
//
// Each quad in the ring is split into two quads by inserting two new
// vertices (one on each of the quad's "parallel" edges) and connecting
// them with a new edge.
CYPHER_NODISCARD mesh_loop_cut_result_t MeshOps_LoopCut(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hStartEdge,
    common::f64 t ) noexcept;

// ---------------------------------------------------------------------------
// Vertex welding (Gate 13)
// ---------------------------------------------------------------------------

// Result of a vertex weld operation.
struct mesh_weld_result_t {
    common::u32 cVerticesMerged{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Merges all vertex pairs whose positions are within `tolerance` of each
// other. For each pair found, CollapseEdge is used if the vertices share
// an edge; otherwise the pair is skipped (topologically disconnected
// vertices cannot be welded without adding edges first).
//
// tolerance must be finite and strictly positive.
CYPHER_NODISCARD mesh_weld_result_t MeshOps_WeldVertices(
    editable_mesh_t *pMesh,
    common::f64 tolerance ) noexcept;

// ---------------------------------------------------------------------------
// Edge crease weight (Gate 15)
// ---------------------------------------------------------------------------

// Sets the crease weight for an edge. Weight 0.0 = fully smooth
// (default), 1.0 = fully sharp during Catmull-Clark subdivision.
// Finite values are clamped to [0, 1]; non-finite values are rejected.
CYPHER_NODISCARD geometry_status_t MeshOps_SetEdgeCreaseWeight(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge,
    common::f64 weight ) noexcept;

// Returns the crease weight for an edge, or 0.0 if the handle is
// invalid.
CYPHER_NODISCARD common::f64 MeshOps_GetEdgeCreaseWeight(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept;

// ---------------------------------------------------------------------------
// Edge ring / edge loop selection (Gate 15)
// ---------------------------------------------------------------------------

// Result of an edge ring or loop selection query. Returns the handles
// of the selected edges.
struct mesh_edge_selection_result_t {
    geometry_mesh_edge_handle_t edges[256]{};
    common::u32 cEdges{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Selects an edge ring: edges parallel to hStartEdge across a strip of
// quad faces. Walks from the start edge by jumping across each quad's
// opposite edge until the ring closes or hits a non-quad face.
CYPHER_NODISCARD mesh_edge_selection_result_t MeshOps_SelectEdgeRing(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hStartEdge ) noexcept;

// Selects an edge loop: edges connected end-to-end sharing vertices.
// From the start edge, at each endpoint the loop continues to the
// edge on the opposite side of the vertex fan (the edge 2 half-edges
// away from the incoming one in the fan).
CYPHER_NODISCARD mesh_edge_selection_result_t MeshOps_SelectEdgeLoop(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hStartEdge ) noexcept;

// ---------------------------------------------------------------------------
// Edge bevel / chamfer (Gate 14)
// ---------------------------------------------------------------------------

// Result of an edge bevel operation. Returns the handle of the new face
// that replaces the original edge (the bevel strip).
struct mesh_bevel_edge_result_t {
    geometry_mesh_face_handle_t hBevelFace{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Bevels an edge by replacing it with a new face (a strip quad for a
// single-segment bevel). The edge's two adjacent faces shrink inward by
// `width` along the edge direction, and the gap is filled with a new
// connecting face.
//
// width must be finite and strictly positive. cSegments must be in [1, 64].
// The current implementation supports one chamfer segment; values greater
// than one return UNSUPPORTED without changing the mesh until rounded bevel
// strip generation is implemented.
CYPHER_NODISCARD mesh_bevel_edge_result_t MeshOps_BevelEdge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge,
    common::f64 width,
    common::u32 cSegments ) noexcept;

// ---------------------------------------------------------------------------
// Bridge edges (Gate 14)
// ---------------------------------------------------------------------------

// Result of a bridge operation. Returns the number of bridging quads
// created.
struct mesh_bridge_edges_result_t {
    common::u32 cFacesCreated{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Reserved for connecting two explicit open boundary loops with quad faces.
// EditableMesh currently requires closed two-manifold shells, so valid edge
// handles return UNSUPPORTED without changing the mesh. This API becomes
// operational only after boundary half-edges and boundary loops have a
// representation and validation contract.
CYPHER_NODISCARD mesh_bridge_edges_result_t MeshOps_BridgeEdges(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdgeA,
    geometry_mesh_edge_handle_t hEdgeB ) noexcept;

// ---------------------------------------------------------------------------
// Fill hole (Gate 14)
// ---------------------------------------------------------------------------

// Result of a fill-hole operation. Returns the handle of the new face.
struct mesh_fill_hole_result_t {
    geometry_mesh_face_handle_t hNewFace{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Reserved for filling an explicit open boundary loop with a face.
// EditableMesh currently requires reciprocal twins on every half-edge, so a
// valid edge handle returns UNSUPPORTED without changing the mesh.
CYPHER_NODISCARD mesh_fill_hole_result_t MeshOps_FillHole(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hBoundaryEdge ) noexcept;

// ---------------------------------------------------------------------------
// Detach faces (Gate 14)
// ---------------------------------------------------------------------------

// Result of a detach-faces operation. Returns the number of vertices
// that were duplicated at the boundary.
struct mesh_detach_faces_result_t {
    common::u32 cVerticesDuplicated{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Reserved for detaching faces into a separate closed component. Creating an
// open cut on each side is invalid under the current closed-shell contract,
// and closure-face construction is not implemented; valid face sets therefore
// return UNSUPPORTED without changing the mesh.
//
// phFaces is a pointer to an array of cFaces face handles. All handles
// must be valid and belong to the same mesh.
CYPHER_NODISCARD mesh_detach_faces_result_t MeshOps_DetachFaces(
    editable_mesh_t *pMesh,
    const geometry_mesh_face_handle_t *phFaces,
    common::usize cFaces ) noexcept;

// ---------------------------------------------------------------------------
// Mirror mesh (Gate 14)
// ---------------------------------------------------------------------------

// Mirrors all mesh geometry across an arbitrary plane. Vertex positions
// are reflected, face winding is reversed to maintain outward normals,
// and all topology pointers (twin, next, prev) are updated. The plane may
// be unnormalized, but all coefficients must be finite and its normal must
// be non-degenerate.
CYPHER_NODISCARD geometry_status_t MeshOps_Mirror(
    editable_mesh_t *pMesh,
    math::planed_t mirrorPlane ) noexcept;

// ---------------------------------------------------------------------------
// Laplacian smoothing (Gate 17)
// ---------------------------------------------------------------------------

// Result of a smoothing pass. cVerticesMoved counts distinct vertices
// whose position changed in at least one iteration.
struct mesh_smooth_result_t {
    common::u32 cVerticesMoved{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Smooths the mesh by iteratively moving each interior vertex toward
// the average of its neighbors. `factor` controls the step size per
// iteration (0 = no movement, 1 = snap to average). `cIterations`
// is the number of relaxation passes.
//
// Boundary vertices (vertices on edges with no twin) are not moved.
// Face normals are recomputed after all iterations complete.
//
// A finite factor is clamped to [0, 1]. cIterations must be >= 1.
CYPHER_NODISCARD mesh_smooth_result_t MeshOps_LaplacianSmooth(
    editable_mesh_t *pMesh,
    common::f64 factor,
    common::u32 cIterations ) noexcept;

// ---------------------------------------------------------------------------
// Mesh decimation (Gate 17)
// ---------------------------------------------------------------------------

// Result of a decimation pass.
struct mesh_decimate_result_t {
    common::u32 cEdgesCollapsed{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Reduces mesh complexity by iteratively collapsing the shortest edge
// until either `cTargetFaces` faces remain or no edge shorter than
// `fMinEdgeLength` exists. Each collapse is a topology-safe
// CollapseEdge that removes degenerate faces.
//
// cTargetFaces must be >= 4 (minimum tetrahedron).
// fMinEdgeLength must be > 0.
CYPHER_NODISCARD mesh_decimate_result_t MeshOps_Decimate(
    editable_mesh_t *pMesh,
    common::usize cTargetFaces,
    common::f64 fMinEdgeLength ) noexcept;

// ---------------------------------------------------------------------------
// Face triangulation (Gate 17)
// ---------------------------------------------------------------------------

// Result of a triangulation pass.
struct mesh_triangulate_result_t {
    common::u32 cFacesCreated{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Triangulates all faces with more than 3 edges by ear clipping in the
// face's dominant projection plane. Each simple n-gon (convex, concave,
// or with collinear corners) becomes (n - 2) non-degenerate triangles.
// Faces already triangular are left unchanged. Returns the first failing
// face's status (e.g. DEGENERATE for a self-overlapping face); faces
// processed before it remain triangulated.
CYPHER_NODISCARD mesh_triangulate_result_t MeshOps_TriangulateFaces(
    editable_mesh_t *pMesh ) noexcept;

// Triangulates a single face. If the face is already a triangle,
// returns OK with cFacesCreated = 0. The ear-clipping plan is computed
// before any mutation, so DEGENERATE / LIMIT_EXCEEDED leave the mesh
// unchanged.
CYPHER_NODISCARD mesh_triangulate_result_t MeshOps_TriangulateFace(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace ) noexcept;

// ---------------------------------------------------------------------------
// Vertex grid snap (Gate 17)
// ---------------------------------------------------------------------------

// Result of a vertex snap pass.
struct mesh_snap_result_t {
    common::u32 cVerticesMoved{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Snaps all vertex positions to a uniform grid with the given spacing.
// Each coordinate is rounded to the nearest multiple of gridSpacing.
// gridSpacing must be finite and strictly positive. A spacing that would
// produce non-finite coordinates is rejected atomically. Face normals are
// recomputed after snapping.
CYPHER_NODISCARD mesh_snap_result_t MeshOps_SnapToGrid(
    editable_mesh_t *pMesh,
    common::f64 gridSpacing ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_TOPOLOGY_OPS_H
