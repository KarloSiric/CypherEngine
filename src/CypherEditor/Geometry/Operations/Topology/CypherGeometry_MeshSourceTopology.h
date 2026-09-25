//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceTopology.h
//  Purpose: Declares identity-addressed topology edits on a mesh source:
//           split, collapse, dissolve, weld, fill, bridge, detach, and flip,
//           each carrying attributes and identity through the change.
//  Details: Why address by source ID: callers (tools, selection, scripts,
//           undo) hold persistent IDs, not pool handles. An edge is named by
//           its two vertex IDs, matching MeshSource's edge identity.
//
//           Every edit brackets the raw MeshOps_* call with
//           MeshEditCapture (see MeshAttributeTransfer.h), so UVs,
//           materials, hard/seam flags, and creases follow the rules there.
//           New vertices and faces are left without an ID; they receive
//           fresh IDs from the document registry when the transaction
//           commits. The raw ops are failure-atomic, so a failed edit leaves
//           the mesh as it was; an allocation failure while resolving
//           attributes is reported and the caller cancels the transaction.
//
//           All functions return NOT_INITIALIZED for an unusable source,
//           INVALID_HANDLE when an ID does not name a live element, and
//           otherwise the underlying op's status.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_TOPOLOGY_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_TOPOLOGY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshAttributeTransfer.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshKnife.h"

namespace cypher::editor::geometry
{

// Resolves the live edge between two vertex IDs (either orientation).
CYPHER_NODISCARD bool MeshSourceEdit_TryFindEdge(
    const mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    geometry_mesh_edge_handle_t *pEdgeOut ) noexcept;

// Inserts a vertex on edge (a, b) at t in (0, 1) measured from a. Both
// halves keep the edge's flags and crease; corners at the new vertex get
// interpolated UVs. *pNewVertexOut (optional) receives the new vertex.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySplitEdge(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    common::f64 t,
    geometry_mesh_vertex_handle_t *pNewVertexOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// Splits face `faceId` along a new edge between two of its non-adjacent
// vertices. The original face keeps its ID on one side; the other side is a
// new face with the original's attributes.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySplitFace(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    mesh_edit_report_t *pReportOut ) noexcept;

// Merges `removeId` into `keepId` along their shared edge; `keepId`
// survives with its ID. Faces that become degenerate are removed.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryCollapseEdge(
    mesh_source_t *pSource,
    geometry_source_id_t keepId,
    geometry_source_id_t removeId,
    mesh_edit_report_t *pReportOut ) noexcept;

// Removes interior edge (a, b), merging its two faces into one.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryDissolveEdge(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    mesh_edit_report_t *pReportOut ) noexcept;

// Welds vertices closer than `tolerance` that share an edge.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryWeld(
    mesh_source_t *pSource,
    common::f64 tolerance,
    common::u32 *pMergedOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// Closes the boundary loop through boundary edge (a, b) with one new face.
// The new face takes the attributes of the face across edge (a, b).
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryFillHole(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    mesh_edit_report_t *pReportOut ) noexcept;

// Bridges the boundary loops through boundary edges (a0, b0) and (a1, b1).
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryBridge(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA0,
    geometry_source_id_t vertexB0,
    geometry_source_id_t vertexA1,
    geometry_source_id_t vertexB1,
    common::u32 *pFacesCreatedOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// Deletes the faces, opening the mesh: edges shared with surviving faces
// become boundary edges; edges and vertices no surviving face uses are
// removed (their IDs retire at commit). Deleting every face is DEGENERATE.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryDeleteFaces(
    mesh_source_t *pSource,
    common::span_t<const geometry_source_id_t> faceIds,
    mesh_edit_report_t *pReportOut ) noexcept;

// Extrudes boundary edges by `offset` (Hammer's edge pull). Edges are given
// as vertex-ID pairs: edgeVertexIds = { a0, b0, a1, b1, ... }. Each edge
// gets a new quad; edges sharing a vertex share the new vertex, so a chain
// becomes one strip. New quads take the attributes (and continue the UV
// layout) of the face the edge belonged to. pOuterEdgesOut (optional,
// initialized) receives the new outer boundary edges, valid until the next
// topology change - assign IDs (GeometryMeshTransaction_TryAssignIds) to
// address them by ID for the next pull.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryExtrudeEdges(
    mesh_source_t *pSource,
    common::span_t<const geometry_source_id_t> edgeVertexIds,
    math::vec3d_t offset,
    common::vector_t<geometry_mesh_edge_handle_t> *pOuterEdgesOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// One corner for MeshSourceEdit_TryAddFace: an existing vertex (valid
// vertexId) or a new vertex at `position` (invalid vertexId).
struct mesh_edit_corner_t {
    geometry_source_id_t vertexId{};
    math::vec3d_t position{};
};

// Adds one face (the Poly Pen primitive): corners in winding order, each an
// existing vertex or a new point. Edges along existing boundary edges are
// stitched; the face takes the attributes of the neighbour it shares the
// most vertices with (none: default attributes). Winding that conflicts with
// the neighbours, a third face on an edge, or a second open fan at a vertex
// is NON_MANIFOLD; see MeshBoundary_AddFaces. *pNewFaceOut (optional)
// receives the new face.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryAddFace(
    mesh_source_t *pSource,
    common::span_t<const mesh_edit_corner_t> corners,
    geometry_mesh_face_handle_t *pNewFaceOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// Detaches the given faces into their own shell by duplicating boundary
// vertices. Face IDs are unchanged.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryDetachFaces(
    mesh_source_t *pSource,
    common::span_t<const geometry_source_id_t> faceIds,
    mesh_edit_report_t *pReportOut ) noexcept;

// Flips the given faces (see MeshBoundary_FlipFaces): a partial selection is
// detached first, so edges to unflipped faces become boundary edges. Face
// IDs and each (face, vertex) corner's UVs and color are unchanged.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryFlipFaces(
    mesh_source_t *pSource,
    common::span_t<const geometry_source_id_t> faceIds,
    mesh_edit_report_t *pReportOut ) noexcept;

// One knife point addressed by source identity (see mesh_knife_point_t).
//   VERTEX: vertexId.
//   EDGE:   the edge between vertexId and otherVertexId, at t in (0, 1)
//           measured from vertexId.
//   FACE:   faceId and a position inside that face.
struct mesh_edit_knife_point_t {
    mesh_knife_point_kind_t kind{ mesh_knife_point_kind_t::VERTEX };
    geometry_source_id_t vertexId{};
    geometry_source_id_t otherVertexId{};
    common::f64 t{ 0.5 };
    geometry_source_id_t faceId{};
    math::vec3d_t position{};
};

// Knife cut through the source (see MeshKnife_Cut for the path rules and
// rejections). Every piece of a cut face inherits that face's attributes;
// the piece that keeps the face record keeps its ID. Split edges keep their
// flags and crease on both halves. pPointVerticesOut (optional, initialized)
// receives one vertex per path point.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryKnife(
    mesh_source_t *pSource,
    common::span_t<const mesh_edit_knife_point_t> path,
    common::vector_t<geometry_mesh_vertex_handle_t> *pPointVerticesOut,
    mesh_edit_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_TOPOLOGY_H
