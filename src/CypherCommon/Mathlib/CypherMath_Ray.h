//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Ray.h
//  Purpose: Declares rays, finite segments, and parameter operations.
//  Details: Ray directions are not implicitly normalized. The parameter t measures
//           distance only when the caller supplies a unit direction.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Ray Contract

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state.
================
*/

#ifndef CYPHER_COMMON_MATH_RAY_H
#define CYPHER_COMMON_MATH_RAY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Affine3.h"

#include <type_traits>

namespace cypher::math
{

struct ray_t {
    vec3_t origin;    // Point corresponding to parameter t = 0.
    vec3_t direction; // Parameter step; not required to be unit length.
};

struct segment_t {
    vec3_t start; // Point corresponding to normalized parameter t = 0.
    vec3_t end;   // Point corresponding to normalized parameter t = 1.
};

#define CY_RAY_MAKE(origin, direction) \
    (::cypher::math::Ray_Make((origin), (direction)))

// Construction and parameter evaluation -----------------------------------------
CYPHER_NODISCARD constexpr ray_t Ray_Make(
    vec3_t origin, vec3_t direction ) noexcept;
CYPHER_NODISCARD constexpr segment_t Segment_Make(
    vec3_t start, vec3_t end ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Ray_IsFinite( ray_t ray ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Segment_IsFinite(
    segment_t segment ) noexcept;

CYPHER_NODISCARD constexpr vec3_t Ray_PointAt( ray_t ray, f32 t ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Segment_Direction(
    segment_t segment ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Segment_PointAt(
    segment_t segment, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Segment_Length(
    segment_t segment ) noexcept;
CYPHER_NODISCARD constexpr f32 Segment_LengthSquared(
    segment_t segment ) noexcept;

// Closest-point and normalization queries ---------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Ray_TryNormalizeDirection(
    ray_t ray, f32 minimumDirectionLength,
    CY_OUT ray_t *pNormalized,
    CY_OUT_OPTIONAL f32 *pOriginalDirectionLength ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Ray_TryClosestParameterToPoint(
    ray_t ray, vec3_t point, f32 minimumDirectionLength,
    CY_OUT f32 *pParameter ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Segment_ClosestPoint(
    segment_t segment, vec3_t point ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Segment_DistanceSquaredToPoint(
    segment_t segment, vec3_t point ) noexcept;

CYPHER_NODISCARD constexpr ray_t Ray_TransformAffine(
    ray_t ray, affine3_t transform ) noexcept;
CYPHER_NODISCARD constexpr segment_t Segment_TransformAffine(
    segment_t segment, affine3_t transform ) noexcept;

static_assert( sizeof( ray_t ) == sizeof( f32 ) * 6u );
static_assert( sizeof( segment_t ) == sizeof( f32 ) * 6u );
static_assert( std::is_standard_layout_v<ray_t> );
static_assert( std::is_trivially_copyable_v<ray_t> );
static_assert( std::is_standard_layout_v<segment_t> );
static_assert( std::is_trivially_copyable_v<segment_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_RAY_INL
    #include "CypherMath_Ray.inl"
#endif

#endif // CYPHER_COMMON_MATH_RAY_H
