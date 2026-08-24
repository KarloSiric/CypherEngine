//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Quantization.h
//  Purpose: Declares bounded scalar, vector, angle, and rotation quantization.
//  Details: Bit width and numerical ranges remain explicit so networking and
//           cooked formats can version their binary contracts independently.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_QUANTIZATION_H
#define CYPHER_COMMON_MATH_QUANTIZATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Quaternion.h"

#include <type_traits>

namespace cypher::math
{

struct quantized_vec3_t {
    u32 x; // Encoded X code using the caller-selected bit width.
    u32 y; // Encoded Y code using the caller-selected bit width.
    u32 z; // Encoded Z code using the caller-selected bit width.
};

// The largest quaternion component is reconstructed as nonnegative.
struct quantized_quat_t {
    u32 largestComponent; // Index [0, 3] of the omitted quaternion component.
    u32 components[3];    // Encoded remaining components in original order.
};

CYPHER_NODISCARD CYPHER_MATH_API u32 Quantization_MaxCode(
    u32 cBits ) noexcept;

// Encoding clamps finite values to the documented normalized interval.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryEncodeUnorm(
    f32 value, u32 cBits, CY_OUT u32 *pCode ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryDecodeUnorm(
    u32 code, u32 cBits, CY_OUT f32 *pValue ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryEncodeSnorm(
    f32 value, u32 cBits, CY_OUT u32 *pCode ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryDecodeSnorm(
    u32 code, u32 cBits, CY_OUT f32 *pValue ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryEncodeRange(
    f32 value, f32 minimum, f32 maximum, u32 cBits,
    CY_OUT u32 *pCode ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryDecodeRange(
    u32 code, f32 minimum, f32 maximum, u32 cBits,
    CY_OUT f32 *pValue ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryEncodeVec3Range(
    vec3_t value, vec3_t minimum, vec3_t maximum, u32 cBits,
    CY_OUT quantized_vec3_t *pCode ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryDecodeVec3Range(
    quantized_vec3_t code, vec3_t minimum, vec3_t maximum, u32 cBits,
    CY_OUT vec3_t *pValue ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryEncodeAngle(
    angle_t angle, u32 cBits, CY_OUT u32 *pCode ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryDecodeAngle(
    u32 code, u32 cBits, CY_OUT angle_t *pAngle ) noexcept;

// Smallest-three encoding uses two index bits plus three cBits-wide components.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryEncodeQuatSmallestThree(
    quat_t rotation,
    u32 cBitsPerComponent,
    f32 minimumLength,
    CY_OUT quantized_quat_t *pCode ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Quantization_TryDecodeQuatSmallestThree(
    quantized_quat_t code,
    u32 cBitsPerComponent,
    f32 minimumLength,
    CY_OUT quat_t *pRotation ) noexcept;

static_assert( sizeof( quantized_vec3_t ) == sizeof( u32 ) * 3u );
static_assert( sizeof( quantized_quat_t ) == sizeof( u32 ) * 4u );
static_assert( std::is_trivially_copyable_v<quantized_quat_t> );

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_QUANTIZATION_H
