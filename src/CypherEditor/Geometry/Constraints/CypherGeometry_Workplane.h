//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Workplane.h
//  Purpose: Declares workplanes: a local grid frame that tools draw, snap,
//           and place on - Hammer's "set workplane to face" for building
//           on sloped surfaces.
//  Details: A workplane is an orthonormal right-handed frame (u, v, normal)
//           at an origin. Local coordinates are (along u, along v, height
//           above the plane). Grid snapping happens in local coordinates
//           with the same rounding as Snap_GridScalar, so a workplane on
//           the world XY plane at the origin snaps exactly like the world
//           grid.
//
//           From a face: the origin is the face's first corner (a grid
//           line then runs through a real vertex, so snapped points land on
//           the face's own edges), the normal is the face normal, and u
//           runs along the face's longest edge - the direction a builder
//           usually wants to measure along on a slope.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_WORKPLANE_H
#define CYPHER_EDITOR_GEOMETRY_WORKPLANE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

struct geometry_workplane_t {
    math::vec3d_t origin{ 0.0, 0.0, 0.0 };
    math::vec3d_t u{ 1.0, 0.0, 0.0 };
    math::vec3d_t v{ 0.0, 1.0, 0.0 };
    math::vec3d_t normal{ 0.0, 0.0, 1.0 };
};

// True when every vector is finite and (u, v, normal) is orthonormal and
// right-handed within `tolerance`.
CYPHER_NODISCARD bool Workplane_IsValid( const geometry_workplane_t &plane, common::f64 tolerance ) noexcept;

// The workplane of a mesh face (see the file comment). STALE_HANDLE for a
// dead face; DEGENERATE for a face without a usable normal or edge.
CYPHER_NODISCARD geometry_status_t Workplane_TryFromFace(
    const editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    geometry_workplane_t *pOut ) noexcept;

// Origin a, u toward b, normal along (b - a) x (c - a). Non-finite points
// -> NUMERIC_FAILURE; coincident or collinear points -> DEGENERATE.
CYPHER_NODISCARD geometry_status_t Workplane_TryFromPoints(
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c,
    geometry_workplane_t *pOut ) noexcept;

// World <-> local (u, v, height) coordinates.
CYPHER_NODISCARD math::vec3d_t Workplane_ToLocal( const geometry_workplane_t &plane, math::vec3d_t world ) noexcept;
CYPHER_NODISCARD math::vec3d_t Workplane_ToWorld( const geometry_workplane_t &plane, math::vec3d_t local ) noexcept;

// The point dropped onto the plane (height 0).
CYPHER_NODISCARD math::vec3d_t Workplane_Project( const geometry_workplane_t &plane, math::vec3d_t world ) noexcept;

// Snaps each local coordinate to the grid (Snap_GridScalar rules: a
// non-positive or non-finite spacing leaves the point unchanged) and
// returns the world point. bOntoPlane also sets the height to 0.
CYPHER_NODISCARD math::vec3d_t Workplane_SnapPoint(
    const geometry_workplane_t &plane,
    math::vec3d_t world,
    common::f64 gridSpacing,
    bool bOntoPlane ) noexcept;

// Where a ray (origin + t dir, t >= 0) meets the plane. False when the ray
// is parallel to it (|dir . normal| below 1e-12 |dir|), points away, or is
// non-finite.
CYPHER_NODISCARD bool Workplane_TryIntersectRay(
    const geometry_workplane_t &plane,
    math::vec3d_t rayOrigin,
    math::vec3d_t rayDirection,
    common::f64 *pTOut,
    math::vec3d_t *pHitOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_WORKPLANE_H
