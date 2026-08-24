//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Sphere.h
//  Purpose: Declares bounding-sphere calculations.
//  Details: Spheres provide compact broad-phase bounds, distance queries, merging,
//           and conservative affine transformation for culling and tools.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_SPHERE_H
#define CYPHER_COMMON_MATH_SPHERE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Bounds.h"

#include <type_traits>

namespace cypher::math
{

struct sphere_t {
    vec3_t center; // World- or local-space center chosen by the caller.
    f32 radius;    // Nonnegative radius in the same units as center.
};

inline constexpr sphere_t CY_SPHERE_ZERO{ CY_VEC3_ZERO, 0.0f }; // Degenerate sphere at the origin.

#define CY_SPHERE_MAKE(center, radius) \
    (::cypher::math::Sphere_Make((center), (radius)))

CYPHER_NODISCARD constexpr sphere_t Sphere_Make(
    vec3_t center, f32 radius ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Sphere_IsValid(
    sphere_t sphere ) noexcept;
CYPHER_NODISCARD constexpr bool_t Sphere_ContainsPoint(
    sphere_t sphere, vec3_t point ) noexcept;
CYPHER_NODISCARD constexpr bool_t Sphere_ContainsSphere(
    sphere_t outer, sphere_t inner ) noexcept;
CYPHER_NODISCARD constexpr bool_t Sphere_Overlaps(
    sphere_t a, sphere_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Sphere_ClosestPoint(
    sphere_t sphere, vec3_t point, f32 minimumDirectionLength ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Sphere_DistanceToPoint(
    sphere_t sphere, vec3_t point ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Sphere_Volume(
    sphere_t sphere ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Sphere_SurfaceArea(
    sphere_t sphere ) noexcept;

// Construction and transformation ------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API sphere_t Sphere_FromAabb(
    aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API sphere_t Sphere_Merge(
    sphere_t a, sphere_t b, f32 minimumCenterDistance ) noexcept;
// General affine transforms can produce ellipsoids; this returns a safe sphere.
CYPHER_NODISCARD CYPHER_MATH_API sphere_t Sphere_TransformAffineConservative(
    sphere_t sphere, affine3_t transform ) noexcept;

static_assert( sizeof( sphere_t ) == sizeof( f32 ) * 4u );
static_assert( std::is_standard_layout_v<sphere_t> );
static_assert( std::is_trivially_copyable_v<sphere_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_SPHERE_INL
    #include "CypherMath_Sphere.inl"
#endif

#endif // CYPHER_COMMON_MATH_SPHERE_H
