//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Matrix4.h
//  Purpose: Declares column-major four-by-four matrix math.
//  Details: Matrix4 uses column vectors and explicit projection depth policy for
//           rendering, camera, world, animation, and editor transform pipelines.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_MATRIX4_H
#define CYPHER_COMMON_MATH_MATRIX4_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Matrix3.h"
#include "CypherMath_Vector4.h"

#include <type_traits>

namespace cypher::math
{

enum class clip_depth_range_t : common::u8 {
    NEGATIVE_ONE_TO_ONE = 0u, // OpenGL-style normalized device depth.
    ZERO_TO_ONE,              // Vulkan and Direct3D-style normalized device depth.
    COUNT                     // Enum bound; not a projection policy.
};

struct mat4_t {
    f32 m[16]; // Column-major: m[column * 4 + row].
};

inline constexpr mat4_t CY_MAT4_ZERO{ { 0.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 0.0f } };
inline constexpr mat4_t CY_MAT4_IDENTITY{ { 1.0f, 0.0f, 0.0f, 0.0f,
                                            0.0f, 1.0f, 0.0f, 0.0f,
                                            0.0f, 0.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f, 0.0f, 1.0f } };

// Construction and component access ---------------------------------------------
CYPHER_NODISCARD constexpr u32 Mat4_Index( u32 row, u32 column ) noexcept;
CYPHER_NODISCARD constexpr mat4_t Mat4_FromColumns(
    vec4_t column0, vec4_t column1, vec4_t column2, vec4_t column3 ) noexcept;
CYPHER_NODISCARD constexpr mat4_t Mat4_FromRows(
    vec4_t row0, vec4_t row1, vec4_t row2, vec4_t row3 ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Mat4_Column( mat4_t value, u32 column ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Mat4_Row( mat4_t value, u32 row ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Mat4_Component(
    mat4_t value, u32 row, u32 column ) noexcept;
CYPHER_MATH_API void Mat4_SetComponent(
    CY_INOUT mat4_t *pValue, u32 row, u32 column, f32 component ) noexcept;

// Arithmetic and application -----------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_IsFinite( mat4_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_NearlyEquals(
    mat4_t a, mat4_t b, f32 absoluteTolerance, f32 relativeTolerance ) noexcept;
CYPHER_NODISCARD constexpr mat4_t Mat4_Add( mat4_t a, mat4_t b ) noexcept;
CYPHER_NODISCARD constexpr mat4_t Mat4_Subtract( mat4_t a, mat4_t b ) noexcept;
CYPHER_NODISCARD constexpr mat4_t Mat4_Scale( mat4_t value, f32 scale ) noexcept;
CYPHER_NODISCARD constexpr mat4_t Mat4_Transpose( mat4_t value ) noexcept;

// Mat4_Multiply(a, b) applies b first, then a.
CYPHER_NODISCARD constexpr mat4_t Mat4_Multiply( mat4_t a, mat4_t b ) noexcept;
CYPHER_NODISCARD constexpr vec4_t Mat4_TransformVector4(
    mat4_t matrix, vec4_t vector ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Mat4_TransformPointAffine(
    mat4_t matrix, vec3_t point ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Mat4_TransformDirection(
    mat4_t matrix, vec3_t direction ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_TryProjectPoint(
    mat4_t matrix, vec3_t point, f32 minimumAbsW,
    CY_OUT vec3_t *pProjected ) noexcept;

// Inversion and affine construction ---------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API f32 Mat4_Determinant( mat4_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_TryInverse(
    mat4_t value, f32 minimumAbsPivot, CY_OUT mat4_t *pInverse ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_IsAffine(
    mat4_t value, f32 tolerance ) noexcept;

CYPHER_NODISCARD constexpr mat4_t Mat4_FromTranslation( vec3_t translation ) noexcept;
CYPHER_NODISCARD constexpr mat4_t Mat4_FromScale( vec3_t scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API mat4_t Mat4_FromQuaternion(
    quat_t unitRotation ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API mat4_t Mat4_FromTRS(
    vec3_t translation, quat_t unitRotation, vec3_t scale ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Mat4_Translation( mat4_t value ) noexcept;
CYPHER_NODISCARD constexpr mat3_t Mat4_LinearPart( mat4_t value ) noexcept;

// Camera and projection ----------------------------------------------------------
// View space is conventional right-handed camera space looking down negative Z.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_TryLookAtRH(
    vec3_t eye, vec3_t target, vec3_t upHint, f32 minimumDirectionLength,
    CY_OUT mat4_t *pView ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_TryPerspectiveRH(
    angle_t verticalFieldOfView, f32 aspectRatio, f32 nearDistance,
    f32 farDistance, clip_depth_range_t depthRange,
    CY_OUT mat4_t *pProjection ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_TryPerspectiveInfiniteRH(
    angle_t verticalFieldOfView, f32 aspectRatio, f32 nearDistance,
    clip_depth_range_t depthRange, CY_OUT mat4_t *pProjection ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Mat4_TryOrthographicRH(
    f32 left, f32 right, f32 bottom, f32 top,
    f32 nearDistance, f32 farDistance, clip_depth_range_t depthRange,
    CY_OUT mat4_t *pProjection ) noexcept;

static_assert( sizeof( mat4_t ) == sizeof( f32 ) * 16u );
static_assert( alignof( mat4_t ) == alignof( f32 ) );
static_assert( std::is_standard_layout_v<mat4_t> );
static_assert( std::is_trivially_copyable_v<mat4_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_MATRIX4_INL
    #include "CypherMath_Matrix4.inl"
#endif

#endif // CYPHER_COMMON_MATH_MATRIX4_H
