//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Scalar.h
//  Purpose: Declares scalar math policy shared by every CypherMath module.
//  Details: Constants, comparisons, interpolation, wrapping, and transcendental
//           wrappers live here so engine and tool code use identical semantics.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_SCALAR_H
#define CYPHER_COMMON_MATH_SCALAR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Annotations.h"
#include "CypherMath_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::math
{

using common::bool_t;
using common::f32;
using common::f64;

inline constexpr f32 CY_PI_F = 3.14159265358979323846f;
inline constexpr f32 CY_TAU_F = 6.28318530717958647692f;
inline constexpr f32 CY_HALF_PI_F = 1.57079632679489661923f;
inline constexpr f32 CY_QUARTER_PI_F = 0.78539816339744830962f;
inline constexpr f32 CY_DEGREES_TO_RADIANS_F = CY_PI_F / 180.0f;
inline constexpr f32 CY_RADIANS_TO_DEGREES_F = 180.0f / CY_PI_F;

inline constexpr f64 CY_PI_D = 3.14159265358979323846264338327950288;
inline constexpr f64 CY_TAU_D = 6.28318530717958647692528676655900576;
inline constexpr f64 CY_HALF_PI_D = 1.57079632679489661923132169163975144;
inline constexpr f64 CY_DEGREES_TO_RADIANS_D = CY_PI_D / 180.0;
inline constexpr f64 CY_RADIANS_TO_DEGREES_D = 180.0 / CY_PI_D;

CYPHER_NODISCARD constexpr f32 Scalar_Square( f32 value ) noexcept
{
    return value * value;
}

CYPHER_NODISCARD constexpr f64 Scalar_Square( f64 value ) noexcept
{
    return value * value;
}

CYPHER_NODISCARD constexpr f32 Scalar_DegreesToRadians( f32 degrees ) noexcept
{
    return degrees * CY_DEGREES_TO_RADIANS_F;
}

CYPHER_NODISCARD constexpr f64 Scalar_DegreesToRadians( f64 degrees ) noexcept
{
    return degrees * CY_DEGREES_TO_RADIANS_D;
}

CYPHER_NODISCARD constexpr f32 Scalar_RadiansToDegrees( f32 radians ) noexcept
{
    return radians * CY_RADIANS_TO_DEGREES_F;
}

CYPHER_NODISCARD constexpr f64 Scalar_RadiansToDegrees( f64 radians ) noexcept
{
    return radians * CY_RADIANS_TO_DEGREES_D;
}

CYPHER_NODISCARD constexpr f32 Scalar_Lerp( f32 a, f32 b, f32 t ) noexcept
{
    return a + ( b - a ) * t;
}

CYPHER_NODISCARD constexpr f64 Scalar_Lerp( f64 a, f64 b, f64 t ) noexcept
{
    return a + ( b - a ) * t;
}

CYPHER_NODISCARD CYPHER_MATH_API bool_t Scalar_IsFinite( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Scalar_IsFinite( f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Scalar_IsNan( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Scalar_IsNan( f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Scalar_NearlyEquals(
    f32 a, f32 b, f32 absoluteTolerance, f32 relativeTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Scalar_NearlyEquals(
    f64 a, f64 b, f64 absoluteTolerance, f64 relativeTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Scalar_IsNearZero(
    f32 value, f32 tolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Scalar_IsNearZero(
    f64 value, f64 tolerance ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Abs( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Scalar_Abs( f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Min( f32 a, f32 b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Scalar_Min( f64 a, f64 b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Max( f32 a, f32 b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Scalar_Max( f64 a, f64 b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Clamp(
    f32 value, f32 minimum, f32 maximum ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Scalar_Clamp(
    f64 value, f64 minimum, f64 maximum ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Saturate( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Scalar_Saturate( f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Sign( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Scalar_Sign( f64 value ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Sqrt( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Scalar_Sqrt( f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_InvSqrt( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Scalar_InvSqrt( f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Sin( f32 radians ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Cos( f32 radians ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Tan( f32 radians ) noexcept;
CYPHER_MATH_API void Scalar_SinCos(
    f32 radians, CY_OUT f32 *pSin, CY_OUT f32 *pCos ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_AsinClamped( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_AcosClamped( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Atan2( f32 y, f32 x ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Floor( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Ceil( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Round( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Truncate( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Fmod( f32 value, f32 divisor ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Repeat( f32 value, f32 length ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_WrapRadiansPositive( f32 radians ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_WrapRadiansSigned( f32 radians ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_InverseLerp(
    f32 a, f32 b, f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_Remap(
    f32 value, f32 sourceMinimum, f32 sourceMaximum,
    f32 destinationMinimum, f32 destinationMaximum ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_SmoothStep(
    f32 edge0, f32 edge1, f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_SmootherStep(
    f32 edge0, f32 edge1, f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Scalar_MoveTowards(
    f32 current, f32 target, f32 maximumDelta ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_SCALAR_H
