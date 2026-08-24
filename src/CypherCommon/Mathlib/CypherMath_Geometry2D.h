//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Geometry2D.h
//  Purpose: Declares robust planar geometry used by editor tooling.
//  Details: Predicates use double-precision intermediates while public storage
//           remains float. Triangulation uses caller-owned output and scratch.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_GEOMETRY2D_H
#define CYPHER_COMMON_MATH_GEOMETRY2D_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Vector2.h"

namespace cypher::math
{

using common::usize;

struct segment2_t {
    vec2_t start; // Point at normalized parameter zero.
    vec2_t end;   // Point at normalized parameter one.
};

enum class segment2_intersection_kind_t : common::u8 {
    NONE = 0u, // Segments share no point within tolerance.
    POINT,     // Segments meet at one point.
    OVERLAP,   // Collinear segments share a finite interval.
    COUNT      // Enum bound; not a query result.
};

struct segment2_intersection_t {
    segment2_intersection_kind_t kind; // Selects which result fields are meaningful.
    vec2_t point0;                     // Point hit or first overlap endpoint.
    vec2_t point1;                     // Second overlap endpoint; equals point0 for POINT.
    f32 parameterA0;                   // point0 parameter on segment A.
    f32 parameterA1;                   // point1 parameter on segment A.
    f32 parameterB0;                   // point0 parameter on segment B.
    f32 parameterB1;                   // point1 parameter on segment B.
};

enum class polygon_triangulation_status_t : common::u8 {
    OK = 0u,           // Complete triangle index list was written.
    INVALID_ARGUMENT,  // Pointer, count, or tolerance contract failed.
    DEGENERATE,        // Polygon has no stable signed area.
    NOT_SIMPLE,        // Non-adjacent polygon edges intersect.
    INSUFFICIENT_OUTPUT, // Output cannot hold 3 * (vertexCount - 2) indices.
    INSUFFICIENT_SCRATCH, // Scratch cannot hold one index per input vertex.
    COUNT              // Enum bound; not returned.
};

struct polygon_triangulation_result_t {
    polygon_triangulation_status_t status; // Completion state.
    usize cTriangles;                      // Number of complete triangles emitted.
    usize cIndicesWritten;                 // Valid prefix in the output index array.
};

CYPHER_NODISCARD CYPHER_MATH_API f64 Geometry2D_Orientation(
    vec2_t a, vec2_t b, vec2_t c ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Geometry2D_PointOnSegment(
    vec2_t point, segment2_t segment, f32 tolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API segment2_intersection_t
Geometry2D_IntersectSegments(
    segment2_t a, segment2_t b, f32 tolerance ) noexcept;

// Polygon predicates -------------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API f64 Polygon2_SignedArea(
    CY_IN_READS( cVertices ) const vec2_t *pVertices,
    usize cVertices ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon2_TryCentroid(
    CY_IN_READS( cVertices ) const vec2_t *pVertices,
    usize cVertices,
    f64 minimumAbsArea,
    CY_OUT vec2_t *pCentroid ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon2_ContainsPoint(
    CY_IN_READS( cVertices ) const vec2_t *pVertices,
    usize cVertices,
    vec2_t point,
    f32 boundaryTolerance,
    bool_t bIncludeBoundary ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon2_IsSimple(
    CY_IN_READS( cVertices ) const vec2_t *pVertices,
    usize cVertices,
    f32 tolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon2_IsConvex(
    CY_IN_READS( cVertices ) const vec2_t *pVertices,
    usize cVertices,
    f64 orientationTolerance ) noexcept;

// Ear clipping accepts clockwise or counter-clockwise simple polygons.
CYPHER_NODISCARD CYPHER_MATH_API polygon_triangulation_result_t
Polygon2_Triangulate(
    CY_IN_READS( cVertices ) const vec2_t *pVertices,
    usize cVertices,
    f64 orientationTolerance,
    CY_OUT_WRITES( cScratchIndices ) u32 *pScratchIndices,
    usize cScratchIndices,
    CY_OUT_WRITES( cOutputIndices ) u32 *pOutputIndices,
    usize cOutputIndices ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_GEOMETRY2D_H
