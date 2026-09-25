//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushShapes.h
//  Purpose: Declares box-fitted brush shapes for the draw-shape tool: the
//           host drags out a bounding box and the shape fills it (cuboid,
//           cylinder, cone, UV and icosahedral spheroids, arch, stairs),
//           with a choice of how round shapes meet the box.
//  Details: The existing generators (BrushGenerator_*) take a centre and a
//           radius. A drawing tool instead has a box, often not square, so
//           round shapes here become elliptical and the circle mode decides
//           how the polygon touches the box:
//             - VERTEX_ALIGNED: a polygon inscribed in the ellipse; with a
//               side count divisible by 4, four vertices touch the box;
//             - EDGE_ALIGNED: a polygon circumscribed about the ellipse,
//               four of its edges lying on the box (side count must be
//               divisible by 4, otherwise it could not stay inside);
//             - SCALABLE: grid-friendly curves. Each quarter is a fixed
//               integer template (12, 24, 48 or 96 sides) scaled so the
//               smaller box side fits it; on a non-square box the quarters
//               stay round and straight runs fill the difference, so the
//               vertices stay on the grid instead of being stretched off
//               it. Every vertex lands on the grid whenever the smaller box
//               side is a multiple of BrushShapes_ScalableCircleUnits(n)
//               grid steps.
//           The scalable templates are derived here (the smallest integer
//           radius whose rounded quarter arc is strictly convex); they are
//           not taken from any other editor's tables.
//
//           Z is up (stairs rise and arches stand along +Z). All outputs
//           are fully validated convex brushes whose sides carry
//           iAttributeIndex = side index, ready for
//           BrushSource_TryBuildDefault. Like the generators, every function
//           builds privately and publishes only on success: on failure the
//           destinations and the source-ID allocator are unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_SHAPES_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_SHAPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_Policy.h"
#include "CypherMath_Vector2.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kBrushCircleSidesMax = 256u;

enum class brush_circle_mode_t : common::u8 {
    EDGE_ALIGNED = 0u,
    VERTEX_ALIGNED,
    SCALABLE
};

// The dragged box. lo < hi strictly on every axis.
struct brush_shape_box_t {
    math::vec3d_t lo{};
    math::vec3d_t hi{};
};

// Direction a staircase is climbed in.
enum class brush_stairs_direction_t : common::u8 {
    POS_X = 0u,
    NEG_X,
    POS_Y,
    NEG_Y
};

// For SCALABLE circles: the template diameter in grid steps (vertices are
// on the grid when the smaller box side is a multiple of this many grid
// steps). 0 for side counts SCALABLE does not support.
CYPHER_NODISCARD common::u32 BrushShapes_ScalableCircleUnits( common::u32 nSides ) noexcept;

// The circle polygon fitted into the rectangle [lo, hi], counter-clockwise.
// pPoints needs nSides + 4 slots (SCALABLE may add up to four straight-run
// vertices); *pCountOut receives the vertex count. INVALID_ARGUMENT for a
// bad rectangle, side count outside [3, kBrushCircleSidesMax], EDGE_ALIGNED
// with nSides not divisible by 4, or SCALABLE with nSides not 12/24/48/96.
CYPHER_NODISCARD geometry_status_t BrushShapes_TryMakeCircle(
    math::vec2d_t lo,
    math::vec2d_t hi,
    common::u32 nSides,
    brush_circle_mode_t mode,
    math::vec2d_t *pPoints,
    common::u32 cCapacity,
    common::u32 *pCountOut ) noexcept;

CYPHER_NODISCARD geometry_status_t BrushShapes_TryMakeCuboid(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const brush_shape_box_t &box ) noexcept;

// A cylinder along `axis` (0 = X, 1 = Y, 2 = Z) filling the box: its cross-
// section is the circle fitted into the box's other two extents.
CYPHER_NODISCARD geometry_status_t BrushShapes_TryMakeCylinder(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const brush_shape_box_t &box,
    common::u32 axis,
    common::u32 nSides,
    brush_circle_mode_t mode ) noexcept;

// A cone along `axis`: the base circle fills the box at its low end, the
// apex is the centre of its high end.
CYPHER_NODISCARD geometry_status_t BrushShapes_TryMakeCone(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const brush_shape_box_t &box,
    common::u32 axis,
    common::u32 nSides,
    brush_circle_mode_t mode ) noexcept;

// A UV spheroid (latitude rings of quads, triangles at the poles) filling
// the box: nSides around, nRings bands from pole to pole (>= 2). The rings
// use the circle mode, so EDGE_ALIGNED touches the box sides with faces.
CYPHER_NODISCARD geometry_status_t BrushShapes_TryMakeUvSphere(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const brush_shape_box_t &box,
    common::u32 nSides,
    common::u32 nRings,
    brush_circle_mode_t mode ) noexcept;

// An icosahedral spheroid (BrushGenerator_TryMakeSphere) stretched to fill
// the box.
CYPHER_NODISCARD geometry_status_t BrushShapes_TryMakeIcoSphere(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const brush_shape_box_t &box,
    common::u32 nSubdivisions ) noexcept;

// An arch filling the box, running through it along `axis` (0 = X or
// 1 = Y) like a tunnel, with walls `thickness` thick. The outline is the
// upper half of the circle fitted to a box twice as tall (so the arch
// spans the full width at the bottom and touches the top); in SCALABLE mode
// a box taller than half its width gives straight vertical supports. One
// convex brush per segment. pBrushes must hold nSides / 2 + 4 canonical-
// empty brushes; *pCountOut receives how many were made.
CYPHER_NODISCARD geometry_status_t BrushShapes_TryMakeArch(
    brush_solid_t *pBrushes,
    common::u32 cCapacity,
    common::u32 *pCountOut,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const brush_shape_box_t &box,
    common::u32 axis,
    common::u32 nSides,
    brush_circle_mode_t mode,
    common::f64 thickness ) noexcept;

// Solid stairs filling the box, climbed in `direction`: ceil(height /
// stepHeight) steps, each a box from the floor up to its tread (the last
// step reaches the top of the box). INSUFFICIENT_CAPACITY (with the needed
// count in *pCountOut) when pBrushes is too small.
CYPHER_NODISCARD geometry_status_t BrushShapes_TryMakeStairs(
    brush_solid_t *pBrushes,
    common::u32 cCapacity,
    common::u32 *pCountOut,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const brush_shape_box_t &box,
    brush_stairs_direction_t direction,
    common::f64 stepHeight ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_SHAPES_H
