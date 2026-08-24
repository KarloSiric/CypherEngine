//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Matrix3.h
//  Purpose: Declares column-major three-by-three matrix math.
//  Details: Matrix3 stores linear transforms for rotation, scale, normal handling,
//           inertia work, and editor geometry without translation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_MATRIX3_H
#define CYPHER_COMMON_MATH_MATRIX3_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Quaternion.h"

#include <type_traits>

namespace cypher::math
{

struct mat3_t {
    f32 m[9]; // Column-major: m[column * 3 + row].
};

// Constants are laid out as three contiguous columns.
inline constexpr mat3_t CY_MAT3_ZERO{ { 0.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f } };
inline constexpr mat3_t CY_MAT3_IDENTITY{ { 1.0f, 0.0f, 0.0f,
                                            0.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f, 1.0f } };

// Construction and component access ---------------------------------------------
CYPHER_NODISCARD constexpr u32 Mat3_Index( u32 row, u32 column ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Mat3_FromColumns(
    vec3_t column0, vec3_t column1, vec3_t column2 ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Mat3_FromRows(
    vec3_t row0, vec3_t row1, vec3_t row2 ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Mat3_Column( mat3_t value, u32 column ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Mat3_Row( mat3_t value, u32 row ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Mat3_Component(
    mat3_t value, u32 row, u32 column ) noexcept;
CYPHER_MATH_API void Mat3_SetComponent(
    CY_INOUT mat3_t *pValue, u32 row, u32 column, f32 component ) noexcept;

// Arithmetic ---------------------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat3_IsFinite( mat3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat3_NearlyEquals(
    mat3_t a, mat3_t b, f32 absoluteTolerance, f32 relativeTolerance ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Mat3_Add( mat3_t a, mat3_t b ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Mat3_Subtract( mat3_t a, mat3_t b ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Mat3_Scale( mat3_t value, f32 scale ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Mat3_Transpose( mat3_t value ) noexcept;

// Mat3_Multiply(a, b) applies b first, then a.
CYPHER_NODISCARD constexpr mat3_t Mat3_Multiply( mat3_t a, mat3_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Mat3_TransformVector(
    mat3_t matrix, vec3_t vector ) noexcept;
CYPHER_NODISCARD constexpr f32 Mat3_Determinant( mat3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat3_TryInverse(
    mat3_t value, f32 minimumAbsDeterminant, CY_OUT mat3_t *pInverse ) noexcept;

// Rotation and basis helpers -----------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API mat3_t Mat3_FromQuaternion(
    quat_t unitRotation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat3_TryToQuaternion(
    mat3_t rotation, f32 minimumColumnLength, CY_OUT quat_t *pRotation ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Mat3_FromScale( vec3_t scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat3_IsOrthonormal(
    mat3_t value, f32 tolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat3_TryOrthonormalize(
    mat3_t value, f32 minimumColumnLength, CY_OUT mat3_t *pResult ) noexcept;

static_assert( sizeof( mat3_t ) == sizeof( f32 ) * 9u );
static_assert( alignof( mat3_t ) == alignof( f32 ) );
static_assert( std::is_standard_layout_v<mat3_t> );
static_assert( std::is_trivially_copyable_v<mat3_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_MATRIX3_INL
    #include "CypherMath_Matrix3.inl"
#endif

#endif // CYPHER_COMMON_MATH_MATRIX3_H
