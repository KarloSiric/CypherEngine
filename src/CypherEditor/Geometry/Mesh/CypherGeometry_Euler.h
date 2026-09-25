//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Euler.h
//  Purpose: Declares the Euler operators: the minimal split/join
//           topology edits of the editable mesh, each with checked
//           preconditions, a guaranteed element-count change, and an exact
//           inverse.
//  Details: Every larger mesh edit (loop cut, knife, bevel, collapse) is,
//           in the end, a sequence of these four moves. Stating them once
//           with their contracts gives those edits something provable to
//           stand on, and gives tests a precise oracle.
//
//             operator            name  dV  dE  dF   inverse
//             split edge          SEMV  +1  +1   0   JEKV
//             join edges          JEKV  -1  -1   0   SEMV
//             split face          MEF    0  +1  +1   KEF
//             join faces          KEF    0  -1  -1   MEF
//
//           (SEMV = split edge, make vertex; JEKV = join edges, kill
//           vertex; MEF = make edge and face; KEF = kill edge and face.)
//           None of them changes V - E + F or the shell count.
//
//           Contract for every operator:
//             - Preconditions are checked before anything changes; a
//               violation returns a precise status and leaves the mesh
//               exactly as it was (the Euler_Check* functions run the same
//               checks without editing).
//             - On success the counts changed by exactly the table row,
//               every killed element's handle is stale, every surviving
//               element keeps its handle, and the touched loops are closed
//               with their cached counts. The operator verifies this before
//               returning; a violation would be CORRUPT_STATE (a bug, never
//               an input error). bFullValidation additionally runs the
//               whole-mesh MeshValidation pass.
//             - Faces keep their handles through SEMV and JEKV; through MEF
//               the split face keeps its handle for one side and the other
//               side is the new face; through KEF one of the two faces
//               survives (reported) and the other is killed.
//
//           These act on the raw editable mesh. Identity and attribute
//           transfer for authored meshes is the mesh-source edit layer's
//           job (MeshEdit_Bracket), which wraps topology edits like these.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_EULER_H
#define CYPHER_EDITOR_GEOMETRY_EULER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

struct euler_counts_t {
    common::i64 cVertices{ 0 };
    common::i64 cEdges{ 0 };
    common::i64 cFaces{ 0 };
    common::i64 cShells{ 0 };
};

enum class euler_operator_t : common::u8 {
    SEMV = 0u,
    JEKV,
    MEF,
    KEF
};

CYPHER_NODISCARD euler_counts_t Euler_Counts( const editable_mesh_t *pMesh ) noexcept;
// The count change an operator guarantees (a row of the table above).
CYPHER_NODISCARD euler_counts_t Euler_Delta( euler_operator_t op ) noexcept;
CYPHER_NODISCARD euler_operator_t Euler_Inverse( euler_operator_t op ) noexcept;

// ---------------------------------------------------------------------------
// SEMV: split an edge at parameter t in (0, 1) from its representative
// half-edge's origin; boundary edges included. Preconditions: live edge (INVALID_HANDLE), finite t
// strictly inside (0, 1) (INVALID_ARGUMENT), an edge of non-zero length
// (DEGENERATE), room in the pools (LIMIT_EXCEEDED).
// ---------------------------------------------------------------------------

struct euler_semv_result_t {
    geometry_mesh_vertex_handle_t hNewVertex{};
    geometry_mesh_edge_handle_t hNewEdge{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

CYPHER_NODISCARD geometry_status_t Euler_CheckSplitEdge(
    const editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge, common::f64 t ) noexcept;
CYPHER_NODISCARD euler_semv_result_t Euler_SplitEdge(
    editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge, common::f64 t, bool bFullValidation = false ) noexcept;

// ---------------------------------------------------------------------------
// JEKV: remove a vertex that has exactly two edges, joining them into one.
// Preconditions, in the order checked: live vertex (INVALID_HANDLE);
// exactly two incident edges and one loop on each side (INVALID_ARGUMENT /
// NON_MANIFOLD); every face through the vertex keeps at least three
// corners and non-zero area (DEGENERATE); its two neighbours not already
// joined by an edge (NON_MANIFOLD - the join would duplicate it). Works
// on boundary vertices too. The edge with the lower handle key survives,
// now spanning both.
// ---------------------------------------------------------------------------

struct euler_jekv_result_t {
    geometry_mesh_edge_handle_t hSurvivingEdge{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

CYPHER_NODISCARD geometry_status_t Euler_CheckJoinEdges(
    const editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t hVertex ) noexcept;
CYPHER_NODISCARD euler_jekv_result_t Euler_JoinEdges(
    editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t hVertex, bool bFullValidation = false ) noexcept;

// ---------------------------------------------------------------------------
// MEF: split a face with a new edge between two of its corners.
// Preconditions: live face and vertices (INVALID_HANDLE); two distinct
// corners of that face that are not neighbours on it (INVALID_ARGUMENT);
// the corners not already joined by an edge elsewhere (NON_MANIFOLD);
// room in the pools (LIMIT_EXCEEDED); both halves of non-zero area
// (DEGENERATE).
// ---------------------------------------------------------------------------

struct euler_mef_result_t {
    geometry_mesh_edge_handle_t hNewEdge{};
    geometry_mesh_face_handle_t hNewFace{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

CYPHER_NODISCARD geometry_status_t Euler_CheckSplitFace(
    const editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace, geometry_mesh_vertex_handle_t hA,
    geometry_mesh_vertex_handle_t hB ) noexcept;
CYPHER_NODISCARD euler_mef_result_t Euler_SplitFace(
    editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace, geometry_mesh_vertex_handle_t hA, geometry_mesh_vertex_handle_t hB,
    bool bFullValidation = false ) noexcept;

// ---------------------------------------------------------------------------
// KEF: remove an edge between two different faces, merging them.
// Preconditions: live edge (INVALID_HANDLE); two faces on it, i.e. not a
// boundary edge (INVALID_ARGUMENT); two different faces that share no
// other edge or corner (NON_MANIFOLD - the merged loop would touch itself).
// ---------------------------------------------------------------------------

struct euler_kef_result_t {
    geometry_mesh_face_handle_t hSurvivingFace{};
    geometry_mesh_face_handle_t hKilledFace{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

CYPHER_NODISCARD geometry_status_t Euler_CheckJoinFaces(
    const editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge ) noexcept;
CYPHER_NODISCARD euler_kef_result_t Euler_JoinFaces(
    editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge, bool bFullValidation = false ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_EULER_H
