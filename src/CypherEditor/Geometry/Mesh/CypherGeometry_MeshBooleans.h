//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBooleans.h
//  Purpose: Declares mesh-level boolean operations on editable meshes.
//  Details: Provides union, subtraction, and intersection of two closed
//           convex editable meshes. The implementation pipelines through
//           the brush CSG layer: each mesh is converted to a brush solid,
//           the brush-level boolean is performed, and the result brush
//           is reconstructed back into a half-edge mesh.
//
//           Both input meshes must be closed convex two-manifold solids.
//           The result mesh is a new allocation; inputs are not modified.
//
//           Union requires the combined result to also be convex (the
//           brush merge operation rejects non-convex unions).
//           Subtraction succeeds only when brush CSG produces one convex
//           fragment. Multi-fragment results return UNSUPPORTED until the
//           mesh layer can merge every fragment as a remapped shell without
//           losing topology or provenance.
//           Each mesh is limited to kMeshBoolMaxFaces faces.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_BOOLEANS_H
#define CYPHER_EDITOR_GEOMETRY_MESH_BOOLEANS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

// Upper bound on per-mesh face count for boolean operands.
static constexpr common::usize kMeshBoolMaxFaces = 4096u;

// Computes A ∪ B: the boolean union of two closed convex meshes. The result
// is a new mesh containing the outer shell of both meshes combined.
// Interior faces (those fully inside the opposing mesh) are discarded.
//
// Both meshes must be initialized, closed (Euler characteristic 2),
// and have at most kMeshBoolMaxFaces faces each.
//
// pMeshOut must point to an uninitialized editable_mesh_t; the function
// initializes and populates it on success. On failure, pMeshOut is left
// uninitialized. The caller owns the result and must call
// EditableMesh_Shutdown when done.
//
// pAllocator is used for all scratch and result allocations.
// tolerance controls the coplanar-distance band for face classification
// (should match the geometry policy's coplanar tolerance).
CYPHER_NODISCARD geometry_status_t MeshBool_TryUnion(
    const editable_mesh_t *pMeshA,
    const editable_mesh_t *pMeshB,
    const common::allocator_t *pAllocator,
    common::f64 tolerance,
    editable_mesh_t *pMeshOut ) noexcept;

// Computes A \ B: the boolean subtraction of B from A. The result is
// a new mesh representing the volume of A that is not inside B. Faces
// of A that are inside B are removed; faces of B that are inside A
// are kept with reversed winding (they become interior walls).
//
// Returns UNSUPPORTED when the complete result requires more than one
// convex fragment. In that case pMeshOut remains uninitialized; no fragment
// is silently selected or discarded.
CYPHER_NODISCARD geometry_status_t MeshBool_TrySubtract(
    const editable_mesh_t *pMeshA,
    const editable_mesh_t *pMeshB,
    const common::allocator_t *pAllocator,
    common::f64 tolerance,
    editable_mesh_t *pMeshOut ) noexcept;

// Computes A ∩ B: the boolean intersection of two closed meshes. The
// result is a new mesh containing only the volume shared by both meshes.
// Faces of each mesh that lie outside the other are discarded.
CYPHER_NODISCARD geometry_status_t MeshBool_TryIntersect(
    const editable_mesh_t *pMeshA,
    const editable_mesh_t *pMeshB,
    const common::allocator_t *pAllocator,
    common::f64 tolerance,
    editable_mesh_t *pMeshOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_BOOLEANS_H
