//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceComponents.h
//  Purpose: Component transforms on a mesh source: move / rotate / scale the
//           selected vertices, edges, or faces, offset them along their
//           normals, and flatten them onto a plane.
//  Details: The host composes the transform around the pivot it wants
//           (MeshSelection_TryComputePivot provides centroid / bounds
//           centre): T(pivot) * R * S * T(-pivot). Both edits act on the
//           selection's vertex set (MeshSelection_TryGatherVertices) and go
//           through MeshVertices_TryMove, so a transform that would fold or
//           collapse any face is rejected as a whole and changes nothing.
//           Topology and corner attributes are untouched: UVs stay on their
//           corners, so the texture moves with the geometry.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_COMPONENTS_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_COMPONENTS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSelection.h"
#include "CypherMath_Affine3.h"

namespace cypher::editor::geometry
{

// Applies `transform` to the vertices of the `mode` selection. Mirroring
// (determinant <= 0) is UNSUPPORTED: it would turn the moved faces inside
// out relative to their neighbours. Positions outside the source coordinate
// domain are NUMERIC_FAILURE; folds and collapses are SELF_INTERSECTING /
// DEGENERATE (see MeshVertices_TryMove). An empty selection is a no-op.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryTransformComponents(
    mesh_source_t *pSource,
    const mesh_selection_t *pSelection,
    mesh_selection_mode_t mode,
    const math::affine3d_t &transform ) noexcept;

// Moves each vertex of the `mode` selection by `distance` along its normal:
// the normalized sum of the normals of the selected faces around it (face
// mode) or of all faces around it (vertex and edge mode). Positive
// distance is outward. A vertex whose normals cancel out is DEGENERATE.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryOffsetComponents(
    mesh_source_t *pSource,
    const mesh_selection_t *pSelection,
    mesh_selection_mode_t mode,
    common::f64 distance ) noexcept;

// Which plane Flatten projects onto. Every plane passes through the
// centroid of the selection's vertices.
enum class mesh_flatten_plane_t : common::u8 {
    BEST_FIT = 0u,  // least-squares plane of the vertices
    AVERAGE_NORMAL, // normal = the summed face normals (the selected faces in
                    // face mode, all faces at the vertices otherwise)
    AXIS_X,         // Hammer's Flatten X / Y / Z: one coordinate made equal
    AXIS_Y,
    AXIS_Z
};

// Hammer's Flatten: projects the `mode` selection's vertices onto a plane
// (see mesh_flatten_plane_t) through MeshVertices_TryMove, so a flatten
// that would fold or collapse a face changes nothing. *pPlaneOut (optional)
// receives the plane used (unit normal). BEST_FIT needs three vertices not
// on one line and AVERAGE_NORMAL normals that do not cancel (DEGENERATE
// otherwise). An empty selection is a no-op.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryFlattenComponents(
    mesh_source_t *pSource,
    const mesh_selection_t *pSelection,
    mesh_selection_mode_t mode,
    mesh_flatten_plane_t plane,
    math::planed_t *pPlaneOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_COMPONENTS_H
