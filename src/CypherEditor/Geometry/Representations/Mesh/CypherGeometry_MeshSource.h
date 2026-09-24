//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSource.h
//  Purpose: Declares the document-level Mesh source: an editable half-edge
//           mesh plus its authored attributes and the persistent source
//           identity of its elements, together with a flat canonical
//           description used for building, cloning, snapshots, undo, and
//           serialization.
//  Details: Why a separate wrapper: editable_mesh_t handles are pool slots
//           plus generations. They are exact while a mesh lives in memory,
//           but mean nothing after save/load, a snapshot rebuild, or an
//           undo that recreates the mesh. Selection, cook source mapping,
//           and transaction deltas need identity that survives those, so
//           it lives here as source IDs:
//
//             - the mesh root has a source ID;
//             - every vertex and every face has a source ID;
//             - an edge is identified by the unordered pair of its vertex
//               source IDs. Edges exist only between vertices, so this is
//               stable without allocating (and later retiring) an ID for
//               every edge an operation creates or dissolves.
//
//           Identity and attributes are generation-stamped sidecars indexed
//           by pool slot, so a slot reused after a deletion can never
//           inherit the previous occupant's ID or attributes.
//
//           Description (mesh_source_description_t) is the canonical flat
//           form: vertices, faces with corner runs, and edge attributes by
//           vertex pair. Building always goes description -> polygon soup
//           -> Sanitation (no weld, open boundaries allowed), so there is a
//           single validated construction path.
//
//           Canonical order: Describe() lists vertices and faces by
//           ascending source ID, starts every face at its lowest-ID vertex,
//           and sorts edge entries by vertex pair. The description is then a
//           function of identity, topology, and attributes only - not of
//           pool slot history - so two meshes are the same authored mesh
//           exactly when their descriptions are Equal. Build accepts any
//           order; building a canonical description and describing it again
//           reproduces it bit for bit.
//
//           Build rejects inputs Sanitation would silently alter: isolated
//           vertices (they would be dropped with their IDs), empty meshes,
//           and anything non-manifold. The mesh source never repairs; that
//           is Repair's job (ARCHITECTURE.md).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_Attributes_MeshStore.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Description limits. They bound the canonical form, which every document
// mesh passes through on save, snapshot, and undo.
inline constexpr common::u32 kMeshSourceVerticesMax = 65536u;
inline constexpr common::u32 kMeshSourceFacesMax = 65536u;
inline constexpr common::u32 kMeshSourceCornersMax = 262144u;
inline constexpr common::u32 kMeshSourceCornersPerFaceMax = 256u;
inline constexpr common::f64 kMeshSourceCoordinateMax = 1048576.0;

// ---------------------------------------------------------------------------
// Canonical description
// ---------------------------------------------------------------------------

struct mesh_source_vertex_t {
    math::vec3d_t position{};
    geometry_source_id_t sourceId{};
};

struct mesh_source_corner_t {
    common::u32 iVertex{ 0u };
    mesh_corner_attributes_t attributes{};
};

struct mesh_source_face_t {
    common::u32 iFirstCorner{ 0u };
    common::u32 cCorners{ 0u };
    geometry_source_id_t sourceId{};
    mesh_face_attributes_t attributes{};
};

// Edge data that is not default. Edges without an entry get default
// attributes and crease weight 0. iVertexA < iVertexB and creaseWeight is in
// the subdivision domain [0, 1].
struct mesh_source_edge_t {
    common::u32 iVertexA{ 0u };
    common::u32 iVertexB{ 0u };
    mesh_edge_attributes_t attributes{};
    common::f64 creaseWeight{ 0.0 };
};

struct mesh_source_description_t {
    common::vector_t<mesh_source_vertex_t> vertices{};
    common::vector_t<mesh_source_corner_t> corners{};
    common::vector_t<mesh_source_face_t> faces{};
    common::vector_t<mesh_source_edge_t> edges{};
    geometry_source_id_t sourceId{};
};

