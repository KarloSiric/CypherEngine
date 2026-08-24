//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Bounds.h
//  Purpose: Declares axis-aligned three-dimensional bounding boxes.
//  Details: AABB supports an explicit empty sentinel, incremental construction,
//           spatial queries, and conservative affine transformation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Bounds Contract

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state.
================
*/

#ifndef CYPHER_COMMON_MATH_BOUNDS_H
#define CYPHER_COMMON_MATH_BOUNDS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Affine3.h"

#include <type_traits>

namespace cypher::math
{

struct aabb_t {
    vec3_t minimum; // Inclusive minimum on each world or local axis.
    vec3_t maximum; // Inclusive maximum on each world or local axis.
};

// Reversed extrema let the first expanded point initialize all six bounds.
inline constexpr aabb_t CY_AABB_EMPTY{
    { common::CY_F32_MAX, common::CY_F32_MAX, common::CY_F32_MAX },
    { -common::CY_F32_MAX, -common::CY_F32_MAX, -common::CY_F32_MAX }
};

// Construction -------------------------------------------------------------------
CYPHER_NODISCARD constexpr aabb_t Aabb_Make(
    vec3_t minimum, vec3_t maximum ) noexcept;
CYPHER_NODISCARD constexpr aabb_t Aabb_FromPoint( vec3_t point ) noexcept;
CYPHER_NODISCARD constexpr bool_t Aabb_IsEmpty( aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Aabb_IsFinite(
    aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Aabb_IsValid(
    aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API aabb_t Aabb_FromCenterExtents(
    vec3_t center, vec3_t extents ) noexcept;

// Expansion and set operations ---------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API aabb_t Aabb_ExpandPoint(
    aabb_t bounds, vec3_t point ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API aabb_t Aabb_ExpandAabb(
    aabb_t bounds, aabb_t other ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API aabb_t Aabb_Union(
    aabb_t a, aabb_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API aabb_t Aabb_Intersection(
    aabb_t a, aabb_t b ) noexcept;

// Spatial queries ----------------------------------------------------------------
CYPHER_NODISCARD constexpr bool_t Aabb_ContainsPoint(
    aabb_t bounds, vec3_t point ) noexcept;
CYPHER_NODISCARD constexpr bool_t Aabb_ContainsAabb(
    aabb_t outer, aabb_t inner ) noexcept;
CYPHER_NODISCARD constexpr bool_t Aabb_Overlaps(
    aabb_t a, aabb_t b ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API vec3_t Aabb_Center( aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Aabb_Size( aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Aabb_Extents( aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Aabb_Volume( aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Aabb_SurfaceArea( aabb_t bounds ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Aabb_Corner(
    aabb_t bounds, u32 iCorner ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Aabb_ClosestPoint(
    aabb_t bounds, vec3_t point ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Aabb_DistanceSquaredToPoint(
    aabb_t bounds, vec3_t point ) noexcept;

// Eight transformed corners are enclosed conservatively by the output AABB.
CYPHER_NODISCARD CYPHER_MATH_API aabb_t Aabb_TransformAffine(
    aabb_t bounds, affine3_t transform ) noexcept;

static_assert( sizeof( aabb_t ) == sizeof( f32 ) * 6u );
static_assert( std::is_standard_layout_v<aabb_t> );
static_assert( std::is_trivially_copyable_v<aabb_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_BOUNDS_INL
    #include "CypherMath_Bounds.inl"
#endif

#endif // CYPHER_COMMON_MATH_BOUNDS_H
