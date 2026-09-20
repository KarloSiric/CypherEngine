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

/*
================
Vector2 Contract

Operations follow CypherMath coordinate, storage, and multiplication conventions. Inputs may
alias only where documented, and normalization handles degenerate values explicitly.
================
*/

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
    f32 x; // Horizontal, U, or first planar component.
    f32 y; // Vertical, V, or second planar component.
};

// Binary64 authoring vector. Geometry authoring keeps this precision until an
// explicit checked cook/runtime conversion requests vec2_t.
struct vec2d_t {
    f64 x;
    f64 y;
};

inline constexpr vec2_t CY_VEC2_ZERO{ 0.0f, 0.0f }; // Additive identity.
inline constexpr vec2_t CY_VEC2_ONE{ 1.0f, 1.0f };  // Unit value on both axes.
inline constexpr vec2_t CY_VEC2_X{ 1.0f, 0.0f };    // Positive X basis direction.
inline constexpr vec2_t CY_VEC2_Y{ 0.0f, 1.0f };    // Positive Y basis direction.

inline constexpr vec2d_t CY_VEC2D_ZERO{ 0.0, 0.0 };
inline constexpr vec2d_t CY_VEC2D_ONE{ 1.0, 1.0 };
inline constexpr vec2d_t CY_VEC2D_X{ 1.0, 0.0 };
inline constexpr vec2d_t CY_VEC2D_Y{ 0.0, 1.0 };

// Construction and component access ---------------------------------------------
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

