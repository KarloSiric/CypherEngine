//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector2.h
//  Purpose: Declares tightly packed two-dimensional vector math.
//  Details: The API supports editor coordinates, UVs, screen-space geometry, and
//           planar queries without allocation or hidden ownership.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_VECTOR2_H
#define CYPHER_COMMON_MATH_VECTOR2_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Scalar.h"

#include <type_traits>

namespace cypher::math
{

using common::u32;

struct vec2_t {
    f32 x;
    f32 y;
};

inline constexpr vec2_t CY_VEC2_ZERO{ 0.0f, 0.0f };
inline constexpr vec2_t CY_VEC2_ONE{ 1.0f, 1.0f };
inline constexpr vec2_t CY_VEC2_X{ 1.0f, 0.0f };
inline constexpr vec2_t CY_VEC2_Y{ 0.0f, 1.0f };

CYPHER_NODISCARD constexpr vec2_t Vec2_Make( f32 x, f32 y ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_Splat( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_FromArray(
    CY_IN_READS( 2 ) const f32 *pValues ) noexcept;
CYPHER_MATH_API void Vec2_Store(
    vec2_t value, CY_OUT_WRITES( 2 ) f32 *pValues ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec2_Component(
    vec2_t value, u32 iComponent ) noexcept;
CYPHER_MATH_API void Vec2_SetComponent(
    CY_INOUT vec2_t *pValue, u32 iComponent, f32 value ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2_IsFinite( vec2_t value ) noexcept;
CYPHER_NODISCARD constexpr bool_t Vec2_EqualsExact( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2_NearlyEquals(
    vec2_t a, vec2_t b, f32 absoluteTolerance, f32 relativeTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2_IsNearZero(
    vec2_t value, f32 tolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2_IsUnitLength(
    vec2_t value, f32 tolerance ) noexcept;

CYPHER_NODISCARD constexpr vec2_t Vec2_Add( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_Subtract( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_MultiplyComponents( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_DivideComponents( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_Scale( vec2_t value, f32 scale ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_DivideScalar( vec2_t value, f32 divisor ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_Negate( vec2_t value ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_MulAdd( vec2_t a, vec2_t b, f32 scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Abs( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Min( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Max( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Clamp(
    vec2_t value, vec2_t minimum, vec2_t maximum ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Floor( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Ceil( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Round( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Truncate( vec2_t value ) noexcept;

CYPHER_NODISCARD constexpr f32 Vec2_Dot( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec2_Cross( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec2_LengthSquared( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec2_Length( vec2_t value ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec2_DistanceSquared( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec2_Distance( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_PerpendicularCCW( vec2_t value ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_PerpendicularCW( vec2_t value ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_NormalizeUnchecked( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2_TryNormalize(
    vec2_t value, f32 minimumLength, CY_OUT vec2_t *pNormalized,
    CY_OUT_OPTIONAL f32 *pOriginalLength ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_Lerp( vec2_t a, vec2_t b, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_LerpClamped(
    vec2_t a, vec2_t b, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_MoveTowards(
    vec2_t current, vec2_t target, f32 maximumDistance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_ClampLength(
    vec2_t value, f32 minimumLength, f32 maximumLength ) noexcept;

CYPHER_NODISCARD constexpr vec2_t Vec2_ProjectOntoUnit(
    vec2_t value, vec2_t unitDirection ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_RejectFromUnit(
    vec2_t value, vec2_t unitDirection ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_ReflectUnitNormal(
    vec2_t incident, vec2_t unitNormal ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2_TryProjectOnto(
    vec2_t value, vec2_t onto, f32 minimumLength,
    CY_OUT vec2_t *pProjected ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2_TryAngleBetween(
    vec2_t a, vec2_t b, f32 minimumLength,
    CY_OUT f32 *pAngleRadians ) noexcept;

static_assert( sizeof( vec2_t ) == 8u );
static_assert( alignof( vec2_t ) == alignof( f32 ) );
static_assert( std::is_standard_layout_v<vec2_t> );
static_assert( std::is_trivially_copyable_v<vec2_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_VECTOR2_INL
    #include "CypherMath_Vector2.inl"
#endif

#endif // CYPHER_COMMON_MATH_VECTOR2_H