CYPHER_NODISCARD geometry_status_t MeshSourceDescription_Init(
    mesh_source_description_t *pDesc,
    const common::allocator_t *pAllocator,
    geometry_source_id_t meshId ) noexcept;

void MeshSourceDescription_Shutdown( mesh_source_description_t *pDesc ) noexcept;

CYPHER_NODISCARD bool MeshSourceDescription_IsInitialized(
    const mesh_source_description_t *pDesc ) noexcept;

// Keeps capacity; resets counts and the root ID to `meshId`.
void MeshSourceDescription_Clear(
    mesh_source_description_t *pDesc,
    geometry_source_id_t meshId ) noexcept;

// Convenience builders. They check indices and limits but not identity
// uniqueness (Build does, over the whole description).
CYPHER_NODISCARD geometry_status_t MeshSourceDescription_TryAddVertex(
    mesh_source_description_t *pDesc,
    math::vec3d_t position,
    geometry_source_id_t vertexId,
    common::u32 *pIndexOut ) noexcept;

CYPHER_NODISCARD geometry_status_t MeshSourceDescription_TryAddFace(
    mesh_source_description_t *pDesc,
    common::span_t<const common::u32> vertexIndices,
    geometry_source_id_t faceId,
    const mesh_face_attributes_t &attributes,
    common::u32 *pIndexOut ) noexcept;

CYPHER_NODISCARD geometry_status_t MeshSourceDescription_TrySetEdge(
    mesh_source_description_t *pDesc,
    common::u32 iVertexA,
    common::u32 iVertexB,
    const mesh_edge_attributes_t &attributes,
    common::f64 creaseWeight ) noexcept;

// Transfers ownership of every array from *pSrc to *pDst (whatever *pDst
// held is released first). *pSrc is left uninitialized. Never allocates.
void MeshSourceDescription_Move(
    mesh_source_description_t *pDst,
    mesh_source_description_t *pSrc ) noexcept;

// Replaces the contents of an initialized *pDst with a copy of *pSrc.
// Failure leaves *pDst cleared.
CYPHER_NODISCARD geometry_status_t MeshSourceDescription_TryCopy(
    mesh_source_description_t *pDst,
    const mesh_source_description_t *pSrc ) noexcept;

// Structural equality: same root ID, same vertices/faces/corners/edges in the
// same order with bit-identical values. Used to prove round trips.
CYPHER_NODISCARD bool MeshSourceDescription_Equal(
    const mesh_source_description_t *pA,
    const mesh_source_description_t *pB ) noexcept;

// ---------------------------------------------------------------------------
// Mesh source
// ---------------------------------------------------------------------------

struct mesh_identity_slot_t {
    common::u32 nGeneration{ 0u }; // 0 = no identity recorded for this slot
    geometry_source_id_t sourceId{};
};

struct mesh_source_t {
    editable_mesh_t mesh;
    mesh_attribute_store_t attributes{};
    common::vector_t<mesh_identity_slot_t> vertexIds{};
    common::vector_t<mesh_identity_slot_t> faceIds{};
    geometry_source_id_t sourceId{};
};

enum class mesh_source_fault_t : common::u8 {
    NONE = 0u,
    NOT_INITIALIZED,
    INVALID_ROOT_ID,
    MISSING_VERTEX_ID,
    MISSING_FACE_ID,
    DUPLICATE_SOURCE_ID,  // any two of root / vertex / face IDs collide
    INVALID_TOPOLOGY,     // half-edge structure fails MeshValidation
    INVALID_ATTRIBUTES,
    NON_FINITE,
    COORDINATE_RANGE,
    VALIDATION_INCOMPLETE // scratch allocation failed
};

struct mesh_source_validation_t {
    mesh_source_fault_t fault{ mesh_source_fault_t::NONE };
    geometry_source_id_t sourceId{}; // offending ID when applicable
};