// Comparison and component arithmetic ------------------------------------------
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
// NOTE: Adding double precisions values
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_Abs( vec2d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_Min( vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_Max( vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_Clamp(
    vec2d_t value, vec2d_t minimum, vec2d_t maximum ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Floor( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Ceil( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Round( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2_t Vec2_Truncate( vec2_t value ) noexcept;

// NOTE: Adding double precision values
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_Floor( vec2d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_Ceil( vec2d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_Round( vec2d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_Truncate( vec2d_t value ) noexcept;

// Geometric operations ----------------------------------------------------------
CYPHER_NODISCARD constexpr f32 Vec2_Dot( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec2_Cross( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec2_LengthSquared( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec2_Length( vec2_t value ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec2_DistanceSquared( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec2_Distance( vec2_t a, vec2_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_PerpendicularCCW( vec2_t value ) noexcept;
CYPHER_NODISCARD constexpr vec2_t Vec2_PerpendicularCW( vec2_t value ) noexcept;

// NOTE: Adding geometrical double precision calculations
CYPHER_NODISCARD constexpr f64 Vec2d_DistanceSquared( vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_TryDistance(
    vec2d_t a, vec2d_t b, CY_OUT f64 *pDistance ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_PerpendicularCCW( vec2d_t value ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_PerpendicularCW( vec2d_t value ) noexcept;

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

// Binary64 authoring subset -----------------------------------------------------
// Raw constexpr arithmetic follows IEEE-754 and does not validate operands;
// LengthSquared may overflow. TryLength and TryNormalize scale before squaring,
// reject non-finite or unrepresentable results, and reset outputs on failure.
// TryLength accepts the zero vector. TryNormalize requires length > minimumLength.
CYPHER_NODISCARD constexpr vec2d_t Vec2d_Make( f64 x, f64 y ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_Splat( f64 value ) noexcept;

// NOTE: Construction/access and precision-conversion group, filled in late —
// this was skipped when it was originally planned; adding now to close the gap.
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_FromArray(
    CY_IN_READS( 2 ) const f64 *pValues ) noexcept;
CYPHER_MATH_API void Vec2d_Store(
    vec2d_t value, CY_OUT_WRITES( 2 ) f64 *pValues ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Vec2d_Component(
    vec2d_t value, u32 iComponent ) noexcept;
CYPHER_MATH_API void Vec2d_SetComponent(
    CY_INOUT vec2d_t *pValue, u32 iComponent, f64 value ) noexcept;

// Widening is exact and lossless; narrowing is checked because it can overflow
// f32's representable range even when the f64 source is perfectly finite.
CYPHER_NODISCARD constexpr vec2d_t Vec2d_FromVec2( vec2_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_TryToVec2(
    vec2d_t value, CY_OUT vec2_t *pResult ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_IsFinite( vec2d_t value ) noexcept;
CYPHER_NODISCARD constexpr bool_t Vec2d_EqualsExact(
    vec2d_t a, vec2d_t b ) noexcept;

// NOTE: Adding because it is important for the later on iterations that will come.
// NOTE: Mason editor math requires these..
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_NearlyEquals(
    vec2d_t a, vec2d_t b, f64 absoluteTolerance, f64 relativeTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_IsNearZero(
    vec2d_t value, f64 tolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_IsUnitLength(
    vec2d_t value, f64 tolerance ) noexcept;

CYPHER_NODISCARD constexpr vec2d_t Vec2d_Add(
    vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_Subtract(
    vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_Scale(
    vec2d_t value, f64 scale ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_DivideScalar(
    vec2d_t value, f64 divisor ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_Negate( vec2d_t value ) noexcept;

// NOTE: Component-wise arithmetic missing a header prototype before this pass;
// definitions already existed in the .inl.
CYPHER_NODISCARD constexpr vec2d_t Vec2d_MultiplyComponents(
    vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_DivideComponents(
    vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_MulAdd(
    vec2d_t a, vec2d_t b, f64 scale ) noexcept;

CYPHER_NODISCARD constexpr f64 Vec2d_Dot( vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD constexpr f64 Vec2d_Cross( vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD constexpr f64 Vec2d_LengthSquared( vec2d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_TryLength(
    vec2d_t value, CY_OUT f64 *pLength ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_TryNormalize(
    vec2d_t value, f64 minimumLength, CY_OUT vec2d_t *pNormalized,
    CY_OUT_OPTIONAL f64 *pOriginalLength ) noexcept;

// NOTE: Adding double precision for geoemtrical needs and proper smooth movement
CYPHER_NODISCARD constexpr vec2d_t Vec2d_Lerp( vec2d_t a, vec2d_t b, f64 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_LerpClamped(
    vec2d_t a, vec2d_t b, f64 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_MoveTowards(
    vec2d_t current, vec2d_t target, f64 maximumDistance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec2d_t Vec2d_ClampLength(
    vec2d_t value, f64 minimumLength, f64 maximumLength ) noexcept;

// NOTE: Adding projections with double precisions
CYPHER_NODISCARD constexpr vec2d_t Vec2d_ProjectOntoUnit(
    vec2d_t value, vec2d_t unitDirection ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_RejectFromUnit(
    vec2d_t value, vec2d_t unitDirection ) noexcept;
CYPHER_NODISCARD constexpr vec2d_t Vec2d_ReflectUnitNormal(
    vec2d_t incident, vec2d_t unitNormal ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_TryProjectOnto(
    vec2d_t value, vec2d_t onto, f64 minimumLength,
    CY_OUT vec2d_t *pProjected ) noexcept;
CYPHER_NODISCARD constexpr bool_t Vec2d_LexicographicLess( vec2d_t a, vec2d_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec2d_TryAngleBetween(
    vec2d_t a, vec2d_t b, f64 minimumLength,
    CY_OUT f64 *pAngleRadians ) noexcept;


static_assert( sizeof( vec2_t ) == 8u );
static_assert( alignof( vec2_t ) == alignof( f32 ) );
static_assert( std::is_standard_layout_v<vec2_t> );
static_assert( std::is_trivially_copyable_v<vec2_t> );

static_assert( sizeof( vec2d_t ) == 16u );
static_assert( alignof( vec2d_t ) == alignof( f64 ) );
static_assert( std::is_standard_layout_v<vec2d_t> );
static_assert( std::is_trivial_v<vec2d_t> );
static_assert( std::is_trivially_copyable_v<vec2d_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_VECTOR2_INL
    #include "CypherMath_Vector2.inl"
#endif

#endif // CYPHER_COMMON_MATH_VECTOR2_H
