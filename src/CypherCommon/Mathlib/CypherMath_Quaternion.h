//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Quaternion.h
//  Purpose: Declares quaternion rotation math.
//  Details: Quaternions use x/y/z vector components and w scalar, represent active
//           right-handed rotations, and compose using column-vector order.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_QUATERNION_H
#define CYPHER_COMMON_MATH_QUATERNION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Angle.h"
#include "CypherMath_Vector3.h"

#include <type_traits>

namespace cypher::math
{

struct quat_t {
    f32 x; // X component of the imaginary/vector part.
    f32 y; // Y component of the imaginary/vector part.
    f32 z; // Z component of the imaginary/vector part.
    f32 w; // Real/scalar component.
};

inline constexpr quat_t CY_QUAT_IDENTITY{ 0.0f, 0.0f, 0.0f, 1.0f }; // No rotation.

// Construction and comparison ---------------------------------------------------
CYPHER_NODISCARD constexpr quat_t Quat_Make( f32 x, f32 y, f32 z, f32 w ) noexcept;
CYPHER_NODISCARD constexpr quat_t Quat_FromVectorScalar( vec3_t vector, f32 scalar ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Quat_VectorPart( quat_t value ) noexcept;
CYPHER_NODISCARD constexpr bool_t Quat_EqualsExact( quat_t a, quat_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_IsFinite( quat_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_NearlyEquals(
    quat_t a, quat_t b, f32 absoluteTolerance, f32 relativeTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_RotationEquivalent(
    quat_t a, quat_t b, f32 toleranceRadians ) noexcept;

// Arithmetic and normalization --------------------------------------------------
CYPHER_NODISCARD constexpr quat_t Quat_Add( quat_t a, quat_t b ) noexcept;
CYPHER_NODISCARD constexpr quat_t Quat_Subtract( quat_t a, quat_t b ) noexcept;
CYPHER_NODISCARD constexpr quat_t Quat_Scale( quat_t value, f32 scale ) noexcept;
CYPHER_NODISCARD constexpr quat_t Quat_Negate( quat_t value ) noexcept;
CYPHER_NODISCARD constexpr quat_t Quat_Conjugate( quat_t value ) noexcept;
CYPHER_NODISCARD constexpr f32 Quat_Dot( quat_t a, quat_t b ) noexcept;
CYPHER_NODISCARD constexpr f32 Quat_LengthSquared( quat_t value ) noexcept;

// Hamilton product: Quat_Multiply(a, b) applies rotation b, then rotation a.
CYPHER_NODISCARD constexpr quat_t Quat_Multiply( quat_t a, quat_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Quat_Length( quat_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API quat_t Quat_NormalizeUnchecked( quat_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_TryNormalize(
    quat_t value, f32 minimumLength, CY_OUT quat_t *pNormalized,
    CY_OUT_OPTIONAL f32 *pOriginalLength ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_TryInverse(
    quat_t value, f32 minimumLength, CY_OUT quat_t *pInverse ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_IsUnit(
    quat_t value, f32 tolerance ) noexcept;

// Vector rotation ----------------------------------------------------------------
// The input quaternion must be unit length.
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Quat_RotateVectorUnit(
    quat_t unitRotation, vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Quat_InverseRotateVectorUnit(
    quat_t unitRotation, vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Quat_Forward( quat_t unitRotation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Quat_Left( quat_t unitRotation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Quat_Up( quat_t unitRotation ) noexcept;

// Rotation construction and decomposition --------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API quat_t Quat_FromUnitAxisAngle(
    vec3_t unitAxis, angle_t angle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_TryFromAxisAngle(
    vec3_t axis, angle_t angle, f32 minimumAxisLength,
    CY_OUT quat_t *pRotation ) noexcept;

// Euler XYZ applies X rotation first, followed by Y, followed by Z.
CYPHER_NODISCARD CYPHER_MATH_API quat_t Quat_FromEulerXYZ(
    vec3_t anglesRadians ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Quat_ToEulerXYZ(
    quat_t unitRotation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_TryToAxisAngle(
    quat_t rotation, f32 minimumLength,
    CY_OUT vec3_t *pUnitAxis, CY_OUT angle_t *pAngle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_TryFromToRotation(
    vec3_t from, vec3_t to, f32 minimumLength,
    CY_OUT quat_t *pRotation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quat_TryLookRotation(
    vec3_t forward, vec3_t upHint, f32 minimumLength,
    CY_OUT quat_t *pRotation ) noexcept;

// Interpolation ------------------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API quat_t Quat_Nlerp(
    quat_t a, quat_t b, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API quat_t Quat_Slerp(
    quat_t a, quat_t b, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API angle_t Quat_AngleBetween(
    quat_t a, quat_t b ) noexcept;

static_assert( sizeof( quat_t ) == 16u );
static_assert( alignof( quat_t ) == alignof( f32 ) );
static_assert( std::is_standard_layout_v<quat_t> );
static_assert( std::is_trivially_copyable_v<quat_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_QUATERNION_INL
    #include "CypherMath_Quaternion.inl"
#endif

#endif // CYPHER_COMMON_MATH_QUATERNION_H
