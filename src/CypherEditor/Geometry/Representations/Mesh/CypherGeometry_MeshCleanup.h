//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshCleanup.h
//  Purpose: Declares low-level editable-mesh cleanup primitives, deep
//           clone, and per-corner auto-smooth normal evaluation.
//  Details: The cleanup functions mutate the mesh directly. Per
//           ARCHITECTURE.md they are *primitives*: user-facing repair must
//           go through an explicit, previewable Repair plan applied by a
//           Transaction, and Validation never calls these. They live here
//           until the Repair/ and Attributes/Propagation modules exist.
//
//           MeshCleanup_RemoveDegenerateFaces — dissolves faces whose loop
//             has fewer than 3 half-edges or whose area is below tolerance.
//           MeshCleanup_RemoveIsolatedVertices — removes vertices with no
//             outgoing half-edge (orphans left by earlier topology edits).
//           MeshCleanup_RecalculateNormals — recomputes stored face normals
//             from loop winding and reports (but does not fix) a shell whose
//             winding encloses negative volume.
//           MeshCleanup_Full — runs the three passes in order.
//           EditableMesh_TryClone — deep copy with full handle remapping.
//           MeshCleanup_ComputeAutoSmoothNormals — split normals for render
//             cook, smooth across edges below an angle threshold.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//  - 2026-09-23: clone remaps handles; normals no longer claim to repair
//    orientation; convex decomposition removed (physics-owned per
//    Cook/Collision contract).
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_CLEANUP_H
#define CYPHER_EDITOR_GEOMETRY_MESH_CLEANUP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

// Result of removing degenerate faces.
struct mesh_remove_degenerate_result_t {
    common::u32 cFacesRemoved{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Dissolves faces with fewer than 3 half-edges or with area below
// fAreaTolerance by dissolving one of their edges into a neighbour.
// fAreaTolerance must be finite and >= 0 (0 disables the area test). Faces
// whose dissolve is refused (e.g. it would itself create a degenerate face)
// are left in place and not counted.
CYPHER_NODISCARD mesh_remove_degenerate_result_t
MeshCleanup_RemoveDegenerateFaces(
    editable_mesh_t *pMesh,
    common::f64 fAreaTolerance ) noexcept;

// Result of removing isolated vertices.
struct mesh_remove_isolated_result_t {
    common::u32 cVerticesRemoved{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Removes vertices whose outgoing half-edge handle is invalid.
CYPHER_NODISCARD mesh_remove_isolated_result_t
MeshCleanup_RemoveIsolatedVertices(
    editable_mesh_t *pMesh ) noexcept;

// Result of normal recalculation.
struct mesh_recalc_normals_result_t {
    // Faces whose stored normal changed by more than a small angle.
    common::u32 cNormalsChanged{ 0u };
    // True when the winding encloses negative signed volume, i.e. the
    // shell is inside-out. Fixing that means reversing every loop, which
    // is a topology change owned by an explicit Repair plan — this
    // function only reports it.
    bool bInwardWinding{ false };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Recomputes all stored face normals from loop winding (Newell's method),
// so normal and winding can never disagree afterwards.
CYPHER_NODISCARD mesh_recalc_normals_result_t
MeshCleanup_RecalculateNormals(
    editable_mesh_t *pMesh ) noexcept;

// Aggregate result of a full cleanup pass.
struct mesh_cleanup_result_t {
    common::u32 cDegenerateFacesRemoved{ 0u };
    common::u32 cIsolatedVerticesRemoved{ 0u };
    common::u32 cNormalsChanged{ 0u };
    bool bInwardWinding{ false };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Runs degenerate-face removal, isolated-vertex removal, and normal
// recalculation in that order.
CYPHER_NODISCARD mesh_cleanup_result_t MeshCleanup_Full(
    editable_mesh_t *pMesh,
    common::f64 fAreaTolerance ) noexcept;

// ---------------------------------------------------------------------------
// Mesh clone
// ---------------------------------------------------------------------------

// Deep-copies an editable mesh into pMeshOut, which must be uninitialized.
// Every stored handle is remapped through per-pool slot tables, so the copy
// is valid even when the source pools contain holes or bumped generations
// from earlier deletions. On failure pMeshOut is left uninitialized.
// Returns CORRUPT_STATE if a live source record references a stale handle.
CYPHER_NODISCARD geometry_status_t EditableMesh_TryClone(
    const editable_mesh_t *pMeshSrc,
    const common::allocator_t *pAllocator,
    editable_mesh_t *pMeshOut ) noexcept;

// ---------------------------------------------------------------------------
// Auto-smooth normals
// ---------------------------------------------------------------------------

// One per-corner (per-half-edge) render normal.
struct mesh_corner_normal_t {
    geometry_mesh_half_edge_handle_t hHalfEdge{};
    math::vec3d_t normal{};
};

// Result of auto-smooth evaluation. pNormals is allocated from the caller's
// allocator; free with Allocator_Free(pAllocator, pNormals,
// cNormals * sizeof(mesh_corner_normal_t), alignof(mesh_corner_normal_t)).
struct mesh_auto_smooth_result_t {
    mesh_corner_normal_t *pNormals{ nullptr };
    common::usize cNormals{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Computes one normal per half-edge corner. At each corner, the normals of
// faces around the corner's vertex are averaged if they are within
// fAngleThresholdRadians of the corner's own face normal. This is a
// pairwise test against the corner face, not a transitive smoothing
// group: a fan of gently curving faces whose ends differ by more than the
// threshold is not averaged end-to-end. The threshold must be finite and in
// [0, pi]: 0 gives flat shading and pi gives fully smooth shading. The mesh
// is not modified.
CYPHER_NODISCARD mesh_auto_smooth_result_t
MeshCleanup_ComputeAutoSmoothNormals(
    const editable_mesh_t *pMesh,
    const common::allocator_t *pAllocator,
    common::f64 fAngleThresholdRadians ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_CLEANUP_H
