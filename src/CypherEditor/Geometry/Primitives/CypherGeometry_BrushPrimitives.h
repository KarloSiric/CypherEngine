//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushPrimitives.h
//  Purpose: Declares pure generators for convex brush primitives and
//           multi-brush architectural shapes.
//  Details: Every generator fills the frame box the user dragged out (the
//           TrenchBroom draw-shape convention) and orients itself by an up
//           axis. Output is identity-free: a single piece for convex
//           shapes, a piece list for shapes that decompose into several
//           convex brushes (stairs, arches, pipes). Materialization into
//           document brushes happens at the operation layer, so these
//           functions stay deterministic data transforms that depend only
//           on the Kernel-level piece contract.
//
//           Generated planes carry NONE provenance; every piece is reduced
//           before it is returned, so each one is a valid solid.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_PRIMITIVES_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_PRIMITIVES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushPiece.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 PRIMITIVE_SIDES_MIN = 3u;
inline constexpr common::u32 PRIMITIVE_SIDES_MAX = 128u;

enum class geometry_axis_t : common::u8 {
    X = 0u,
    Y = 1u,
    Z = 2u,
    COUNT
};

// The box a primitive fills, and which world axis is "up". The two other
// axes follow cyclically: run = (up + 1) % 3, lateral = (up + 2) % 3.
struct geometry_primitive_frame_t {
    math::aabbd_t bounds{};
    geometry_axis_t up{ geometry_axis_t::Z };
};

// How a circular cross-section meets its bounds.
enum class geometry_circle_alignment_t : common::u8 {
    EDGE = 0u,    // polygon edges touch the bounds (circumscribed)
    VERTEX,       // polygon vertices lie on the inscribed ellipse
    COUNT
};

// Common failures: INVALID_ARGUMENT for an empty/non-finite frame, an
// invalid axis, or out-of-range counts; LIMIT_EXCEEDED beyond the policy's
// coordinate limit; DEGENERATE when the frame is thinner than the minimum
// edge length. Outputs must be initialized; on failure a piece is cleared
// and a list is left as it was at entry.

CYPHER_NODISCARD geometry_status_t Primitive_TryBox(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    geometry_brush_piece_t *pOut ) noexcept;

// Ramp rising along +run (or -run when bDescending) from the bottom of the
// frame to its top, with a vertical back wall.
CYPHER_NODISCARD geometry_status_t Primitive_TryWedge(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    common::bool_t bDescending,
    geometry_brush_piece_t *pOut ) noexcept;

// Upright n-sided prism fitted to the frame's run/lateral extent.
CYPHER_NODISCARD geometry_status_t Primitive_TryCylinder(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    common::u32 cSides,
    geometry_circle_alignment_t alignment,
    geometry_brush_piece_t *pOut ) noexcept;

// n-sided cone (pyramid when cSides == 4) with its apex at the top center.
CYPHER_NODISCARD geometry_status_t Primitive_TryCone(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    common::u32 cSides,
    geometry_circle_alignment_t alignment,
    geometry_brush_piece_t *pOut ) noexcept;

// UV sphere (ellipsoid fitted to the frame) with cSlices around the up
// axis and cStacks from pole to pole. cSlices in [3, 32], cStacks in
// [2, 16], limited so the hull stays within its point budget.
CYPHER_NODISCARD geometry_status_t Primitive_TrySphere(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    common::u32 cSlices,
    common::u32 cStacks,
    geometry_brush_piece_t *pOut ) noexcept;

// Solid stairs climbing along +run (or -run): ceil(height / stepHeight)
// steps of equal tread depth, each one brush from the floor to its tread;
// the last riser is clipped to the frame top.
CYPHER_NODISCARD geometry_status_t Primitive_TryStairs(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    math::f64 stepHeight,
    common::bool_t bDescending,
    geometry_piece_list_t *pOut ) noexcept;

// Semi-elliptical arch spanning the run axis, rising along up, extruded
// along lateral, split into cSegments convex voussoirs of the given radial
// thickness.
CYPHER_NODISCARD geometry_status_t Primitive_TryArch(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    common::u32 cSegments,
    math::f64 thickness,
    geometry_piece_list_t *pOut ) noexcept;

// Hollow n-sided tube along up with the given wall thickness, one brush per
// side.
CYPHER_NODISCARD geometry_status_t Primitive_TryPipe(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    common::u32 cSides,
    math::f64 wallThickness,
    geometry_piece_list_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_PRIMITIVES_H
