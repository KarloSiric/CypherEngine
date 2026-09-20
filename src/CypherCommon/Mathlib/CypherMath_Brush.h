//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Brush.h
//  Purpose: Declares allocation-free convex brush construction helpers.
//  Details: Brushes use outward-facing planes and define their interior as the
//           nonpositive half-space. This matches map-authoring solid geometry.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_BRUSH_H
#define CYPHER_COMMON_MATH_BRUSH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Bounds.h"
#include "CypherMath_Polygon.h"

namespace cypher::math
{

enum class brush_build_status_t : common::u8 {
    OK = 0u,             // Complete requested geometry was written.
    INVALID_ARGUMENT,    // Plane set, tolerance, or output contract is invalid.
    DEGENERATE,          // Planes do not define stable finite brush geometry.
    INSUFFICIENT_CAPACITY, // Caller output cannot hold all unique vertices.
    COUNT                // Enum bound; not returned.
};

struct brush_vertex_result_t {
    brush_build_status_t status; // Completion state.
    usize cVerticesWritten;      // Valid prefix in the caller output array.
};

CYPHER_NODISCARD CYPHER_MATH_API usize Brush_MaximumVertexCandidates(
    usize cPlanes ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Brush_ContainsPoint(
    CY_IN_READS( cPlanes ) const plane_t *pPlanes,
    usize cPlanes,
    vec3_t point,
    f32 insideTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Brush_TryIntersectPlanes(
    plane_t a,
    plane_t b,
    plane_t c,
    f64 minimumAbsDeterminant,
    CY_OUT vec3_t *pPoint ) noexcept;

// Enumerates unique vertices formed by triples of outward brush planes.
CYPHER_NODISCARD CYPHER_MATH_API brush_vertex_result_t Brush_BuildVertices(
    CY_IN_READS( cPlanes ) const plane_t *pPlanes,
    usize cPlanes,
    f64 minimumAbsDeterminant,
    f32 insideTolerance,
    f32 mergeTolerance,
    CY_OUT_WRITES( cOutputVertices ) vec3_t *pOutputVertices,
    usize cOutputVertices ) noexcept;

// Filters and orders existing brush vertices counter-clockwise around a face normal.
CYPHER_NODISCARD CYPHER_MATH_API brush_vertex_result_t Brush_BuildFacePolygon(
    plane_t outwardFacePlane,
    CY_IN_READS( cBrushVertices ) const vec3_t *pBrushVertices,
    usize cBrushVertices,
    f32 faceDistanceTolerance,
    f32 minimumNormalLength,
    CY_OUT_WRITES( cOutputVertices ) vec3_t *pOutputVertices,
    usize cOutputVertices ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Brush_TryBounds(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    CY_OUT aabb_t *pBounds ) noexcept;

// Binary64 authoring brush ---------------------------------------------------------
// Brush_MaximumVertexCandidates and brush_vertex_result_t/brush_build_status_t are
// precision-agnostic (pure counting and status vocabulary) and are reused as-is.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Brushd_ContainsPoint(
    CY_IN_READS( cPlanes ) const planed_t *pPlanes,
    usize cPlanes,
    vec3d_t point,
    f64 insideTolerance ) noexcept;

// Enumerates unique vertices formed by triples of outward brush planes. Internally
// calls Intersection_TryThreePlanesD rather than re-deriving Cramer's rule.
//
// PRECONDITION: plane normals must be unit length. Planes are forwarded to
// Intersection_TryThreePlanesD unchanged, where minimumAbsDeterminant is only a
// meaningful conditioning threshold for unit normals.
//
// COMPLEXITY: enumerates all plane triples and tests each candidate against
// every plane, so cost grows as cPlanes^4. Intended for authoring-scale brushes
// (roughly 6-30 sides); see geometry_limit_policy_t::cBrushSidesPerBrushMax.
CYPHER_NODISCARD CYPHER_MATH_API brush_vertex_result_t Brushd_BuildVertices(
    CY_IN_READS( cPlanes ) const planed_t *pPlanes,
    usize cPlanes,
    f64 minimumAbsDeterminant,
    f64 insideTolerance,
    f64 mergeTolerance,
    CY_OUT_WRITES( cOutputVertices ) vec3d_t *pOutputVertices,
    usize cOutputVertices ) noexcept;

// Filters and orders existing brush vertices counter-clockwise around a face normal.
CYPHER_NODISCARD CYPHER_MATH_API brush_vertex_result_t Brushd_BuildFacePolygon(
    planed_t outwardFacePlane,
    CY_IN_READS( cBrushVertices ) const vec3d_t *pBrushVertices,
    usize cBrushVertices,
    f64 faceDistanceTolerance,
    f64 minimumNormalLength,
    CY_OUT_WRITES( cOutputVertices ) vec3d_t *pOutputVertices,
    usize cOutputVertices ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Brushd_TryBounds(
    CY_IN_READS( cVertices ) const vec3d_t *pVertices,
    usize cVertices,
    CY_OUT aabbd_t *pBounds ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_BRUSH_H
