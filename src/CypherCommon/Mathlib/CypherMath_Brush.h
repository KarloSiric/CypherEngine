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
    OK = 0u,
    INVALID_ARGUMENT,
    DEGENERATE,
    INSUFFICIENT_CAPACITY,
    COUNT
};

struct brush_vertex_result_t {
    brush_build_status_t status;
    usize cVerticesWritten;
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

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_BRUSH_H
