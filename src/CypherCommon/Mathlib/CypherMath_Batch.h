//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Batch.h
//  Purpose: Declares four-lane SIMD-friendly vector math.
//  Details: Batch math uses a structure-of-arrays layout and exposes scalar,
//           SSE2, and NEON implementations without changing public data types.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_BATCH_H
#define CYPHER_COMMON_MATH_BATCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Matrix4.h"

#include <type_traits>

namespace cypher::math
{

using common::usize;

inline constexpr u32 CY_MATH_BATCH_LANES = 4u;

enum class math_batch_backend_t : common::u8 {
    SCALAR = 0u,
    SSE2,
    NEON,
    COUNT
};

struct alignas( 16 ) f32_soa4_t {
    f32 lane[CY_MATH_BATCH_LANES];
};

struct alignas( 16 ) vec3_soa4_t {
    f32 x[CY_MATH_BATCH_LANES];
    f32 y[CY_MATH_BATCH_LANES];
    f32 z[CY_MATH_BATCH_LANES];
};

// Reports the backend compiled into this translation unit.
CYPHER_NODISCARD CYPHER_MATH_API math_batch_backend_t
MathBatch_CompiledBackend() noexcept;

// Gathers/scatters exactly four tightly packed vec3_t values.
CYPHER_NODISCARD CYPHER_MATH_API vec3_soa4_t Vec3Soa4_Load(
    CY_IN_READS( CY_MATH_BATCH_LANES ) const vec3_t *pValues ) noexcept;
CYPHER_MATH_API void Vec3Soa4_Store(
    vec3_soa4_t values,
    CY_OUT_WRITES( CY_MATH_BATCH_LANES ) vec3_t *pValues ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API vec3_soa4_t Vec3Soa4_Add(
    vec3_soa4_t a, vec3_soa4_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_soa4_t Vec3Soa4_Subtract(
    vec3_soa4_t a, vec3_soa4_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_soa4_t Vec3Soa4_Scale(
    vec3_soa4_t values, f32 scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_soa4_t Vec3Soa4_MulAdd(
    vec3_soa4_t a, vec3_soa4_t b, f32 scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32_soa4_t Vec3Soa4_Dot(
    vec3_soa4_t a, vec3_soa4_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32_soa4_t Vec3Soa4_LengthSquared(
    vec3_soa4_t values ) noexcept;

// Matrix inputs must be affine. These calls do not validate hot-path inputs.
CYPHER_NODISCARD CYPHER_MATH_API vec3_soa4_t Vec3Soa4_TransformPointsAffine(
    mat4_t matrix, vec3_soa4_t points ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_soa4_t Vec3Soa4_TransformDirections(
    mat4_t matrix, vec3_soa4_t directions ) noexcept;

// Array forms permit pOutput == pInput and handle a scalar tail.
CYPHER_MATH_API void Vec3Batch_TransformPointsAffine(
    mat4_t matrix,
    CY_IN_READS( cValues ) const vec3_t *pInput,
    CY_OUT_WRITES( cValues ) vec3_t *pOutput,
    usize cValues ) noexcept;
CYPHER_MATH_API void Vec3Batch_TransformDirections(
    mat4_t matrix,
    CY_IN_READS( cValues ) const vec3_t *pInput,
    CY_OUT_WRITES( cValues ) vec3_t *pOutput,
    usize cValues ) noexcept;
CYPHER_MATH_API void Vec3Batch_Dot(
    CY_IN_READS( cValues ) const vec3_t *pA,
    CY_IN_READS( cValues ) const vec3_t *pB,
    CY_OUT_WRITES( cValues ) f32 *pOutput,
    usize cValues ) noexcept;

static_assert( sizeof( f32_soa4_t ) == sizeof( f32 ) * CY_MATH_BATCH_LANES );
static_assert( sizeof( vec3_soa4_t ) == sizeof( f32 ) * 12u );
static_assert( alignof( f32_soa4_t ) == 16u );
static_assert( alignof( vec3_soa4_t ) == 16u );
static_assert( std::is_standard_layout_v<vec3_soa4_t> );
static_assert( std::is_trivially_copyable_v<vec3_soa4_t> );

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_BATCH_H
