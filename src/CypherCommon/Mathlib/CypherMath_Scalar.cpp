//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Scalar.cpp
//  Purpose: Implements shared scalar math behavior.
//  Details: Checked range operations assert programmer errors while preserving
//           deterministic fallback behavior when assertions are compiled out.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

#include <algorithm>
#include <cmath>

namespace cypher::math
{

namespace
{

template<typename scalar_t>
CYPHER_NODISCARD bool_t Scalar_NearlyEqualsImpl(
    scalar_t a,
    scalar_t b,
    scalar_t absoluteTolerance,
    scalar_t relativeTolerance ) noexcept
{
    const bool_t bValidTolerances = std::isfinite( absoluteTolerance ) &&
                                    std::isfinite( relativeTolerance ) &&
                                    absoluteTolerance >= scalar_t{ 0 } &&
                                    relativeTolerance >= scalar_t{ 0 };
    CY_ASSERT_MSG(
        bValidTolerances,
        "Scalar_NearlyEquals requires finite nonnegative tolerances." );
    if ( !bValidTolerances ) {
        return false;
    }
    if ( a == b ) {
        return true;
    }
    if ( !std::isfinite( a ) || !std::isfinite( b ) ) {
        return false;
    }

    // Absolute tolerance protects values near zero; relative tolerance scales
    // with the larger operand for values far from zero.
    const scalar_t difference = std::fabs( a - b );
    if ( !std::isfinite( difference ) ) {
        return false;
    }
    const scalar_t scale = std::max( std::fabs( a ), std::fabs( b ) );
    return difference <= std::max( absoluteTolerance, relativeTolerance * scale );
}

template<typename scalar_t>
CYPHER_NODISCARD scalar_t Scalar_ClampImpl(
    scalar_t value,
    scalar_t minimum,
    scalar_t maximum ) noexcept
{
    const bool_t bValidBounds = minimum <= maximum;
    CY_ASSERT_MSG( bValidBounds, "Scalar_Clamp requires ordered bounds." );
    return bValidBounds ? std::clamp( value, minimum, maximum ) : value;
}

} // namespace

bool_t Scalar_IsFinite( f32 value ) noexcept { return std::isfinite( value ); }
bool_t Scalar_IsFinite( f64 value ) noexcept { return std::isfinite( value ); }
bool_t Scalar_IsNan( f32 value ) noexcept { return std::isnan( value ); }
bool_t Scalar_IsNan( f64 value ) noexcept { return std::isnan( value ); }

bool_t Scalar_NearlyEquals(
    f32 a, f32 b, f32 absoluteTolerance, f32 relativeTolerance ) noexcept
{
    return Scalar_NearlyEqualsImpl( a, b, absoluteTolerance, relativeTolerance );
}

bool_t Scalar_NearlyEquals(
    f64 a, f64 b, f64 absoluteTolerance, f64 relativeTolerance ) noexcept
{
    return Scalar_NearlyEqualsImpl( a, b, absoluteTolerance, relativeTolerance );
}

bool_t Scalar_IsNearZero( f32 value, f32 tolerance ) noexcept
{
    return Scalar_NearlyEquals( value, 0.0f, tolerance, 0.0f );
}

bool_t Scalar_IsNearZero( f64 value, f64 tolerance ) noexcept
{
    return Scalar_NearlyEquals( value, 0.0, tolerance, 0.0 );
}

f32 Scalar_Abs( f32 value ) noexcept { return std::fabs( value ); }
f64 Scalar_Abs( f64 value ) noexcept { return std::fabs( value ); }
f32 Scalar_Min( f32 a, f32 b ) noexcept { return std::min( a, b ); }
f64 Scalar_Min( f64 a, f64 b ) noexcept { return std::min( a, b ); }
f32 Scalar_Max( f32 a, f32 b ) noexcept { return std::max( a, b ); }
f64 Scalar_Max( f64 a, f64 b ) noexcept { return std::max( a, b ); }

f32 Scalar_Clamp( f32 value, f32 minimum, f32 maximum ) noexcept
{
    return Scalar_ClampImpl( value, minimum, maximum );
}

f64 Scalar_Clamp( f64 value, f64 minimum, f64 maximum ) noexcept
{
    return Scalar_ClampImpl( value, minimum, maximum );
}

f32 Scalar_Saturate( f32 value ) noexcept { return Scalar_Clamp( value, 0.0f, 1.0f ); }
f64 Scalar_Saturate( f64 value ) noexcept { return Scalar_Clamp( value, 0.0, 1.0 ); }

f32 Scalar_Sign( f32 value ) noexcept
{
    // Boolean subtraction produces -1, 0, or +1 without treating zero as positive.
    return static_cast<f32>( ( 0.0f < value ) - ( value < 0.0f ) );
}

f64 Scalar_Sign( f64 value ) noexcept
{
    return static_cast<f64>( ( 0.0 < value ) - ( value < 0.0 ) );
}

f32 Scalar_Sqrt( f32 value ) noexcept { return std::sqrt( value ); }
f64 Scalar_Sqrt( f64 value ) noexcept { return std::sqrt( value ); }

f32 Scalar_InvSqrt( f32 value ) noexcept
{
    CY_ASSERT_MSG( value > 0.0f, "Scalar_InvSqrt requires a positive value." );
    return value > 0.0f ? 1.0f / std::sqrt( value ) : 0.0f;
}

f64 Scalar_InvSqrt( f64 value ) noexcept
{
    CY_ASSERT_MSG( value > 0.0, "Scalar_InvSqrt requires a positive value." );
    return value > 0.0 ? 1.0 / std::sqrt( value ) : 0.0;
}

f32 Scalar_Sin( f32 radians ) noexcept { return std::sin( radians ); }
f32 Scalar_Cos( f32 radians ) noexcept { return std::cos( radians ); }
f32 Scalar_Tan( f32 radians ) noexcept { return std::tan( radians ); }

void Scalar_SinCos( f32 radians, f32 *pSin, f32 *pCos ) noexcept
{
    const bool_t bValidOutputs = pSin != nullptr && pCos != nullptr && pSin != pCos;
    CY_ASSERT_MSG( bValidOutputs, "Scalar_SinCos requires distinct output storage." );
    if ( !bValidOutputs ) {
        return;
    }
    *pSin = std::sin( radians );
    *pCos = std::cos( radians );
}

f32 Scalar_AsinClamped( f32 value ) noexcept
{
    return std::asin( Scalar_Clamp( value, -1.0f, 1.0f ) );
}

f32 Scalar_AcosClamped( f32 value ) noexcept
{
    return std::acos( Scalar_Clamp( value, -1.0f, 1.0f ) );
}

f32 Scalar_Atan2( f32 y, f32 x ) noexcept { return std::atan2( y, x ); }
f32 Scalar_Floor( f32 value ) noexcept { return std::floor( value ); }
f32 Scalar_Ceil( f32 value ) noexcept { return std::ceil( value ); }
f32 Scalar_Round( f32 value ) noexcept { return std::round( value ); }
f32 Scalar_Truncate( f32 value ) noexcept { return std::trunc( value ); }

f32 Scalar_Fmod( f32 value, f32 divisor ) noexcept
{
    const bool_t bValidDivisor = std::isfinite( divisor ) && divisor != 0.0f;
    CY_ASSERT_MSG( bValidDivisor, "Scalar_Fmod requires a finite nonzero divisor." );
    return bValidDivisor ? std::fmod( value, divisor ) : 0.0f;
}

f32 Scalar_Repeat( f32 value, f32 length ) noexcept
{
    const bool_t bValidLength = std::isfinite( length ) && length > 0.0f;
    CY_ASSERT_MSG( bValidLength, "Scalar_Repeat requires a finite positive length." );
    if ( !bValidLength || !std::isfinite( value ) ) {
        return 0.0f;
    }

    // Floor, rather than fmod, keeps negative inputs in the canonical [0, length)
    // interval used for angles and periodic editor values.
    const f32 repeated = value - std::floor( value / length ) * length;
    return repeated < length ? repeated : 0.0f;
}

f32 Scalar_WrapRadiansPositive( f32 radians ) noexcept
{
    return Scalar_Repeat( radians, CY_TAU_F );
}

f32 Scalar_WrapRadiansSigned( f32 radians ) noexcept
{
    const f32 wrapped = Scalar_WrapRadiansPositive( radians + CY_PI_F );
    return wrapped - CY_PI_F;
}

f32 Scalar_InverseLerp( f32 a, f32 b, f32 value ) noexcept
{
    const f32 range = b - a;
    CY_ASSERT_MSG( range != 0.0f, "Scalar_InverseLerp requires distinct endpoints." );
    return range != 0.0f ? ( value - a ) / range : 0.0f;
}

f32 Scalar_Remap(
    f32 value,
    f32 sourceMinimum,
    f32 sourceMaximum,
    f32 destinationMinimum,
    f32 destinationMaximum ) noexcept
{
    return Scalar_Lerp(
        destinationMinimum,
        destinationMaximum,
        Scalar_InverseLerp( sourceMinimum, sourceMaximum, value ) );
}

f32 Scalar_SmoothStep( f32 edge0, f32 edge1, f32 value ) noexcept
{
    const f32 t = Scalar_Saturate( Scalar_InverseLerp( edge0, edge1, value ) );
    // Cubic Hermite polynomial with zero first derivative at both endpoints.
    return t * t * ( 3.0f - 2.0f * t );
}

f32 Scalar_SmootherStep( f32 edge0, f32 edge1, f32 value ) noexcept
{
    const f32 t = Scalar_Saturate( Scalar_InverseLerp( edge0, edge1, value ) );
    // Quintic form also drives the second derivative to zero at both endpoints.
    return t * t * t * ( t * ( t * 6.0f - 15.0f ) + 10.0f );
}

f32 Scalar_MoveTowards( f32 current, f32 target, f32 maximumDelta ) noexcept
{
    const bool_t bValidDelta = std::isfinite( maximumDelta ) && maximumDelta >= 0.0f;
    CY_ASSERT_MSG(
        bValidDelta,
        "Scalar_MoveTowards requires a finite nonnegative maximum delta." );
    if ( !bValidDelta ) {
        return current;
    }

    const f32 difference = target - current;
    if ( std::fabs( difference ) <= maximumDelta ) {
        return target;
    }
    return current + Scalar_Sign( difference ) * maximumDelta;
}

} // namespace cypher::math
