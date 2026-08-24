//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Intersection.h
//  Purpose: Declares primitive intersection and volume-classification queries.
//  Details: Queries expose caller-selected tolerances and parameter ranges so
//           physics, rendering, editor picking, and tools share precise semantics.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Intersection Contract

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state.
================
*/

#ifndef CYPHER_COMMON_MATH_INTERSECTION_H
#define CYPHER_COMMON_MATH_INTERSECTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Frustum.h"
#include "CypherMath_Ray.h"
#include "CypherMath_Sphere.h"
#include "CypherMath_Triangle.h"

#include <type_traits>

namespace cypher::math
{

enum class volume_relation_t : common::u8 {
    OUTSIDE = 0u, // Primitive lies wholly outside at least one frustum plane.
    INTERSECTING, // Primitive crosses or touches a frustum boundary.
    INSIDE,       // Primitive lies wholly inside every frustum half-space.
    COUNT         // Enum bound; not a classification result.
};

enum class triangle_cull_mode_t : common::u8 {
    NONE = 0u, // Accept either triangle winding.
    BACK_FACE, // Reject rays approaching the back side.
    FRONT_FACE, // Reject rays approaching the front side.
    COUNT      // Enum bound; not a valid culling policy.
};

struct ray_interval_t {
    f32 tEnter; // First accepted parameter along the original ray.
    f32 tExit;  // Last accepted parameter along the original ray.
};

struct ray_triangle_hit_t {
    f32 t;       // Ray or segment parameter at the hit.
    f32 weightB; // Barycentric weight for triangle vertex b.
    f32 weightC; // Barycentric weight for c; weightA = 1 - B - C.
};

// Ray and segment queries --------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Intersection_RayPlane(
    ray_t ray, plane_t plane, f32 minimumAbsDenominator,
    f32 tMinimum, f32 tMaximum,
    CY_OUT f32 *pParameter,
    CY_OUT_OPTIONAL vec3_t *pPoint ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Intersection_RaySphere(
    ray_t ray, sphere_t sphere, f32 minimumDirectionLength,
    f32 tMinimum, f32 tMaximum,
    CY_OUT ray_interval_t *pInterval ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Intersection_RayAabb(
    ray_t ray, aabb_t bounds, f32 minimumAbsDirection,
    f32 tMinimum, f32 tMaximum,
    CY_OUT ray_interval_t *pInterval ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Intersection_RayTriangle(
    ray_t ray, triangle3_t triangle, triangle_cull_mode_t cullMode,
    f32 minimumAbsDeterminant, f32 barycentricTolerance,
    f32 tMinimum, f32 tMaximum,
    CY_OUT ray_triangle_hit_t *pHit ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Intersection_SegmentTriangle(
    segment_t segment, triangle3_t triangle,
    triangle_cull_mode_t cullMode,
    f32 minimumAbsDeterminant, f32 barycentricTolerance,
    CY_OUT ray_triangle_hit_t *pHit ) noexcept;

// Primitive overlap --------------------------------------------------------------
CYPHER_NODISCARD constexpr bool_t Intersection_AabbAabb(
    aabb_t a, aabb_t b ) noexcept;
CYPHER_NODISCARD constexpr bool_t Intersection_SphereSphere(
    sphere_t a, sphere_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Intersection_SphereAabb(
    sphere_t sphere, aabb_t bounds ) noexcept;

// Frustum classification ---------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API volume_relation_t Intersection_FrustumPoint(
    frustum_t frustum, vec3_t point, f32 distanceTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API volume_relation_t Intersection_FrustumSphere(
    frustum_t frustum, sphere_t sphere, f32 distanceTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API volume_relation_t Intersection_FrustumAabb(
    frustum_t frustum, aabb_t bounds, f32 distanceTolerance ) noexcept;

static_assert( sizeof( ray_interval_t ) == sizeof( f32 ) * 2u );
static_assert( sizeof( ray_triangle_hit_t ) == sizeof( f32 ) * 3u );
static_assert( std::is_trivially_copyable_v<ray_interval_t> );
static_assert( std::is_trivially_copyable_v<ray_triangle_hit_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_INTERSECTION_INL
    #include "CypherMath_Intersection.inl"
#endif

#endif // CYPHER_COMMON_MATH_INTERSECTION_H
