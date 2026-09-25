//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBoundaryOps.h
//  Purpose: Declares open-boundary topology operations on the editable
//           half-edge mesh: deleting faces to make openings, filling holes,
//           extruding boundary edges (the Hammer edge-pull workflow),
//           detaching faces into their own shell, flipping faces, adding
//           faces (Poly Pen), bridging two boundary loops, and replacing a
//           set of faces in one failure-atomic step (the primitive bevel
//           builds on).
//  Details: Boundary convention (the same one Sanitation builds): a
//           half-edge on the mesh boundary has no twin (invalid hTwin), and
//           its edge record points at it. A boundary vertex stores the
//           outgoing half-edge that starts its open fan - the one whose
//           previous half-edge has no twin - because fan walks rotate
//           h -> next(twin(h)) and only cover the whole fan from there.
//
//           Why these live outside MeshTopologyOps: the operations there
//           assume closed reciprocal topology throughout. Opening and closing
//           boundaries is a different contract - it changes which edges have
//           twins and can split or merge shells - so it gets its own module
//           with that contract stated once.
//
//           Every operation is failure-atomic: it validates, computes the
//           exact number of new records, simulates the resulting shell
//           components, and reserves every pool and scratch buffer before
//           the first mutation. After that point nothing can fail, so an
//           error leaves the mesh untouched (backing capacity may remain
//           grown). Shells are recomputed after each operation, because
//           opening a mesh can split a shell and bridging or filling can
//           join two.
//
//           Identity and attributes are not handled here; the source-level
//           wrappers (MeshSourceTopology.h) bracket these ops with the
//           attribute-transfer engine.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_BOUNDARY_OPS_H
#define CYPHER_EDITOR_GEOMETRY_MESH_BOUNDARY_OPS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Largest boundary loop FillHole accepts (matches the face corner limit of
// mesh sources, so every filled face stays describable).
inline constexpr common::u32 kMeshBoundaryLoopMax = 256u;

