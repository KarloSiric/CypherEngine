//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PatchPrimitives.h
//  Purpose: Declares generators that publish canonical biquadratic Patch
//           sources: surfaces of revolution (cylinder, cone, sphere,
//           torus, disc) and linear extrusions of a quadratic profile
//           (bevels, curved wall strips).
//  Details: Everything is built from two exact constructions:
//
//           Revolve: for a circle approximation c(u) in the plane and a
//           profile m(v) = (r, z), the surface S(u, v) = (m_r(v) c(u),
//           m_z(v)) is a biquadratic tensor product with control points
//           P[i][j] = (m_r[j] c[i], m_z[j]). No further approximation is
//           introduced beyond the circle itself.
//
//           Circle: each of cSegments arcs is one quadratic span whose middle
//           control sits where the end tangents meet. That keeps adjacent
//           arcs tangent-continuous (G1), so shading is smooth across
//           sub-patch seams. A non-rational quadratic cannot be exactly
//           circular; its radial error is outward only, zero at the arc
//           ends, and peaks mid-arc at
//             e(n) = (cos(pi/n) + sec(pi/n)) / 2 - 1
//           (n = 4: 6.07 %, n = 8: 0.31 %, n = 16: 0.019 %). Callers choose n
//           to fit their tolerance; PatchPrimitive_CircleRadialError
//           exposes e(n).
//
//           Closed loops (the u seam of every revolve, the v seam of a
//           torus) repeat the first control exactly in the last column or
//           row, so the seam closes bit-exactly. The repeated controls
//           still get distinct source IDs and distinct UVs (0 and 1),
//           which is how textures wrap without a discontinuity.
//
//           Orientation: the default front face points away from the axis
//           for walls (cylinder, cone, sphere, torus) and along +axis for
//           the disc. bInward flips it.
//
//           Primitives are transient generator inputs (see Primitives/
//           README): the published Patch is the authored source, not the
//           parameter record.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PATCH_PRIMITIVES_H
#define CYPHER_EDITOR_GEOMETRY_PATCH_PRIMITIVES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Patch.h"

namespace cypher::editor::geometry
{

// Arc count limits for one full circle. 128 arcs = 257 controls, the Patch
// per-axis maximum.
inline constexpr common::u32 kPatchPrimitiveSegmentsMin = 3u;
inline constexpr common::u32 kPatchPrimitiveSegmentsMax = ( kPatchControlsPerAxisMax - 1u ) / 2u;

// Placement shared by all revolved primitives: the axis passes through
// `center`; angle 0 lies along the component of `reference` perpendicular
// to the axis (any non-parallel vector works; it only fixes where the u
// seam sits).
struct patch_revolve_frame_t {
    math::vec3d_t center{};
    math::vec3d_t axis{ 0.0, 0.0, 1.0 };
    math::vec3d_t reference{ 1.0, 0.0, 0.0 };
};

struct patch_primitive_common_t {
    common::u32 cSegments{ 8u }; // arcs around the full circle
    bool bInward{ false };
    common::u32 materialId{ 0u };
};

// Peak outward radial error of the quadratic circle approximation, as a
// fraction of the radius. Returns +inf for an invalid segment count.
CYPHER_NODISCARD common::f64 PatchPrimitive_CircleRadialError( common::u32 cSegments ) noexcept;

// Generic surface of revolution. `profile` holds (r, z) quadratic controls
// in the frame (r >= 0 distance from the axis, z along it), 2k + 1 of them.
// The profile runs along v; the circle runs along u. Controls get IDs
// row-major from *pIdAllocator; failure-atomic including the allocator.
CYPHER_NODISCARD geometry_status_t PatchPrimitive_TryRevolve(
    common::span_t<const math::vec2d_t> profile,
    const patch_revolve_frame_t &frame,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept;

// Open tube (no caps) from z = 0 to z = height. radiusTop == 0 makes a cone
// with a collapsed apex row; radiusBottom == radiusTop a cylinder.
CYPHER_NODISCARD geometry_status_t PatchPrimitive_TryMakeCone(
    const patch_revolve_frame_t &frame,
    common::f64 radiusBottom,
    common::f64 radiusTop,
    common::f64 height,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept;

// Sphere centred on frame.center, with cMeridianSegments arcs from pole to
// pole (>= 2). The pole rows collapse to single points.
CYPHER_NODISCARD geometry_status_t PatchPrimitive_TryMakeSphere(
    const patch_revolve_frame_t &frame,
    common::f64 radius,
    common::u32 cMeridianSegments,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept;

// Torus around the axis: ring radius R (axis to tube centre) and tube radius
// r with 0 < r < R, cTubeSegments arcs around the tube (>= 3).
CYPHER_NODISCARD geometry_status_t PatchPrimitive_TryMakeTorus(
    const patch_revolve_frame_t &frame,
    common::f64 ringRadius,
    common::f64 tubeRadius,
    common::u32 cTubeSegments,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept;

// Flat disc in the plane z = 0 of the frame, facing +axis by default. The
// centre row collapses to one point. Useful as a cap for cones/cylinders
// built with the same frame and segment count (their rims coincide).
CYPHER_NODISCARD geometry_status_t PatchPrimitive_TryMakeDisc(
    const patch_revolve_frame_t &frame,
    common::f64 radius,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept;

// Linear extrusion: `profile` (2k + 1 world-space quadratic controls) along
// u, swept by `direction` along v in cSubPatchesV quadratic spans (the
// extra rows are exact midpoints, so the shape is a ruled surface; they
// exist so the author can later bend the strip). Front face follows
// dPdu x direction; bInward flips it.
CYPHER_NODISCARD geometry_status_t PatchPrimitive_TryExtrude(
    common::span_t<const math::vec3d_t> profile,
    math::vec3d_t direction,
    common::u32 cSubPatchesV,
    bool bInward,
    common::u32 materialId,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept;

// Rounded corner fill (Radiant "bevel"): the quadratic from corner + sideA
// to corner + sideB whose middle control is the corner itself, extruded
// along `depth`. The curve is tangent to both sides where it meets them, so
// it blends into adjoining walls without a crease.
CYPHER_NODISCARD geometry_status_t PatchPrimitive_TryMakeBevel(
    math::vec3d_t corner,
    math::vec3d_t sideA,
    math::vec3d_t sideB,
    math::vec3d_t depth,
    bool bInward,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PATCH_PRIMITIVES_H
