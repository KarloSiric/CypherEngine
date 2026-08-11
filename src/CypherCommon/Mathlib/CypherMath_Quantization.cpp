//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Quantization.cpp
//  Purpose: Implements bounded scalar, vector, angle, and rotation quantization.
//  Details: Double intermediates make endpoint mapping stable, while quaternion
//           compression canonicalizes sign before dropping its largest component.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Quantization.h"

#include "CypherCommon_Assert.h"

#include <algorithm>
#include <cmath>

namespace cypher::math
{

namespace
{

inline constexpr f64 CY_QUAT_SMALLEST_THREE_LIMIT =
    0.707106781186547524400844362104849039;

bool_t BitCountValid( u32 cBits ) noexcept
{
    return cBits >= 1u && cBits <= 32u;
}

f32 QuatComponent( quat_t value, u32 iComponent ) noexcept
{
    switch ( iComponent ) {
        case 0u: return value.x;
        case 1u: return value.y;
        case 2u: return value.z;
        default: return value.w;
    }
}

void SetQuatComponent( quat_t *pValue, u32 iComponent, f32 component ) noexcept
{
    switch ( iComponent ) {
        case 0u: pValue->x = component; break;
        case 1u: pValue->y = component; break;
        case 2u: pValue->z = component; break;
        default: pValue->w = component; break;
    }
}

} // namespace

u32 Quantization_MaxCode( u32 cBits ) noexcept
{
    if ( !BitCountValid( cBits ) ) {
        return 0u;
    }
    return cBits == 32u ? common::CY_U32_MAX : ( 1u << cBits ) - 1u;
}

bool_t Quantization_TryEncodeUnorm( f32 value, u32 cBits, u32 *pCode ) noexcept
{
    const bool_t bValidOutput = pCode != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Quantization_TryEncodeUnorm requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pCode = 0u;
    if ( !BitCountValid( cBits ) || !Scalar_IsFinite( value ) ) {
        return false;
    }
    const f64 maximumCode = Quantization_MaxCode( cBits );
    const f64 normalized = std::clamp( static_cast<f64>( value ), 0.0, 1.0 );
    *pCode = static_cast<u32>( std::floor( normalized * maximumCode + 0.5 ) );
    return true;
}

bool_t Quantization_TryDecodeUnorm( u32 code, u32 cBits, f32 *pValue ) noexcept
{
    const bool_t bValidOutput = pValue != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Quantization_TryDecodeUnorm requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pValue = 0.0f;
    const u32 maximumCode = Quantization_MaxCode( cBits );
    if ( maximumCode == 0u || code > maximumCode ) {
        return false;
    }
    *pValue = static_cast<f32>(
        static_cast<f64>( code ) / static_cast<f64>( maximumCode ) );
    return true;
}

bool_t Quantization_TryEncodeSnorm( f32 value, u32 cBits, u32 *pCode ) noexcept
{
    if ( !Scalar_IsFinite( value ) ) {
        if ( pCode != nullptr ) {
            *pCode = 0u;
        }
        return false;
    }
    return Quantization_TryEncodeUnorm( value * 0.5f + 0.5f, cBits, pCode );
}

bool_t Quantization_TryDecodeSnorm( u32 code, u32 cBits, f32 *pValue ) noexcept
{
    f32 normalized = 0.0f;
    if ( pValue == nullptr ||
         !Quantization_TryDecodeUnorm( code, cBits, &normalized ) ) {
        if ( pValue != nullptr ) {
            *pValue = 0.0f;
        }
        return false;
    }
    *pValue = normalized * 2.0f - 1.0f;
    return true;
}

bool_t Quantization_TryEncodeRange(
    f32 value,
    f32 minimum,
    f32 maximum,
    u32 cBits,
    u32 *pCode ) noexcept
{
    if ( pCode == nullptr || !Scalar_IsFinite( value ) ||
         !Scalar_IsFinite( minimum ) || !Scalar_IsFinite( maximum ) ||
         maximum <= minimum ) {
        if ( pCode != nullptr ) {
            *pCode = 0u;
        }
        return false;
    }
    const f64 normalized =
        ( static_cast<f64>( value ) - minimum ) /
        ( static_cast<f64>( maximum ) - minimum );
    return Quantization_TryEncodeUnorm(
        static_cast<f32>( normalized ), cBits, pCode );
}

bool_t Quantization_TryDecodeRange(
    u32 code,
    f32 minimum,
    f32 maximum,
    u32 cBits,
    f32 *pValue ) noexcept
{
    if ( pValue == nullptr || !Scalar_IsFinite( minimum ) ||
         !Scalar_IsFinite( maximum ) || maximum <= minimum ) {
        if ( pValue != nullptr ) {
            *pValue = 0.0f;
        }
        return false;
    }
    f32 normalized = 0.0f;
    if ( !Quantization_TryDecodeUnorm( code, cBits, &normalized ) ) {
        *pValue = 0.0f;
        return false;
    }
    *pValue = static_cast<f32>(
        static_cast<f64>( minimum ) +
        ( static_cast<f64>( maximum ) - minimum ) * normalized );
    return Scalar_IsFinite( *pValue );
}

bool_t Quantization_TryEncodeVec3Range(
    vec3_t value,
    vec3_t minimum,
    vec3_t maximum,
    u32 cBits,
    quantized_vec3_t *pCode ) noexcept
{
    if ( pCode == nullptr ) {
        return false;
    }
    *pCode = {};
    if ( !Vec3_IsFinite( value ) || !Vec3_IsFinite( minimum ) ||
         !Vec3_IsFinite( maximum ) ) {
        return false;
    }

    quantized_vec3_t result{};
    if ( !Quantization_TryEncodeRange(
             value.x, minimum.x, maximum.x, cBits, &result.x ) ||
         !Quantization_TryEncodeRange(
             value.y, minimum.y, maximum.y, cBits, &result.y ) ||
         !Quantization_TryEncodeRange(
             value.z, minimum.z, maximum.z, cBits, &result.z ) ) {
        return false;
    }
    *pCode = result;
    return true;
}

bool_t Quantization_TryDecodeVec3Range(
    quantized_vec3_t code,
    vec3_t minimum,
    vec3_t maximum,
    u32 cBits,
    vec3_t *pValue ) noexcept
{
    if ( pValue == nullptr ) {
        return false;
    }
    *pValue = CY_VEC3_ZERO;
    if ( !Vec3_IsFinite( minimum ) || !Vec3_IsFinite( maximum ) ) {
        return false;
    }

    vec3_t result{};
    if ( !Quantization_TryDecodeRange(
             code.x, minimum.x, maximum.x, cBits, &result.x ) ||
         !Quantization_TryDecodeRange(
             code.y, minimum.y, maximum.y, cBits, &result.y ) ||
         !Quantization_TryDecodeRange(
             code.z, minimum.z, maximum.z, cBits, &result.z ) ) {
        return false;
    }
    *pValue = result;
    return true;
}

bool_t Quantization_TryEncodeAngle(
    angle_t angle,
    u32 cBits,
    u32 *pCode ) noexcept
{
    if ( pCode == nullptr || !BitCountValid( cBits ) ||
         !Scalar_IsFinite( angle.radians ) ) {
        if ( pCode != nullptr ) {
            *pCode = 0u;
        }
        return false;
    }
    const f64 codeCount = std::ldexp( 1.0, static_cast<int>( cBits ) );
    const f64 normalized = static_cast<f64>(
        Scalar_WrapRadiansPositive( angle.radians ) ) / CY_TAU_D;
    const f64 rounded = std::floor( normalized * codeCount + 0.5 );
    *pCode = rounded >= codeCount
        ? 0u
        : static_cast<u32>( static_cast<common::u64>( rounded ) );
    return true;
}

bool_t Quantization_TryDecodeAngle(
    u32 code,
    u32 cBits,
    angle_t *pAngle ) noexcept
{
    if ( pAngle == nullptr ) {
        return false;
    }
    *pAngle = Angle_FromRadians( 0.0f );
    const u32 maximumCode = Quantization_MaxCode( cBits );
    if ( maximumCode == 0u || code > maximumCode ) {
        return false;
    }
    const f64 codeCount = std::ldexp( 1.0, static_cast<int>( cBits ) );
    *pAngle = Angle_FromRadians( static_cast<f32>(
        static_cast<f64>( code ) / codeCount * CY_TAU_D ) );
    return true;
}

bool_t Quantization_TryEncodeQuatSmallestThree(
    quat_t rotation,
    u32 cBitsPerComponent,
    f32 minimumLength,
    quantized_quat_t *pCode ) noexcept
{
    if ( pCode == nullptr ) {
        return false;
    }
    *pCode = {};
    if ( !BitCountValid( cBitsPerComponent ) || minimumLength < 0.0f ||
         !Scalar_IsFinite( minimumLength ) ) {
        return false;
    }
    quat_t unitRotation{};
    if ( !Quat_TryNormalize(
             rotation, minimumLength, &unitRotation, nullptr ) ) {
        return false;
    }

    u32 largest = 0u;
    f32 largestAbs = std::abs( unitRotation.x );
    for ( u32 i = 1u; i < 4u; ++i ) {
        const f32 candidate = std::abs( QuatComponent( unitRotation, i ) );
        if ( candidate > largestAbs ) {
            largest = i;
            largestAbs = candidate;
        }
    }
    if ( QuatComponent( unitRotation, largest ) < 0.0f ) {
        unitRotation = Quat_Negate( unitRotation );
    }

    pCode->largestComponent = largest;
    u32 iStored = 0u;
    for ( u32 i = 0u; i < 4u; ++i ) {
        if ( i == largest ) {
            continue;
        }
        const f64 component = std::clamp(
            static_cast<f64>( QuatComponent( unitRotation, i ) ),
            -CY_QUAT_SMALLEST_THREE_LIMIT,
            CY_QUAT_SMALLEST_THREE_LIMIT );
        const f32 normalized = static_cast<f32>(
            ( component + CY_QUAT_SMALLEST_THREE_LIMIT ) /
            ( 2.0 * CY_QUAT_SMALLEST_THREE_LIMIT ) );
        if ( !Quantization_TryEncodeUnorm(
                 normalized, cBitsPerComponent,
                 &pCode->components[iStored++] ) ) {
            *pCode = {};
            return false;
        }
    }
    return true;
}

bool_t Quantization_TryDecodeQuatSmallestThree(
    quantized_quat_t code,
    u32 cBitsPerComponent,
    f32 minimumLength,
    quat_t *pRotation ) noexcept
{
    if ( pRotation == nullptr ) {
        return false;
    }
    *pRotation = CY_QUAT_IDENTITY;
    if ( code.largestComponent >= 4u ||
         !BitCountValid( cBitsPerComponent ) || minimumLength < 0.0f ||
         !Scalar_IsFinite( minimumLength ) ) {
        return false;
    }

    quat_t decoded{};
    f64 sumSquared = 0.0;
    u32 iStored = 0u;
    for ( u32 i = 0u; i < 4u; ++i ) {
        if ( i == code.largestComponent ) {
            continue;
        }
        f32 normalized = 0.0f;
        if ( !Quantization_TryDecodeUnorm(
                 code.components[iStored++], cBitsPerComponent, &normalized ) ) {
            return false;
        }
        const f32 component = static_cast<f32>(
            static_cast<f64>( normalized ) *
                ( 2.0 * CY_QUAT_SMALLEST_THREE_LIMIT ) -
            CY_QUAT_SMALLEST_THREE_LIMIT );
        SetQuatComponent( &decoded, i, component );
        sumSquared += static_cast<f64>( component ) * component;
    }
    SetQuatComponent(
        &decoded,
        code.largestComponent,
        static_cast<f32>( std::sqrt( std::max( 0.0, 1.0 - sumSquared ) ) ) );
    return Quat_TryNormalize(
        decoded, minimumLength, pRotation, nullptr );
}

} // namespace cypher::math