// Builds a mesh source from a description into a canonical-empty *pOut.
// Validates the whole description first (IDs valid and unique across root,
// vertices, and faces; indices in range; no repeated vertex within a face;
// every vertex referenced; finite in-range positions; edge entries refer to
// real edges and are not repeated). On failure *pOut stays empty and
// *pFaultOut (optional) says why.
CYPHER_NODISCARD geometry_status_t MeshSource_TryBuild(
    const mesh_source_description_t *pDesc,
    const common::allocator_t *pAllocator,
    mesh_source_t *pOut,
    mesh_source_validation_t *pFaultOut = nullptr ) noexcept;

void MeshSource_Shutdown( mesh_source_t *pSource ) noexcept;

CYPHER_NODISCARD bool MeshSource_IsInitialized( const mesh_source_t *pSource ) noexcept;

// Writes the canonical description of *pSource into an initialized *pDesc
// (cleared first). Fails with CORRUPT_STATE if an element lacks identity.
CYPHER_NODISCARD geometry_status_t MeshSource_TryDescribe(
    const mesh_source_t *pSource,
    mesh_source_description_t *pDesc ) noexcept;

// Deep copy (Describe + Build). The copy's handles are compact and differ
// from the source's; identity and attributes are preserved exactly.
CYPHER_NODISCARD geometry_status_t MeshSource_TryClone(
    const mesh_source_t *pSource,
    const common::allocator_t *pAllocator,
    mesh_source_t *pOut ) noexcept;

// Identity access. Invalid or stale handles return
// GEOMETRY_SOURCE_ID_INVALID.
CYPHER_NODISCARD geometry_source_id_t MeshSource_VertexId(
    const mesh_source_t *pSource, geometry_mesh_vertex_handle_t hVertex ) noexcept;
CYPHER_NODISCARD geometry_source_id_t MeshSource_FaceId(
    const mesh_source_t *pSource, geometry_mesh_face_handle_t hFace ) noexcept;

// Handle lookup by ID (linear in the pool size).
CYPHER_NODISCARD bool MeshSource_TryFindVertex(
    const mesh_source_t *pSource, geometry_source_id_t id, geometry_mesh_vertex_handle_t *pOut ) noexcept;
CYPHER_NODISCARD bool MeshSource_TryFindFace(
    const mesh_source_t *pSource, geometry_source_id_t id, geometry_mesh_face_handle_t *pOut ) noexcept;

// Records an ID for a live element (used by edit operations after they
// create topology). Rejects invalid IDs; does not check uniqueness (Validate
// does, and the document registry does on commit).
CYPHER_NODISCARD geometry_status_t MeshSource_TrySetVertexId(
    mesh_source_t *pSource, geometry_mesh_vertex_handle_t hVertex, geometry_source_id_t id ) noexcept;
CYPHER_NODISCARD geometry_status_t MeshSource_TrySetFaceId(
    mesh_source_t *pSource, geometry_mesh_face_handle_t hFace, geometry_source_id_t id ) noexcept;

// Gives every live vertex and face that has no identity a fresh ID from
// *pIdAllocator, vertices first, each in slot order (deterministic).
// Failure-atomic including the allocator. *pAssignedOut (optional) counts
// the IDs handed out.
CYPHER_NODISCARD geometry_status_t MeshSource_TryAssignMissingIds(
    mesh_source_t *pSource,
    geometry_source_id_allocator_t *pIdAllocator,
    common::u32 *pAssignedOut ) noexcept;

// Appends every source ID the mesh owns (root, vertices, faces) to *pOut in
// ascending order. Fails with CORRUPT_STATE if an element lacks identity.
CYPHER_NODISCARD geometry_status_t MeshSource_TryCollectSourceIds(
    const mesh_source_t *pSource,
    common::vector_t<geometry_source_id_t> *pOut ) noexcept;

// Full validation: topology, identity completeness and uniqueness,
// attribute consistency, finite in-range positions. Reports the first fault.
CYPHER_NODISCARD mesh_source_validation_t MeshSource_Validate(
    const mesh_source_t *pSource,
    const common::allocator_t *pScratchAllocator ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_H
