//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Segment.h
//  Purpose: Declares exact 2D segment-pair classification and a
//           controlled (non-exact) intersection-point construction.
//  Details: Classification is decided purely by exact Orient2D signs and
//           exact coordinate comparisons, so two callers asking about the
//           same pair always agree — the property every topological
//           decision (validation, bridging, arrangement) depends on.
//
//           Construction of the actual crossing point cannot be exact in
//           binary64. It is kept separate so a caller can never let a
//           rounded point feed back into a sign decision by accident: the
//           arrangement code re-derives topology from the *input*
//           segments, never from constructed points.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PLANAR_SEGMENT_H
#define CYPHER_EDITOR_GEOMETRY_PLANAR_SEGMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// How two closed segments [a0, a1] and [b0, b1] meet.
enum class planar_segment_relation_t : common::u8 {
    DISJOINT          = 0u, // no common point
    PROPER            = 1u, // cross at one point interior to both
    TOUCH_ENDPOINT    = 2u, // one common point that is an endpoint of either
    COLLINEAR_OVERLAP = 3u  // collinear and share a sub-segment of length > 0
};

// Exact classification. Degenerate (zero-length) segments are treated as
// points; a point lying on the other segment is TOUCH_ENDPOINT.
CYPHER_NODISCARD planar_segment_relation_t Planar_ClassifySegments(
    math::vec2d_t a0,
    math::vec2d_t a1,
    math::vec2d_t b0,
    math::vec2d_t b1 ) noexcept;

// Exact test: does point p lie on the closed segment [a, b]?
CYPHER_NODISCARD bool Planar_PointOnSegment(
    math::vec2d_t a,
    math::vec2d_t b,
    math::vec2d_t p ) noexcept;

// Parametric crossing of two segments known to be PROPER. Writes the
// parameters along each segment (in (0, 1) up to rounding) and the point
// evaluated on segment A. Returns false for (near-)parallel input, which
// a PROPER pair cannot be unless the caller skipped classification.
//
// The point is constructed, not exact; see the file header.
CYPHER_NODISCARD bool Planar_TryIntersectProper(
    math::vec2d_t a0,
    math::vec2d_t a1,
    math::vec2d_t b0,
    math::vec2d_t b1,
    common::f64 *pTaOut,
    common::f64 *pTbOut,
    math::vec2d_t *pPointOut ) noexcept;

// Deterministic lexicographic (x, then y) total order on points. Used to
// canonicalize event ordering so results do not depend on input order.
CYPHER_NODISCARD bool Planar_PointLess(
    math::vec2d_t a,
    math::vec2d_t b ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PLANAR_SEGMENT_H
