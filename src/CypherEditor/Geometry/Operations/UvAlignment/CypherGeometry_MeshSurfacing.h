//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSurfacing.h
//  Purpose: Declares surfacing operations for editable meshes: UV
//           projections, material and smoothing-group assignment, and
//           automatic hard-edge marking.
//  Details: These write into a mesh_attribute_store_t; the mesh itself is
//           never modified. They are the "start with planar/box/cylindrical
//           projection, face fit/alignment, UV lock ..." baseline from
//           editor_comparison_beyond_trenchbroom.md 5.6. Seam unwrapping and
//           island packing are later work.
//
//           Face selection: an empty span means "every face".
//
//           Cylindrical projection seam handling: each face's corner angles
//           are unwrapped relative to its first corner, so a face that
//           straddles the seam gets continuous U (e.g. 0.95 -> 1.05) instead
//           of a texture smeared across the whole atlas (0.95 -> 0.05).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SURFACING_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SURFACING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_MeshStore.h"

namespace cypher::editor::geometry
{

// Which UV set a projection writes: 0 = material UVs, 1 = lightmap UVs.
enum class mesh_uv_set_t : common::u8 {
    MATERIAL = 0u,
    LIGHTMAP = 1u
};

// Projects every corner of the selected faces through one planar mapping.
CYPHER_NODISCARD geometry_status_t MeshSurfacing_TryProjectPlanar(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    const math::planar_uv_mappingd_t &mapping,
    mesh_uv_set_t uvSet ) noexcept;

// Box (tri-planar) projection: each face is projected along the world axis
// most aligned with its normal, with a mapping whose normal is that signed
// axis so opposite faces are not mirrored. worldUnitsPerUv > 0 per axis.
CYPHER_NODISCARD geometry_status_t MeshSurfacing_TryProjectBox(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    math::vec3d_t origin,
    math::vec2d_t worldUnitsPerUv,
    mesh_uv_set_t uvSet ) noexcept;

// Cylindrical projection around the axis through axisOrigin along axisDir.
// U = arc length around the axis / worldUnitsPerUv.x (measured at unit
// radius scaled by `radius`), V = height along the axis / worldUnitsPerUv.y.
CYPHER_NODISCARD geometry_status_t MeshSurfacing_TryProjectCylindrical(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    math::vec3d_t axisOrigin,
    math::vec3d_t axisDir,
    common::f64 radius,
    math::vec2d_t worldUnitsPerUv,
    mesh_uv_set_t uvSet ) noexcept;

// Assigns a material to the selected faces (smoothing groups preserved).
CYPHER_NODISCARD geometry_status_t MeshSurfacing_TryAssignMaterial(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    geometry_material_ref_t material ) noexcept;

// Sets the smoothing-group mask of the selected faces (material preserved).
CYPHER_NODISCARD geometry_status_t MeshSurfacing_TrySetSmoothingGroups(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    common::u32 smoothingGroups ) noexcept;

// Marks every interior edge whose dihedral angle (between face normals)
// exceeds fAngleRadians as HARD, and clears HARD on the rest. Boundary
// edges are always marked HARD. Returns the number of hard edges through
// *pcHardOut when non-null.
CYPHER_NODISCARD geometry_status_t MeshSurfacing_TryMarkHardEdgesByAngle(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::f64 fAngleRadians,
    common::u32 *pcHardOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SURFACING_H
