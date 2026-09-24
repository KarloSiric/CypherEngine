//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_MeshStore.h
//  Purpose: Declares sidecar attribute storage for an EditableMesh:
//           per-corner UVs and colour, per-face material and smoothing
//           groups, per-edge surface flags.
//  Details: Attributes live beside the mesh, not inside its pool records,
//           for the same reason the schema keeps materials opaque: topology
//           records stay small and trivially copyable, and a mesh can exist
//           (e.g. mid-CSG) without any surfacing at all.
//
//           Each domain is an array indexed by pool slot and stamped with
//           the handle generation that wrote it. A lookup whose generation
//           does not match returns the domain default. Consequences, all
//           deliberate:
//             - a value belongs to exactly one handle: when a removed
//               element's slot is reused, the new element (new generation)
//               does not inherit the old element's attributes;
//             - elements created by a topology edit read defaults until an
//               Attributes/Propagation rule writes them;
//             - the store never needs to be told about topology edits.
//           Getters do not consult the mesh, so a *stale* handle whose slot
//           has not been reused still reads its old value; callers holding
//           handles across edits must check liveness against the mesh
//           (CountLive and Validate do).
//
//           Corners are half-edges: the corner of face F at vertex V is the
//           half-edge of F's loop whose origin is V.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_MESH_STORE_H
#define CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_MESH_STORE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Schema.h"
#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Per-corner surfacing. uv0 is the material UV set, uv1 the independent
// lightmap set (editor_comparison_beyond_trenchbroom.md 5.6). Colour is
// RGBA8, 0xFFFFFFFF (opaque white) by default so an uncoloured mesh
// multiplies as identity.
struct mesh_corner_attributes_t {
    math::vec2d_t uv0{};
    math::vec2d_t uv1{};
    common::u32 colorRgba{ 0xFFFFFFFFu };
};

// Per-face surfacing. smoothingGroups is a bitmask: two faces meeting at a
// non-hard edge shade smoothly when their masks intersect. 0 means "flat".
struct mesh_face_attributes_t {
    geometry_material_ref_t material{};
    common::u32 smoothingGroups{ 1u };
};

// Per-edge flags.
enum mesh_edge_flag_bits_t : common::u8 {
    MESH_EDGE_FLAG_HARD = 1u << 0, // split normals across this edge
    MESH_EDGE_FLAG_SEAM = 1u << 1  // UV island boundary for unwrapping
};

struct mesh_edge_attributes_t {
    common::u8 flags{ 0u };
};

template <typename record_t>
struct mesh_attribute_slot_t {
    common::u32 nGeneration{ 0u }; // 0 = never written (generations start at 1)
    record_t value{};
};

struct mesh_attribute_store_t {
    common::vector_t<mesh_attribute_slot_t<mesh_corner_attributes_t>> corners{};
    common::vector_t<mesh_attribute_slot_t<mesh_face_attributes_t>> faces{};
    common::vector_t<mesh_attribute_slot_t<mesh_edge_attributes_t>> edges{};
};

CYPHER_NODISCARD geometry_status_t MeshAttributeStore_Init(
    mesh_attribute_store_t *pStore,
    const common::allocator_t *pAllocator ) noexcept;

void MeshAttributeStore_Shutdown( mesh_attribute_store_t *pStore ) noexcept;

CYPHER_NODISCARD bool MeshAttributeStore_IsInitialized(
    const mesh_attribute_store_t *pStore ) noexcept;

// Removes every value (all lookups return defaults afterwards).
void MeshAttributeStore_Clear( mesh_attribute_store_t *pStore ) noexcept;

// Getters return the stored value when the handle's generation matches the
// stamp, otherwise the domain default. They never fail.
CYPHER_NODISCARD mesh_corner_attributes_t MeshAttributeStore_GetCorner(
    const mesh_attribute_store_t *pStore,
    geometry_mesh_half_edge_handle_t hCorner ) noexcept;
CYPHER_NODISCARD mesh_face_attributes_t MeshAttributeStore_GetFace(
    const mesh_attribute_store_t *pStore,
    geometry_mesh_face_handle_t hFace ) noexcept;
CYPHER_NODISCARD mesh_edge_attributes_t MeshAttributeStore_GetEdge(
    const mesh_attribute_store_t *pStore,
    geometry_mesh_edge_handle_t hEdge ) noexcept;

// True when a value was written for exactly this handle.
CYPHER_NODISCARD bool MeshAttributeStore_HasCorner(
    const mesh_attribute_store_t *pStore,
    geometry_mesh_half_edge_handle_t hCorner ) noexcept;
CYPHER_NODISCARD bool MeshAttributeStore_HasFace(
    const mesh_attribute_store_t *pStore,
    geometry_mesh_face_handle_t hFace ) noexcept;

// Setters grow the slot array as needed (bounded by the matching mesh
// pool limit passed as cSlotLimit) and stamp the handle's generation.
// Failure-atomic: INVALID_HANDLE for an invalid handle, LIMIT_EXCEEDED past
// cSlotLimit, ALLOCATION_FAILED on growth failure.
CYPHER_NODISCARD geometry_status_t MeshAttributeStore_TrySetCorner(
    mesh_attribute_store_t *pStore,
    geometry_mesh_half_edge_handle_t hCorner,
    const mesh_corner_attributes_t &value,
    common::usize cSlotLimit ) noexcept;
CYPHER_NODISCARD geometry_status_t MeshAttributeStore_TrySetFace(
    mesh_attribute_store_t *pStore,
    geometry_mesh_face_handle_t hFace,
    const mesh_face_attributes_t &value,
    common::usize cSlotLimit ) noexcept;
CYPHER_NODISCARD geometry_status_t MeshAttributeStore_TrySetEdge(
    mesh_attribute_store_t *pStore,
    geometry_mesh_edge_handle_t hEdge,
    const mesh_edge_attributes_t &value,
    common::usize cSlotLimit ) noexcept;

// Validates values that can be invalid: non-finite UVs. Returns
// NUMERIC_FAILURE and the offending corner handle via *phBadCornerOut.
CYPHER_NODISCARD geometry_status_t MeshAttributeStore_Validate(
    const mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t *phBadCornerOut ) noexcept;

// Counts live values for pMesh (entries whose stamp matches a live handle).
struct mesh_attribute_counts_t {
    common::usize cCorners{ 0u };
    common::usize cFaces{ 0u };
    common::usize cEdges{ 0u };
};
CYPHER_NODISCARD mesh_attribute_counts_t MeshAttributeStore_CountLive(
    const mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_MESH_STORE_H
