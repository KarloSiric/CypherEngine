//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector4.h
//  Purpose: Declares tightly packed four-dimensional vector math.
//  Details: Vector4 is used for homogeneous coordinates, matrix columns, planes,
//           packed shader parameters, and general four-component calculations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_VECTOR4_H
#define CYPHER_COMMON_MATH_VECTOR4_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Vector3.h"

#include <type_traits>

namespace cypher::math
{

struct vec4_t {
    f32 x; // First component or homogeneous X.
    f32 y; // Second component or homogeneous Y.
    f32 z; // Third component or homogeneous Z.
    f32 w; // Fourth component or homogeneous weight.
};

inline constexpr vec4_t CY_VEC4_ZERO{ 0.0f, 0.0f, 0.0f, 0.0f }; // Additive identity.
inline constexpr vec4_t CY_VEC4_ONE{ 1.0f, 1.0f, 1.0f, 1.0f };  // Unit value on every lane.
inline constexpr vec4_t CY_VEC4_X{ 1.0f, 0.0f, 0.0f, 0.0f };    // Positive X basis direction.
inline constexpr vec4_t CY_VEC4_Y{ 0.0f, 1.0f, 0.0f, 0.0f };    // Positive Y basis direction.
inline constexpr vec4_t CY_VEC4_Z{ 0.0f, 0.0f, 1.0f, 0.0f };    // Positive Z basis direction.
inline constexpr vec4_t CY_VEC4_W{ 0.0f, 0.0f, 0.0f, 1.0f };    // Homogeneous W basis direction.

// Construction and component access ---------------------------------------------
CYPHER_NODISCARD constexpr vec4_t Vec4_Make( f32 x, f32 y, f32 z, f32 w ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_Splat( f32 value ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_FromVec3( vec3_t xyz, f32 w ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec4_XYZ( vec4_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec4_t Vec4_FromArray(
    CY_IN_READS( 4 ) const f32 *pValues ) noexcept;
CYPHER_MATH_API void Vec4_Store(
    vec4_t value, CY_OUT_WRITES( 4 ) f32 *pValues ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec4_Component(
    vec4_t value, u32 iComponent ) noexcept;
CYPHER_MATH_API void Vec4_SetComponent(
    CY_INOUT vec4_t *pValue, u32 iComponent, f32 value ) noexcept;

// Comparison and arithmetic ------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec4_IsFinite( vec4_t value ) noexcept;
CYPHER_NODISCARD constexpr bool_t Vec4_EqualsExact( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec4_NearlyEquals(
    vec4_t a, vec4_t b, f32 absoluteTolerance, f32 relativeTolerance ) noexcept;

CYPHER_NODISCARD constexpr vec4_t Vec4_Add( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_Subtract( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_MultiplyComponents( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_DivideComponents( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_Scale( vec4_t value, f32 scale ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_DivideScalar( vec4_t value, f32 divisor ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_Negate( vec4_t value ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_MulAdd( vec4_t a, vec4_t b, f32 scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec4_t Vec4_Abs( vec4_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec4_t Vec4_Min( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec4_t Vec4_Max( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec4_t Vec4_Clamp(
    vec4_t value, vec4_t minimum, vec4_t maximum ) noexcept;

// Length and interpolation -------------------------------------------------------
CYPHER_NODISCARD constexpr f32 Vec4_Dot( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec4_LengthSquared( vec4_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec4_Length( vec4_t value ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec4_DistanceSquared( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec4_Distance( vec4_t a, vec4_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec4_t Vec4_NormalizeUnchecked( vec4_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec4_TryNormalize(
    vec4_t value, f32 minimumLength, CY_OUT vec4_t *pNormalized,
    CY_OUT_OPTIONAL f32 *pOriginalLength ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Vec4_Lerp( vec4_t a, vec4_t b, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec4_t Vec4_LerpClamped(
    vec4_t a, vec4_t b, f32 t ) noexcept;

// Divides xyz by w. Failure leaves pResult at zero.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec4_TryPerspectiveDivide(
    vec4_t value, f32 minimumAbsW, CY_OUT vec3_t *pResult ) noexcept;

static_assert( sizeof( vec4_t ) == 16u );
static_assert( alignof( vec4_t ) == alignof( f32 ) );
static_assert( std::is_standard_layout_v<vec4_t> );
static_assert( std::is_trivially_copyable_v<vec4_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_VECTOR4_INL
    #include "CypherMath_Vector4.inl"
#endif

#endif // CYPHER_COMMON_MATH_VECTOR4_H
