//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshUvTransform.h
//  Purpose: Declares the face-edit UV tools for editable meshes (Hammer's
//           Face Edit sheet): justify and fit, shift / rotate / scale about
//           a pivot, align-to-face re-projection, and hotspot texturing.
//  Details: These edit the corner UVs already in a mesh_attribute_store_t
//           (the projections that create UVs from scratch are in
//           MeshSurfacing.h). Like MeshSurfacing, an empty face span means
//           every face.
//
//           Justify names UV axes rather than "left/top": whether texture
//           "top" is low or high V depends on the renderer's V direction,
//           so the tool layer maps Hammer's buttons onto these. Justify
//           moves the chosen bounds edge onto 0 or 1 exactly (MAX_U puts
//           the largest U at 1), CENTER puts the bounds centre at
//           (0.5, 0.5), and FIT maps the bounds onto [0, 1] x [0, 1] -
//           the texture shown exactly once across the selection.
//           bTreatAsOne uses one bounds for the whole selection (the
//           faces keep their layout relative to each other); otherwise
//           each face is justified on its own.
//
//           Every tool computes all new UVs before writing any, so a
//           rejected call (stale face, degenerate fit, non-finite result)
//           leaves the store unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_UV_TRANSFORM_H
#define CYPHER_EDITOR_GEOMETRY_MESH_UV_TRANSFORM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSurfacing.h"

namespace cypher::editor::geometry
{

enum class mesh_uv_justify_t : common::u8 {
    MIN_U = 0u, // smallest U onto 0
    MAX_U,      // largest U onto 1
    MIN_V,
    MAX_V,
    CENTER,     // bounds centre onto (0.5, 0.5)
    FIT         // bounds onto [0, 1] x [0, 1]
};

// What rotation and scale turn around.
enum class mesh_uv_pivot_t : common::u8 {
    SELECTION_CENTER = 0u, // centre of the whole selection's UV bounds
    FACE_CENTER,           // each face about its own UV bounds centre
    POINT                  // an explicit UV point
};

// uv' = pivot + R(rotation) (scale * (uv - pivot)) + shift. A negative
// scale mirrors that axis.
struct mesh_uv_transform_t {
    math::vec2d_t scale{ 1.0, 1.0 };
    common::f64 rotationRadians{ 0.0 };
    math::vec2d_t shift{ 0.0, 0.0 };
};

// Transforms the corner UVs of the faces. Rejected (store unchanged): a
// non-finite parameter or pivot -> NUMERIC_FAILURE; a scale component with
// magnitude under 1e-12 -> INVALID_ARGUMENT (it would collapse the UVs); a
// face listed twice -> INVALID_ARGUMENT; a stale face -> STALE_HANDLE.
CYPHER_NODISCARD geometry_status_t MeshUv_TryTransform(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    const mesh_uv_transform_t &transform,
    mesh_uv_pivot_t pivot,
    math::vec2d_t pivotPoint ) noexcept;

// Justifies or fits the faces' UVs (see the file comment). FIT on a
// selection (or, per face, a face) whose UVs have no extent along an axis
// is DEGENERATE; other rejections as MeshUv_TryTransform.
CYPHER_NODISCARD geometry_status_t MeshUv_TryJustify(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    mesh_uv_justify_t mode,
    bool bTreatAsOne ) noexcept;

// Re-projects each face in its own plane (no stretching on sloped faces,
// unlike a box projection), with the world origin as the mapping origin so
// coplanar neighbours stay continuous. U runs horizontally where possible
// (the up hint is world Z, or world Y for faces facing up or down).
// worldUnitsPerUv must be positive on both axes (INVALID_ARGUMENT).
CYPHER_NODISCARD geometry_status_t MeshUv_TryAlignToFace(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    math::vec2d_t worldUnitsPerUv ) noexcept;

// ---------------------------------------------------------------------------
// Hotspot texturing
// ---------------------------------------------------------------------------

inline constexpr common::usize kMeshHotspotRectsMax = 1024u;

// One region of a hotspot atlas: where it sits in UV space and the world
// size it was painted for (its texel density). A trim may repeat along one
// or both axes (bTileU / bTileV): along a tiling axis the face keeps the
// region's density and runs past it, so the texture must repeat there.
struct mesh_hotspot_rect_t {
    math::vec2d_t uvMin{ 0.0, 0.0 };
    math::vec2d_t uvMax{ 1.0, 1.0 };
    math::vec2d_t worldSize{ 1.0, 1.0 };
    bool bAllowRotation{ true };
    bool bTileU{ false };
    bool bTileV{ false };
};

// Hammer's hotspot texturing. Each face is measured in its own plane (U
// along its longest edge) and gets the region that best matches its size:
// the smallest sum over non-tiling axes of |ln(face length / region
// length)|, trying the quarter-turned fit where allowed (ties go to the
// earlier region, then the unrotated fit). Along a non-tiling axis the
// face is fitted to the region exactly; along a tiling axis it keeps the
// region's density. pChosenOut (optional, initialized) receives each
// face's region index in face order (selection order, or pool order for an
// empty span). Rejected (store unchanged): no regions, too many, or a
// region with non-positive UV or world extent -> INVALID_ARGUMENT; a face
// with no extent in its plane -> DEGENERATE; others as MeshUv_TryTransform.
CYPHER_NODISCARD geometry_status_t MeshUv_TryApplyHotspots(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    common::span_t<const mesh_hotspot_rect_t> rects,
    common::vector_t<common::u32> *pChosenOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_UV_TRANSFORM_H
