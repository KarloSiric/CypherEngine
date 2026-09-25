//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgPredicates.h
//  Purpose: Declares the exact decisions the CSG pipeline takes on input
//           coordinates: which side of a triangle's plane a point is on,
//           whether a segment passes through a triangle, and 2D point and
//           segment tests in a triangle's projection.
//  Details: All of them reduce to CypherMath's exact Orient2D/Orient3D, so
//           a decision about input geometry is never a rounding accident
//           and the same question always gets the same answer. Inputs must
//           be finite (CSG input validation guarantees it); a coincident or
//           degenerate configuration reports 0 rather than guessing.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_PREDICATES_H
#define CYPHER_EDITOR_GEOMETRY_CSG_PREDICATES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgTypes.h"

namespace cypher::editor::geometry
{

// +1 when p is on the side the counter-clockwise triangle (a, b, c) faces,
// -1 behind it, 0 exactly in its plane.
CYPHER_NODISCARD common::i32 CsgPredicate_Side( math::vec3d_t a, math::vec3d_t b, math::vec3d_t c, math::vec3d_t p ) noexcept;

// The axis to drop when projecting the triangle to 2D (the largest normal
// component, verified exactly non-degenerate), and whether the 2D axes must
// be swapped to keep the triangle counter-clockwise. False for a flat
// (collinear) triangle.
struct csg_projection_t {
    common::u32 axis{ 2u };
    bool bFlip{ false };
};
CYPHER_NODISCARD bool CsgPredicate_TryProjection( math::vec3d_t a, math::vec3d_t b, math::vec3d_t c, csg_projection_t *pOut ) noexcept;
CYPHER_NODISCARD math::vec2d_t CsgPredicate_Project( const csg_projection_t &projection, math::vec3d_t p ) noexcept;

// 2D exact point-in-triangle for a counter-clockwise triangle: +1 strictly
// inside, 0 on its boundary, -1 outside.
CYPHER_NODISCARD common::i32 CsgPredicate_PointInTriangle2D( math::vec2d_t a, math::vec2d_t b, math::vec2d_t c, math::vec2d_t p ) noexcept;

// Where a segment p-q that crosses the plane of triangle (a, b, c) meets
// the triangle: +1 inside, 0 on its boundary (edge or corner), -1 outside.
// *piBoundaryEdge (optional) receives the triangle edge (0: a-b, 1: b-c,
// 2: c-a) it passes through when exactly one edge test is zero, or 3 + the
// corner index when it passes through a corner.
CYPHER_NODISCARD common::i32 CsgPredicate_SegmentThroughTriangle(
    math::vec3d_t p, math::vec3d_t q, math::vec3d_t a, math::vec3d_t b, math::vec3d_t c, common::u32 *piBoundary ) noexcept;

// 2D proper crossing of segments a-b and c-d (interiors cross at one point,
// no endpoint touching).
CYPHER_NODISCARD bool CsgPredicate_ProperCross2D( math::vec2d_t a, math::vec2d_t b, math::vec2d_t c, math::vec2d_t d ) noexcept;

// 2D: p lies on segment a-b (collinear and within its closed extent).
CYPHER_NODISCARD bool CsgPredicate_OnSegment2D( math::vec2d_t a, math::vec2d_t b, math::vec2d_t p ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_PREDICATES_H
