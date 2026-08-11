//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_FixedPoint.cpp
//  Purpose: Implements checked signed 16.16 fixed-point arithmetic.
//  Details: Multiplication and division round to nearest with ties away from zero;
//           all narrowing conversions are range-checked before assignment.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_FixedPoint.h"

#include "CypherCommon_Assert.h"

#include <cmath>

namespace cypher::math
{

namespace
{

bool_t TryNarrowRaw( common::i64 raw, fixed16_16_t *pResult ) noexcept
{
    if ( raw < common::CY_I32_MIN || raw > common::CY_I32_MAX ) {
        return false;
    }
    pResult->raw = static_cast<i32>( raw );
    return true;
}

common::i64 DivideRounded( common::i64 numerator, common::i64 denominator ) noexcept
{
    const common::i64 quotient = numerator / denominator;
    const common::i64 remainder = numerator % denominator;
    const common::u64 absRemainder = remainder < 0
        ? static_cast<common::u64>( -remainder )
        : static_cast<common::u64>( remainder );
    const common::u64 absDenominator = denominator < 0
        ? static_cast<common::u64>( -denominator )
        : static_cast<common::u64>( denominator );
    if ( absRemainder * 2u < absDenominator ) {
        return quotient;
    }
    const bool_t bNegative = ( numerator < 0 ) != ( denominator < 0 );
    return quotient + ( bNegative ? -1 : 1 );
}

} // namespace

bool_t Fixed16_16_TryFromI32( i32 value, fixed16_16_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Fixed16_16_TryFromI32 requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_FIXED16_16_ZERO;
    return TryNarrowRaw(
        static_cast<common::i64>( value ) * CY_FIXED16_16_SCALE, pResult );
}

bool_t Fixed16_16_TryFromF32( f32 value, fixed16_16_t *pResult ) noexcept
{
    return Fixed16_16_TryFromF64( static_cast<f64>( value ), pResult );
}

bool_t Fixed16_16_TryFromF64( f64 value, fixed16_16_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Fixed16_16_TryFromF64 requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_FIXED16_16_ZERO;
    if ( !Scalar_IsFinite( value ) ) {
        return false;
    }
    const f64 scaled = value * static_cast<f64>( CY_FIXED16_16_SCALE );
    if ( scaled < static_cast<f64>( common::CY_I32_MIN ) - 0.5 ||
         scaled > static_cast<f64>( common::CY_I32_MAX ) + 0.5 ) {
        return false;
    }
    return TryNarrowRaw( static_cast<common::i64>( std::round( scaled ) ), pResult );
}

bool_t Fixed16_16_TryAdd(
    fixed16_16_t a,
    fixed16_16_t b,
    fixed16_16_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Fixed16_16_TryAdd requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_FIXED16_16_ZERO;
    return TryNarrowRaw(
        static_cast<common::i64>( a.raw ) + b.raw, pResult );
}

bool_t Fixed16_16_TrySubtract(
    fixed16_16_t a,
    fixed16_16_t b,
    fixed16_16_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Fixed16_16_TrySubtract requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_FIXED16_16_ZERO;
    return TryNarrowRaw(
        static_cast<common::i64>( a.raw ) - b.raw, pResult );
}

bool_t Fixed16_16_TryMultiply(
    fixed16_16_t a,
    fixed16_16_t b,
    fixed16_16_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Fixed16_16_TryMultiply requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_FIXED16_16_ZERO;
    const common::i64 product =
        static_cast<common::i64>( a.raw ) * b.raw;
    return TryNarrowRaw(
        DivideRounded( product, CY_FIXED16_16_SCALE ), pResult );
}

bool_t Fixed16_16_TryDivide(
    fixed16_16_t numerator,
    fixed16_16_t denominator,
    fixed16_16_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Fixed16_16_TryDivide requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_FIXED16_16_ZERO;
    if ( denominator.raw == 0 ) {
        return false;
    }
    const common::i64 scaledNumerator =
        static_cast<common::i64>( numerator.raw ) * CY_FIXED16_16_SCALE;
    return TryNarrowRaw(
        DivideRounded( scaledNumerator, denominator.raw ), pResult );
}

i32 Fixed16_16_FloorToI32( fixed16_16_t value ) noexcept
{
    i32 result = value.raw / CY_FIXED16_16_SCALE;
    if ( value.raw < 0 && value.raw % CY_FIXED16_16_SCALE != 0 ) {
        --result;
    }
    return result;
}

i32 Fixed16_16_CeilToI32( fixed16_16_t value ) noexcept
{
    i32 result = value.raw / CY_FIXED16_16_SCALE;
    if ( value.raw > 0 && value.raw % CY_FIXED16_16_SCALE != 0 ) {
        ++result;
    }
    return result;
}

i32 Fixed16_16_RoundToI32( fixed16_16_t value ) noexcept
{
    return static_cast<i32>( DivideRounded( value.raw, CY_FIXED16_16_SCALE ) );
}

} // namespace cypher::math
