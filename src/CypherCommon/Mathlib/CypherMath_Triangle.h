//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Triangle.h
//  Purpose: Declares triangle geometry operations.
//  Details: Triangles support winding-derived normals, barycentric coordinates,
//           closest-point queries, and affine transformation for mesh tooling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_TRIANGLE_H
#define CYPHER_COMMON_MATH_TRIANGLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Plane.h"

#include <type_traits>

namespace cypher::math
{

struct triangle3_t {
    vec3_t a;
    vec3_t b;
    vec3_t c;
};

CYPHER_NODISCARD constexpr triangle3_t Triangle3_Make(
    vec3_t a, vec3_t b, vec3_t c ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Triangle3_IsFinite(
    triangle3_t triangle ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Triangle3_Centroid(
    triangle3_t triangle ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Triangle3_NormalUnnormalized(
    triangle3_t triangle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Triangle3_TwiceArea(
    triangle3_t triangle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Triangle3_Area(
    triangle3_t triangle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Triangle3_TryNormal(
    triangle3_t triangle, f32 minimumTwiceArea,
    CY_OUT vec3_t *pUnitNormal ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Triangle3_TryPlane(
    triangle3_t triangle, f32 minimumTwiceArea,
    CY_OUT plane_t *pPlane ) noexcept;

// Barycentric components correspond to vertices a, b, and c.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Triangle3_TryBarycentric(
    triangle3_t triangle, vec3_t point, f32 minimumAbsDenominator,
    CY_OUT vec3_t *pBarycentric ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Triangle3_PointFromBarycentric(
    triangle3_t triangle, vec3_t barycentric ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Triangle3_ContainsPoint(
    triangle3_t triangle, vec3_t point,
    f32 minimumTwiceArea, f32 planeTolerance,
    f32 barycentricTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Triangle3_ClosestPoint(
    triangle3_t triangle, vec3_t point ) noexcept;
CYPHER_NODISCARD constexpr triangle3_t Triangle3_TransformAffine(
    triangle3_t triangle, affine3_t transform ) noexcept;

static_assert( sizeof( triangle3_t ) == sizeof( f32 ) * 9u );
static_assert( std::is_standard_layout_v<triangle3_t> );
static_assert( std::is_trivially_copyable_v<triangle3_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_TRIANGLE_INL
    #include "CypherMath_Triangle.inl"
#endif

#endif // CYPHER_COMMON_MATH_TRIANGLE_H