// True when the edge has exactly one face (its half-edge has no twin).
CYPHER_NODISCARD bool MeshBoundary_IsBoundaryEdge(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept;

// Number of boundary edges in the mesh (0 for a closed mesh).
CYPHER_NODISCARD common::usize MeshBoundary_CountBoundaryEdges( const editable_mesh_t *pMesh ) noexcept;

// ---------------------------------------------------------------------------
// Delete faces
// ---------------------------------------------------------------------------

struct mesh_boundary_delete_result_t {
    common::u32 cFacesRemoved{ 0u };
    common::u32 cEdgesRemoved{ 0u };    // edges no face used any more
    common::u32 cVerticesRemoved{ 0u }; // vertices no face used any more
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Removes the faces. Edges shared with a surviving face become boundary
// edges; edges and vertices no surviving face uses are removed too (an
// isolated vertex or dangling edge is not valid mesh content). Deleting
// every face of the mesh is rejected with DEGENERATE: an empty mesh has no
// meaning as a source. Duplicate or stale handles are INVALID_HANDLE
// (checked first). A deletion that would leave a vertex with two separate
// fans (a bow-tie: two surviving sectors touching only at the vertex) is
// NON_MANIFOLD. Implemented as MeshBoundary_ReplaceFaces with nothing added.
CYPHER_NODISCARD mesh_boundary_delete_result_t MeshBoundary_DeleteFaces(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces ) noexcept;

// ---------------------------------------------------------------------------
// Fill hole
// ---------------------------------------------------------------------------

struct mesh_boundary_fill_result_t {
    geometry_mesh_face_handle_t hFace{};
    common::u32 cSides{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Closes the boundary loop containing boundary edge hEdge with one new face
// whose winding is consistent with its neighbours. Every vertex on the loop
// must have exactly one incoming boundary edge (otherwise the loop is
// ambiguous: NON_MANIFOLD). A loop longer than kMeshBoundaryLoopMax is
// LIMIT_EXCEEDED; a loop whose new face would have zero area is DEGENERATE.
CYPHER_NODISCARD mesh_boundary_fill_result_t MeshBoundary_FillHole(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept;

// ---------------------------------------------------------------------------
// Extrude boundary edges
// ---------------------------------------------------------------------------

struct mesh_boundary_extrude_result_t {
    common::u32 cFacesCreated{ 0u };
    common::u32 cVerticesCreated{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Pulls boundary edges out by `offset`: each edge (a, b) gets a quad
// (b, a, a + offset, b + offset) attached along it, with winding matching
// the face the edge belongs to. Edges that share a vertex share the new
// vertex and the side edge between their quads, so a selected chain or loop
// extrudes as one connected strip. The new outer edges are the boundary
// afterwards, which is what makes repeated extrusion possible;
// pOuterEdgesOut (optional, initialized) receives them in input order.
// Every edge must be a boundary edge; duplicates are INVALID_ARGUMENT; a
// zero or non-finite offset is DEGENERATE / NUMERIC_FAILURE.
CYPHER_NODISCARD mesh_boundary_extrude_result_t MeshBoundary_ExtrudeEdges(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_edge_handle_t> edges,
    math::vec3d_t offset,
    common::vector_t<geometry_mesh_edge_handle_t> *pOuterEdgesOut ) noexcept;

// ---------------------------------------------------------------------------
// Detach faces
// ---------------------------------------------------------------------------

struct mesh_boundary_detach_result_t {
    common::u32 cVerticesDuplicated{ 0u };
    common::u32 cEdgesCut{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Separates the faces from the rest of the mesh: every vertex they share
// with an unselected face is duplicated (the copy goes with the selected
// faces), and every edge between a selected and an unselected face is cut
// into two boundary edges (crease weight copied to both). Afterwards the
// selection forms its own shell(s). Selecting every face is a no-op.
CYPHER_NODISCARD mesh_boundary_detach_result_t MeshBoundary_DetachFaces(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces ) noexcept;

// ---------------------------------------------------------------------------
// Flip faces
// ---------------------------------------------------------------------------

struct mesh_boundary_flip_result_t {
    common::u32 cFacesFlipped{ 0u };
    common::u32 cVerticesDuplicated{ 0u }; // by the detach of a partial selection
    common::u32 cEdgesCut{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Reverses the winding (and so the normal) of the faces - Hammer's Flip.
// Two faces sharing an edge must run along it in opposite directions, so a
// flipped face cannot stay joined to a face that keeps its winding: the
// selection is first detached (MeshBoundary_DetachFaces), and every edge
// between a flipped and a kept face becomes two boundary edges. Selecting
// whole shells cuts nothing. Faces keep their handles, so per-corner values
// stay on the same (face, vertex) corners: seen from the new front side the
// texture is mirrored (re-project it to undo that). Stale or repeated faces
// are INVALID_HANDLE. Only the detach can fail, so the flip is
// failure-atomic.
CYPHER_NODISCARD mesh_boundary_flip_result_t MeshBoundary_FlipFaces(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces ) noexcept;

// ---------------------------------------------------------------------------
// Add faces (Poly Pen building block) and bridge
// ---------------------------------------------------------------------------

// One corner of a face to add: an existing vertex, or the index of a new
// vertex in the batch's `newPositions` (so several new faces can share a
// new vertex).
struct mesh_boundary_corner_t {
    geometry_mesh_vertex_handle_t hVertex{}; // used when iNew == CY_INVALID_INDEX
    common::u32 iNew{ CY_INVALID_INDEX };
};

struct mesh_boundary_add_result_t {
    common::u32 cFacesCreated{ 0u };
    common::u32 cVerticesCreated{ 0u };
    common::u32 cEdgesJoined{ 0u }; // new half-edges twinned to existing boundary
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Adds faces given as corner rings (faceCorners holds every ring back to
// back; faceSizes the corner count of each). A ring edge whose reverse is an
// existing boundary half-edge is stitched to it; ring edges shared between
// two new faces are stitched to each other; every other edge is a new
// boundary edge. Rejected (mesh unchanged):
//   - fewer than 3 corners, a repeated corner, bad handles/indices, non-
//     finite new positions -> INVALID_ARGUMENT / INVALID_HANDLE /
//     NUMERIC_FAILURE;
//   - an edge that already exists in the same direction (winding conflict),
//     an edge that would get a third face, or a vertex that would end up
//     with two open fans -> NON_MANIFOLD;
//   - a face with zero area -> DEGENERATE.
// Every vertex must end with exactly one fan (so, for example, a closed
// group of new faces touching an open rim only at a vertex is also
// NON_MANIFOLD). Implemented as MeshBoundary_ReplaceFaces with nothing
// removed. pNewFacesOut (optional, initialized) receives the new faces in
// ring order.
CYPHER_NODISCARD mesh_boundary_add_result_t MeshBoundary_AddFaces(
    editable_mesh_t *pMesh,
    common::span_t<const mesh_boundary_corner_t> faceCorners,
    common::span_t<const common::u32> faceSizes,
    common::span_t<const math::vec3d_t> newPositions,
    common::vector_t<geometry_mesh_face_handle_t> *pNewFacesOut ) noexcept;

// Connects the boundary loops through boundary edges hEdgeA and hEdgeB with
// a ring of quads. The loops must be distinct and equally long; they are
// walked in opposite directions so every quad's winding agrees with both
// sides, and the chosen edges' endpoints are paired crosswise (A's start
// with B's end), which is the pairing that keeps the first quad planar-
// convex for facing loops.
CYPHER_NODISCARD mesh_boundary_add_result_t MeshBoundary_Bridge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdgeA,
    geometry_mesh_edge_handle_t hEdgeB,
    common::vector_t<geometry_mesh_face_handle_t> *pNewFacesOut ) noexcept;

// ---------------------------------------------------------------------------
// Replace faces (the general local-rebuild primitive)
// ---------------------------------------------------------------------------

struct mesh_boundary_replace_result_t {
    common::u32 cFacesRemoved{ 0u };
    common::u32 cFacesCreated{ 0u };
    common::u32 cVerticesCreated{ 0u };
    common::u32 cVerticesRemoved{ 0u }; // existing vertices no face uses any more
    common::u32 cEdgesRemoved{ 0u };    // existing edges no face uses any more
    common::u32 cEdgesJoined{ 0u };     // new half-edges twinned to surviving ones
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Removes `removeFaces` and adds the given corner rings in one failure-
// atomic step: the post-edit mesh is validated as a whole before anything
// changes. This is what operations that rebuild a region (bevel, and
// later local remeshing) are built on - deleting and then adding would
// leave a half-edited mesh whenever the add is rejected.
//
// Rings use the AddFaces conventions (existing vertices by handle, new
// vertices by index into newPositions). A ring edge stitches to a surviving
// half-edge running the other way if that half-edge is on the boundary
// after the removal; ring edges shared between two new faces stitch to each
// other. Existing vertices and edges that no face uses afterwards are
// removed. Rejected (mesh unchanged):
//   - malformed rings, bad handles, duplicate removals, non-finite or
//     unused new positions -> INVALID_ARGUMENT / INVALID_HANDLE /
//     NUMERIC_FAILURE; nothing to do, or removing every face and adding
//     none -> INVALID_ARGUMENT / DEGENERATE;
//   - a ring whose area is at or below the source's minimum face area ->
//     DEGENERATE;
//   - any vertex whose faces afterwards do not form exactly one fan (a
//     winding conflict, a third face on an edge, a bowtie left by the
//     removal, a new face touching a closed fan) -> NON_MANIFOLD.
// pNewFacesOut (optional, initialized) receives the new faces in ring order.
CYPHER_NODISCARD mesh_boundary_replace_result_t MeshBoundary_ReplaceFaces(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> removeFaces,
    common::span_t<const mesh_boundary_corner_t> faceCorners,
    common::span_t<const common::u32> faceSizes,
    common::span_t<const math::vec3d_t> newPositions,
    common::vector_t<geometry_mesh_face_handle_t> *pNewFacesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_BOUNDARY_OPS_H
