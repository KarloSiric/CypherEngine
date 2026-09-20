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
    f32 m[12]; // Four column-major vec3 columns: linear basis then translation.
};

inline constexpr affine3_t CY_AFFINE3_IDENTITY{ {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f
} };

// Construction and component access ---------------------------------------------
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

// Queries and application --------------------------------------------------------
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

// Common transform construction -------------------------------------------------
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

// Full-matrix conversion ---------------------------------------------------------
CYPHER_NODISCARD constexpr mat4_t Affine3_ToMat4( affine3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3_TryFromMat4(
    mat4_t value, f32 affineTolerance, CY_OUT affine3_t *pAffine ) noexcept;

static_assert( sizeof( affine3_t ) == sizeof( f32 ) * 12u );
static_assert( alignof( affine3_t ) == alignof( f32 ) );
static_assert( std::is_standard_layout_v<affine3_t> );
static_assert( std::is_trivially_copyable_v<affine3_t> );

// Binary64 authoring affine transform ---------------------------------------------
// No mat3d_t/mat4d_t exists yet (deferred, no consumer), so this type stands alone:
// it is built from and reasoned about purely in terms of vec3d_t columns. Rotation
// composition still happens in f32 via quaternion/Affine3 and crosses the precision
// boundary through Affine3d_FromAffine3 at the point it needs to apply to double
// geometry. Affine3_Index is reused as-is: it is pure integer index arithmetic with
// no dependency on the component type.
struct affine3d_t {
    f64 m[12]; // Four column-major vec3d columns: linear basis then translation.
};

inline constexpr affine3d_t CY_AFFINE3D_IDENTITY{ {
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0,
    0.0, 0.0, 1.0,
    0.0, 0.0, 0.0
} };

// Construction and component access ---------------------------------------------
CYPHER_NODISCARD constexpr affine3d_t Affine3d_FromColumns(
    vec3d_t column0, vec3d_t column1, vec3d_t column2,
    vec3d_t translation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Affine3d_Component(
    affine3d_t value, u32 row, u32 column ) noexcept;
CYPHER_MATH_API void Affine3d_SetComponent(
    CY_INOUT affine3d_t *pValue, u32 row, u32 column, f64 component ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Affine3d_Column(
    affine3d_t value, u32 column ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Affine3d_Translation( affine3d_t value ) noexcept;

// Precision conversion ------------------------------------------------------------
CYPHER_NODISCARD constexpr affine3d_t Affine3d_FromAffine3( affine3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3d_TryToAffine3(
    affine3d_t value, CY_OUT affine3_t *pResult ) noexcept;

// Queries and application --------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3d_IsFinite(
    affine3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3d_NearlyEquals(
    affine3d_t a, affine3d_t b,
    f64 absoluteTolerance, f64 relativeTolerance ) noexcept;

CYPHER_NODISCARD constexpr affine3d_t Affine3d_Multiply(
    affine3d_t a, affine3d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Affine3d_TransformPoint(
    affine3d_t transform, vec3d_t point ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Affine3d_TransformDirection(
    affine3d_t transform, vec3d_t direction ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3d_TryTransformNormal(
    affine3d_t transform, vec3d_t normal, f64 minimumAbsDeterminant,
    CY_OUT vec3d_t *pTransformed ) noexcept;

// Common transform construction -------------------------------------------------
CYPHER_NODISCARD constexpr affine3d_t Affine3d_FromTranslation(
    vec3d_t translation ) noexcept;
CYPHER_NODISCARD constexpr affine3d_t Affine3d_FromScale( vec3d_t scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Affine3d_TryInverse(
    affine3d_t value, f64 minimumAbsDeterminant,
    CY_OUT affine3d_t *pInverse ) noexcept;

static_assert( sizeof( affine3d_t ) == sizeof( f64 ) * 12u );
static_assert( alignof( affine3d_t ) == alignof( f64 ) );
static_assert( std::is_standard_layout_v<affine3d_t> );
static_assert( std::is_trivially_copyable_v<affine3d_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_AFFINE3_INL
    #include "CypherMath_Affine3.inl"
#endif

#endif // CYPHER_COMMON_MATH_AFFINE3_H
