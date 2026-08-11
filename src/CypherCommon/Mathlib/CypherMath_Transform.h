//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Transform.h
//  Purpose: Declares decomposed translation-rotation-scale transforms.
//  Details: Transform is the ergonomic authoring representation. Exact composition
//           returns Affine3 because rotated nonuniform scales can create shear.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_TRANSFORM_H
#define CYPHER_COMMON_MATH_TRANSFORM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Affine3.h"

#include <type_traits>

namespace cypher::math
{

struct transform_t {
    vec3_t position;
    quat_t rotation;
    vec3_t scale;
};

inline constexpr transform_t CY_TRANSFORM_IDENTITY{
    CY_VEC3_ZERO,
    CY_QUAT_IDENTITY,
    CY_VEC3_ONE
};

CYPHER_NODISCARD constexpr transform_t Transform_Make(
    vec3_t position, quat_t rotation, vec3_t scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Transform_IsFinite(
    transform_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Transform_NearlyEquals(
    transform_t a, transform_t b,
    f32 linearAbsoluteTolerance, f32 linearRelativeTolerance,
    f32 angularToleranceRadians ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Transform_HasUniformScale(
    transform_t value, f32 tolerance ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API vec3_t Transform_TransformPoint(
    transform_t transform, vec3_t point ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Transform_TransformDirection(
    transform_t transform, vec3_t direction ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Transform_TryInversePoint(
    transform_t transform, vec3_t point, f32 minimumAbsScale,
    CY_OUT vec3_t *pLocalPoint ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Transform_TryInverseDirection(
    transform_t transform, vec3_t direction, f32 minimumAbsScale,
    CY_OUT vec3_t *pLocalDirection ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API affine3_t Transform_ToAffine3(
    transform_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API mat4_t Transform_ToMat4(
    transform_t value ) noexcept;

// Composition is exact and may contain shear, so it returns Affine3.
CYPHER_NODISCARD CYPHER_MATH_API affine3_t Transform_ComposeAffine(
    transform_t parent, transform_t local ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Transform_TryInverseAffine(
    transform_t value, f32 minimumAbsDeterminant,
    CY_OUT affine3_t *pInverse ) noexcept;

// Decomposition rejects shear and stores reflection on one scale axis.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Transform_TryFromAffine3(
    affine3_t value, f32 minimumAbsScale, f32 orthogonalityTolerance,
    CY_OUT transform_t *pTransform ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API transform_t Transform_Interpolate(
    transform_t a, transform_t b, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API transform_t Transform_InterpolateClamped(
    transform_t a, transform_t b, f32 t ) noexcept;

static_assert( sizeof( transform_t ) == sizeof( f32 ) * 10u );
static_assert( std::is_standard_layout_v<transform_t> );
static_assert( std::is_trivially_copyable_v<transform_t> );

} // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_TRANSFORM_INL
    #include "CypherMath_Transform.inl"
#endif

#endif // CYPHER_COMMON_MATH_TRANSFORM_H
