//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshAttributeTransfer.h
//  Purpose: Declares the capture/resolve engine that carries face, corner,
//           and edge attributes (and optionally face identity) across a
//           topology edit on a mesh source.
//  Details: Topology operations know how to rewire half-edges; they do not
//           know about UVs, materials, hard edges, or source IDs. Rather
//           than teach every operation about every channel, an edit is
//           bracketed:
//
//             capture  - before the op, record every face (ID, attributes,
//                        plane), every corner keyed by (face, origin
//                        vertex), and every edge keyed by its vertex pair;
//             op       - any MeshOps_* call;
//             resolve  - after the op, fill in what the op could not:
//
//           Faces:   a surviving face keeps its attributes. A new face takes
//                    its parent's attributes (and, when the op says so, its
//                    parent's source ID - e.g. an extruded cap *is* the
//                    original face, moved). Parents come from the caller's
//                    map, else from the pre-op face sharing the most
//                    vertices with the new face (ties: closest plane, then
//                    lowest ID).
//           Corners: a (face, vertex) pair that existed before gets its
//                    exact old value back - this also repairs corners whose
//                    half-edge was rewired or reused by the op. A new corner
//                    copies the parent face's corner at the same vertex,
//                    or the parent corner at the exact same position (a
//                    duplicated vertex); failing that its UVs come from the
//                    least-squares
//                    affine map (position -> UV) fitted over the parent's
//                    corners, which reproduces planar UV layouts exactly
//                    (a vertex inserted on an edge gets the interpolated
//                    UV). Colors come from the nearest parent corner.
//           Edges:   an edge between the same two vertices as before keeps
//                    its flags and crease. A new edge running along an old
//                    edge from one of its endpoints (one half of a split
//                    edge) inherits that edge's flags and crease. Any other
//                    new edge is default.
//
//           Everything is deterministic: pool order, sorted keys, and
//           explicit tie-breaks. Vertex identity needs no transfer: a
//           surviving vertex keeps its slot, new vertices receive fresh IDs
//           at commit (MeshSource_TryAssignMissingIds).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_ATTRIBUTE_TRANSFER_H
#define CYPHER_EDITOR_GEOMETRY_MESH_ATTRIBUTE_TRANSFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSource.h"

namespace cypher::editor::geometry
{

struct mesh_edit_face_record_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_source_id_t sourceId{};
    mesh_face_attributes_t attributes{};
    math::vec3d_t normal{};
    math::vec3d_t centroid{};
    common::u32 iFirstCorner{ 0u };
    common::u32 cCorners{ 0u };
};

struct mesh_edit_corner_record_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_vertex_handle_t hVertex{};
    math::vec3d_t position{};
    mesh_corner_attributes_t attributes{};
    bool bHasAttributes{ false };
};

struct mesh_edit_edge_record_t {
    geometry_mesh_vertex_handle_t hA{}; // lower handle key first
    geometry_mesh_vertex_handle_t hB{};
    geometry_source_id_t idA{};         // vertex IDs at capture time, so
    geometry_source_id_t idB{};         // lineage can name removed vertices
    math::vec3d_t positionA{};
    math::vec3d_t positionB{};
    mesh_edge_attributes_t attributes{};
    common::f64 creaseWeight{ 0.0 };
};

struct mesh_edit_capture_t {
    common::vector_t<mesh_edit_face_record_t> faces{};     // pool order
    common::vector_t<mesh_edit_corner_record_t> corners{}; // runs per face
    common::vector_t<common::u32> cornerOrder{};           // corners sorted by (face, vertex)
    common::vector_t<common::u32> vertexOrder{};           // corners sorted by (vertex, face)
    common::vector_t<mesh_edit_edge_record_t> edges{};     // sorted by (hA, hB)
    common::vector_t<common::u32> edgeEnds{};              // (edge << 1 | end) sorted by endpoint
    bool bCaptured{ false };
};

// Parent assignment for a face created by the op.
struct mesh_edit_face_parent_t {
    geometry_mesh_face_handle_t hNewFace{};
    geometry_mesh_face_handle_t hParent{}; // a face handle from the capture
    bool bInheritIdentity{ false };        // new face takes the parent's source ID
};

