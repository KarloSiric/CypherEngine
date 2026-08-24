//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Polygon.h
//  Purpose: Declares planar three-dimensional polygon operations.
//  Details: Polygon APIs derive a stable local basis, validate planarity, and
//           reuse the allocation-free 2D triangulation implementation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Polygon Contract

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state.
================
*/

#ifndef CYPHER_COMMON_MATH_POLYGON_H
#define CYPHER_COMMON_MATH_POLYGON_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Geometry2D.h"
#include "CypherMath_Plane.h"

namespace cypher::math
{

struct polygon3_basis_t {
    vec3_t origin;    // Projection origin, normally the first polygon vertex.
    vec3_t tangent;   // Unit local X axis in the polygon plane.
    vec3_t bitangent; // Unit local Y axis in the polygon plane.
    vec3_t normal;    // Unit winding normal, tangent cross bitangent.
};

// Basis and projection -----------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon3_TryBasis(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    f32 minimumNormalLength,
    CY_OUT polygon3_basis_t *pBasis ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon3_IsPlanar(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    f32 distanceTolerance ) noexcept;
CYPHER_MATH_API void Polygon3_ProjectToBasis(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    CY_OUT_WRITES( cVertices ) vec2_t *pProjected ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon3_TryPlane(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    f32 minimumNormalLength,
    CY_OUT plane_t *pPlane ) noexcept;
// Planar queries and triangulation ----------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon3_TryAreaCentroid(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    f64 minimumAbsArea,
    CY_OUT f32 *pArea,
    CY_OUT vec3_t *pCentroid ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon3_IsConvex(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    f64 orientationTolerance,
    CY_OUT_WRITES( cProjectedScratch ) vec2_t *pProjectedScratch,
    usize cProjectedScratch ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Polygon3_ContainsPoint(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    vec3_t point,
    f32 planeTolerance,
    f32 boundaryTolerance,
    bool_t bIncludeBoundary,
    CY_OUT_WRITES( cProjectedScratch ) vec2_t *pProjectedScratch,
    usize cProjectedScratch ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API polygon_triangulation_result_t
Polygon3_Triangulate(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    f64 orientationTolerance,
    CY_OUT_WRITES( cProjectedScratch ) vec2_t *pProjectedScratch,
    usize cProjectedScratch,
    CY_OUT_WRITES( cIndexScratch ) u32 *pIndexScratch,
    usize cIndexScratch,
    CY_OUT_WRITES( cOutputIndices ) u32 *pOutputIndices,
    usize cOutputIndices ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_POLYGON_H
