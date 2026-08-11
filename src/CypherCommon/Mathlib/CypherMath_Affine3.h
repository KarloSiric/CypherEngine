//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Affine3.h
//  Purpose: Declares compact three-dimensional affine transforms.
//  Details: Affine3 stores a three-by-three linear transform plus translation.
//           It preserves scale, reflection, and shear without a redundant last row.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_AFFINE3_H
#define CYPHER_COMMON_MATH_AFFINE3_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Matrix4.h"

#include <type_traits>

namespace cypher::math
{

struct affine3_t {
    f32 m[12];
};

inline constexpr affine3_t CY_AFFINE3_IDENTITY{ {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f
} };

CYPHER_NODISCARD constexpr u32 Affine3_Index( u32 row, u32 column ) noexcept;
CYPHER_NODISCARD constexpr affine3_t Affine3_FromColumns(
    vec3_t column0, vec3_t column1, vec3_t column2,
    vec3_t translation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Affine3_Component(
    affine3_t value, u32 row, u32 column ) noexcept;
CYPHER_MATH_API void Affine3_SetComponent(
    CY_INOUT affine3_t *pValue, u32 row, u32 column, f32 component ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Affine3_Column(
    affine3_t value, u32 column ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Affine3_LinearPart( affine3_t value ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Affine3_Translation( affine3_t value ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3_IsFinite(
    affine3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3_NearlyEquals(
    affine3_t a, affine3_t b,
    f32 absoluteTolerance, f32 relativeTolerance ) noexcept;

// Affine3_Multiply(a, b) applies b first, then a.
CYPHER_NODISCARD constexpr affine3_t Affine3_Multiply(
    affine3_t a, affine3_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Affine3_TransformPoint(
    affine3_t transform, vec3_t point ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Affine3_TransformDirection(
    affine3_t transform, vec3_t direction ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3_TryTransformNormal(
    affine3_t transform, vec3_t normal, f32 minimumAbsDeterminant,
    CY_OUT vec3_t *pTransformed ) noexcept;

CYPHER_NODISCARD constexpr affine3_t Affine3_FromTranslation(
    vec3_t translation ) noexcept;
CYPHER_NODISCARD constexpr affine3_t Affine3_FromScale( vec3_t scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API affine3_t Affine3_FromQuaternion(
    quat_t unitRotation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API affine3_t Affine3_FromTRS(
    vec3_t translation, quat_t unitRotation, vec3_t scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3_TryInverse(
    affine3_t value, f32 minimumAbsDeterminant,
    CY_OUT affine3_t *pInverse ) noexcept;

CYPHER_NODISCARD constexpr mat4_t Affine3_ToMat4( affine3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3_TryFromMat4(
    mat4_t value, f32 affineTolerance, CY_OUT affine3_t *pAffine ) noexcept;

static_assert( sizeof( affine3_t ) == sizeof( f32 ) * 12u );
static_assert( alignof( affine3_t ) == alignof( f32 ) );
static_assert( std::is_standard_layout_v<affine3_t> );
static_assert( std::is_trivially_copyable_v<affine3_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_AFFINE3_INL
    #include "CypherMath_Affine3.inl"
#endif

#endif // CYPHER_COMMON_MATH_AFFINE3_H