struct mesh_edit_resolve_stats_t {
    common::u32 cFacesFromParent{ 0u };
    common::u32 cFacesInheritingIdentity{ 0u };
    common::u32 cFacesWithoutParent{ 0u };
    common::u32 cCornersRestored{ 0u };   // same (face, vertex) as before
    common::u32 cCornersCopied{ 0u };     // parent's corner at the same vertex
    common::u32 cCornersInterpolated{ 0u };
    common::u32 cCornersDefaulted{ 0u };
    common::u32 cEdgesRestored{ 0u };
    common::u32 cEdgesInherited{ 0u };
};

// ---------------------------------------------------------------------------
// Provenance (lineage of elements an edit created or merged)
// ---------------------------------------------------------------------------

// A face created by the edit and the pre-edit face it derives from.
struct mesh_edit_face_origin_t {
    geometry_mesh_face_handle_t hFace{};  // post-edit handle
    geometry_source_id_t parentFaceId{};
};

// A new edge that runs along a pre-edit edge (one half of a split edge).
struct mesh_edit_edge_origin_t {
    geometry_mesh_vertex_handle_t hA{};   // post-edit endpoints
    geometry_mesh_vertex_handle_t hB{};
    geometry_source_id_t parentA{};       // pre-edit edge, by vertex IDs
    geometry_source_id_t parentB{};
};

// A vertex removed by merging into another (collapse, weld).
struct mesh_edit_vertex_merge_t {
    geometry_source_id_t removedId{};
    geometry_mesh_vertex_handle_t hSurvivor{};
};

// Handle-based lineage of one edit, valid on the edited source until its
// next topology change. Convert to IDs with MeshEditProvenance_TryToLineage
// (MeshSelection.h) once new elements have IDs.
struct mesh_edit_provenance_t {
    common::vector_t<mesh_edit_face_origin_t> faces{};
    common::vector_t<mesh_edit_edge_origin_t> edges{};
    common::vector_t<mesh_edit_vertex_merge_t> merges{};
};

CYPHER_NODISCARD geometry_status_t MeshEditProvenance_Init(
    mesh_edit_provenance_t *pProvenance,
    const common::allocator_t *pAllocator ) noexcept;

void MeshEditProvenance_Shutdown( mesh_edit_provenance_t *pProvenance ) noexcept;

void MeshEditProvenance_Clear( mesh_edit_provenance_t *pProvenance ) noexcept;

// What an identity-addressed edit reports back. pProvenance is optional and
// caller-owned: set it (initialized) before the call to receive lineage;
// the edit clears it first.
struct mesh_edit_report_t {
    mesh_edit_resolve_stats_t stats{};
    mesh_edit_provenance_t *pProvenance{ nullptr };
};

CYPHER_NODISCARD geometry_status_t MeshEditCapture_Init(
    mesh_edit_capture_t *pCapture,
    const common::allocator_t *pAllocator ) noexcept;

void MeshEditCapture_Shutdown( mesh_edit_capture_t *pCapture ) noexcept;

// Records the pre-edit state of *pSource (replacing any earlier capture).
// Faces must all have identity (the source must be describable).
CYPHER_NODISCARD geometry_status_t MeshEditCapture_TryCapture(
    mesh_edit_capture_t *pCapture,
    const mesh_source_t *pSource ) noexcept;

// Fills attributes (and inherited face identity) on *pSource after an op,
// as described above. `parents` may be empty. A parent entry naming a face
// the capture does not contain is INVALID_ARGUMENT (checked before any
// write). Allocation failure part-way leaves topology as the op left it and
// some new elements with default attributes; callers work on a
// transaction's working copy and cancel on failure.
CYPHER_NODISCARD geometry_status_t MeshEditCapture_TryResolve(
    const mesh_edit_capture_t *pCapture,
    mesh_source_t *pSource,
    common::span_t<const mesh_edit_face_parent_t> parents,
    mesh_edit_resolve_stats_t *pStatsOut,
    mesh_edit_provenance_t *pProvenanceOut = nullptr ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_ATTRIBUTE_TRANSFER_H
