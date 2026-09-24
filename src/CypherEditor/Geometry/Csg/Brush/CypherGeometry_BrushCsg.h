//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCsg.h
//  Purpose: Declares regularized Boolean and clipping algorithms over
//           convex brush pieces.
//  Details: Every algorithm here composes one primitive: append a plane,
//           then reduce. A convex solid intersected with a half-space is
//           convex, so clipping, intersection, and the fragments of a
//           subtraction are all expressible as plane lists without ever
//           building general polygon soup.
//
//           Subtraction follows the classic map-editor carve (as in
//           TrenchBroom's Polyhedron_CSG): walk the cutter's planes in a
//           deterministic order; at each plane, the part of the remaining
//           minuend OUTSIDE that plane becomes a finished fragment and the
//           part INSIDE continues to the next plane. What survives every
//           plane is inside the cutter and is discarded. The fragments are
//           disjoint, convex, and exactly tile minuend minus cutter.
//
//           Results are always sets of convex pieces. Concave output is
//           never folded back into one brush; converting a fragment set to
//           an editable mesh is an explicit, separate conversion.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushPiece.h"

#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

// Maximum points accepted by the brute-force hull. Hull cost is O(n^4);
// 128 points is well under a second and far above what merging a handful
// of authored brushes produces.
inline constexpr common::usize BRUSH_CSG_HULL_POINTS_MAX = 128u;

// Returns the plane with its orientation reversed (same geometric plane).
CYPHER_NODISCARD math::planed_t BrushCsg_FlipPlane( math::planed_t plane ) noexcept;

// Splits a piece by a plane. pBackOut receives the part inside the plane's
// half-space (n·p + d <= 0), pFrontOut the part outside. The new face on
// each side carries `cutOrigin` provenance (CUT_PLANE by default, with the
// given source IDs); the front piece's cut face uses the flipped plane.
// Either output may be EMPTY. Outputs must be initialized and distinct from
// the input; on error both are cleared and the input is unchanged.
CYPHER_NODISCARD geometry_status_t BrushCsg_TrySplit(
    const geometry_brush_piece_t *pPiece,
    const geometry_piece_plane_t &cut,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pBackOut,
    geometry_piece_extent_t *pBackExtentOut,
    geometry_brush_piece_t *pFrontOut,
    geometry_piece_extent_t *pFrontExtentOut ) noexcept;

// Intersection of two pieces into pOut (initialized, distinct from inputs).
CYPHER_NODISCARD geometry_status_t BrushCsg_TryIntersect(
    const geometry_brush_piece_t *pA,
    const geometry_brush_piece_t *pB,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pOut,
    geometry_piece_extent_t *pExtentOut ) noexcept;

enum class geometry_csg_overlap_t : common::u8 {
    DISJOINT = 0u,   // operands do not overlap in volume; minuend unchanged
    PARTIAL,         // fragments produced
    CONSUMED,        // cutter contains the minuend; no fragments
    COUNT
};

// Appends the fragments of minuend minus cutter to pFragmentsOut. When the
// operands are disjoint the reduced minuend itself is appended, so the
// output always equals the regularized difference. On error the list is
// truncated back to its size at entry.
CYPHER_NODISCARD geometry_status_t BrushCsg_TrySubtract(
    const geometry_brush_piece_t *pMinuend,
    const geometry_brush_piece_t *pCutter,
    const geometry_policy_t &policy,
    geometry_piece_list_t *pFragmentsOut,
    geometry_csg_overlap_t *pOverlapOut ) noexcept;

// Subtracts every cutter in turn, re-carving each surviving fragment.
// cFragmentsMax bounds the output (LIMIT_EXCEEDED beyond it). Appends to
// pFragmentsOut with the same rollback guarantee. *pChangedOut reports
// whether any cutter overlapped.
CYPHER_NODISCARD geometry_status_t BrushCsg_TrySubtractAll(
    const geometry_brush_piece_t *pMinuend,
    common::span_t<const geometry_brush_piece_t *const> cutters,
    const geometry_policy_t &policy,
    common::usize cFragmentsMax,
    geometry_piece_list_t *pFragmentsOut,
    common::bool_t *pChangedOut ) noexcept;

// Replaces a solid with walls of the given thickness: the piece minus a
// copy shrunk inward by `thickness`. The inner faces inherit the
// provenance of the side they face, flipped. DEGENERATE when the piece is
// too thin to shrink; INVALID_ARGUMENT for a non-positive thickness.
CYPHER_NODISCARD geometry_status_t BrushCsg_TryHollow(
    const geometry_brush_piece_t *pPiece,
    math::f64 thickness,
    const geometry_policy_t &policy,
    geometry_piece_list_t *pWallsOut ) noexcept;

// Convex hull of a point cloud as a piece. Points closer than the weld
// distance are merged first. DEGENERATE when the points are coplanar or
// fewer than four remain; LIMIT_EXCEEDED above BRUSH_CSG_HULL_POINTS_MAX.
// Hull facets carry NONE provenance.
CYPHER_NODISCARD geometry_status_t BrushCsg_TryConvexHull(
    common::span_t<const math::vec3d_t> points,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pOut ) noexcept;

// Convex merge: the hull of every input's vertices. Hull faces coplanar
// with an input plane inherit that plane's provenance (first match in
// input order). Like every convex merge, it fills gaps between inputs.
CYPHER_NODISCARD geometry_status_t BrushCsg_TryMerge(
    common::span_t<const geometry_brush_piece_t *const> pieces,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_H
