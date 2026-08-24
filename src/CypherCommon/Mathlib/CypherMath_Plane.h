//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Plane.h
//  Purpose: Declares three-dimensional plane operations.
//  Details: A plane satisfies dot(normal, point) + d = 0. Distance operations
//           require a unit normal, which checked constructors produce.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_PLANE_H
#define CYPHER_COMMON_MATH_PLANE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Affine3.h"

#include <type_traits>

namespace cypher::math
{

enum class plane_side_t : common::u8 {
    NEGATIVE = 0u, // Signed distance is below -tolerance.
    ON_PLANE,      // Absolute signed distance is within tolerance.
    POSITIVE,      // Signed distance is above +tolerance.
    COUNT          // Enum bound; not a classification result.
};

struct plane_t {
    vec3_t normal; // Plane orientation; unit length for metric distance queries.
    f32 d;         // Constant in dot(normal, point) + d = 0.
};

inline constexpr plane_t CY_PLANE_X{ CY_VEC3_FORWARD, 0.0f }; // Plane through origin normal to X.
inline constexpr plane_t CY_PLANE_Y{ CY_VEC3_LEFT, 0.0f };    // Plane through origin normal to Y.
inline constexpr plane_t CY_PLANE_Z{ CY_VEC3_UP, 0.0f };      // Plane through origin normal to Z.

// Construction and normalization ------------------------------------------------
CYPHER_NODISCARD constexpr plane_t Plane_Make(
    vec3_t normal, f32 d ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Plane_IsFinite(
    plane_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Plane_IsNormalized(
    plane_t value, f32 tolerance ) noexcept;
CYPHER_NODISCARD constexpr plane_t Plane_Flip( plane_t value ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Plane_TryNormalize(
    plane_t value, f32 minimumNormalLength,
    CY_OUT plane_t *pNormalized ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Plane_TryFromPointNormal(
    vec3_t point, vec3_t normal, f32 minimumNormalLength,
    CY_OUT plane_t *pPlane ) noexcept;
// Triangle winding a->b->c determines the positive normal by the right-hand rule.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Plane_TryFromTriangle(
    vec3_t a, vec3_t b, vec3_t c, f32 minimumTwiceArea,
    CY_OUT plane_t *pPlane ) noexcept;

// Classification and transformation ---------------------------------------------
// Signed distance is metric only when the plane normal is unit length.
CYPHER_NODISCARD constexpr f32 Plane_SignedDistance(
    plane_t plane, vec3_t point ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Plane_ProjectPointUnit(
    plane_t unitPlane, vec3_t point ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API plane_side_t Plane_ClassifyPoint(
    plane_t unitPlane, vec3_t point, f32 distanceTolerance ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Plane_TryTransform(
    plane_t plane, affine3_t transform,
    f32 minimumAbsDeterminant, f32 minimumNormalLength,
    CY_OUT plane_t *pTransformed ) noexcept;

static_assert( sizeof( plane_t ) == sizeof( f32 ) * 4u );
static_assert( std::is_standard_layout_v<plane_t> );
static_assert( std::is_trivially_copyable_v<plane_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_PLANE_INL
    #include "CypherMath_Plane.inl"
#endif

#endif // CYPHER_COMMON_MATH_PLANE_H
