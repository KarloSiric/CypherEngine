//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_FixedPoint.h
//  Purpose: Declares checked signed 16.16 fixed-point arithmetic.
//  Details: Operations use explicit overflow reporting and deterministic integer
//           intermediates for serialization, tools, and bounded simulation data.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Fixed Point Contract

Scalar helpers state their domain, clamping, and precision behavior explicitly. Callers choose
tolerances; the library does not hide unit conversions or exceptional values.
================
*/

#ifndef CYPHER_COMMON_MATH_FIXEDPOINT_H
#define CYPHER_COMMON_MATH_FIXEDPOINT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Scalar.h"

#include <type_traits>

namespace cypher::math
{

using common::i32;

struct fixed16_16_t {
    i32 raw; // Signed value with sixteen integer and sixteen fractional bits.
};

inline constexpr i32 CY_FIXED16_16_FRACTION_BITS = 16; // Fractional bit count.
inline constexpr i32 CY_FIXED16_16_SCALE = 1 << CY_FIXED16_16_FRACTION_BITS; // Raw units per integer.
inline constexpr fixed16_16_t CY_FIXED16_16_ZERO{ 0 }; // Exact numeric zero.
inline constexpr fixed16_16_t CY_FIXED16_16_ONE{ CY_FIXED16_16_SCALE }; // Exact numeric one.

CYPHER_NODISCARD constexpr fixed16_16_t Fixed16_16_FromRaw( i32 raw ) noexcept
{
    return { raw };
}

CYPHER_NODISCARD constexpr i32 Fixed16_16_Raw( fixed16_16_t value ) noexcept
{
    return value.raw;
}

// Checked conversions ------------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Fixed16_16_TryFromI32(
    i32 value, CY_OUT fixed16_16_t *pResult ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Fixed16_16_TryFromF32(
    f32 value, CY_OUT fixed16_16_t *pResult ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Fixed16_16_TryFromF64(
    f64 value, CY_OUT fixed16_16_t *pResult ) noexcept;
CYPHER_NODISCARD constexpr f32 Fixed16_16_ToF32( fixed16_16_t value ) noexcept
{
    return static_cast<f32>( value.raw ) /
           static_cast<f32>( CY_FIXED16_16_SCALE );
}
CYPHER_NODISCARD constexpr f64 Fixed16_16_ToF64( fixed16_16_t value ) noexcept
{
    return static_cast<f64>( value.raw ) /
           static_cast<f64>( CY_FIXED16_16_SCALE );
}

// Checked arithmetic -------------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Fixed16_16_TryAdd(
    fixed16_16_t a, fixed16_16_t b,
    CY_OUT fixed16_16_t *pResult ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Fixed16_16_TrySubtract(
    fixed16_16_t a, fixed16_16_t b,
    CY_OUT fixed16_16_t *pResult ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Fixed16_16_TryMultiply(
    fixed16_16_t a, fixed16_16_t b,
    CY_OUT fixed16_16_t *pResult ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Fixed16_16_TryDivide(
    fixed16_16_t numerator, fixed16_16_t denominator,
    CY_OUT fixed16_16_t *pResult ) noexcept;

// Integral rounding --------------------------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API i32 Fixed16_16_FloorToI32(
    fixed16_16_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API i32 Fixed16_16_CeilToI32(
    fixed16_16_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API i32 Fixed16_16_RoundToI32(
    fixed16_16_t value ) noexcept;

static_assert( sizeof( fixed16_16_t ) == sizeof( i32 ) );
static_assert( std::is_trivially_copyable_v<fixed16_16_t> );

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_FIXEDPOINT_H
