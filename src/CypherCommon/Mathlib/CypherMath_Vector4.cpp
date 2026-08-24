//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector4.cpp
//  Purpose: Implements checked and non-trivial Vector4 operations.
//  Details: Homogeneous-coordinate conversion explicitly checks w so projection
//           failures cannot silently create infinities in renderer or editor code.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Vector4.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

namespace
{

CYPHER_NODISCARD bool_t Vec4_ValidTolerance( f32 tolerance ) noexcept
{
    return Scalar_IsFinite( tolerance ) && tolerance >= 0.0f;
}

CYPHER_NODISCARD bool_t Vec4_NormalizeFinite(
    vec4_t value,
    vec4_t *pNormalized,
    f32 *pLength ) noexcept
{
    // Scale by the largest component before squaring. This avoids overflow for
    // large finite vectors and underflow for very small finite vectors.
    const f32 maximumComponent = Scalar_Max(
        Scalar_Max( Scalar_Abs( value.x ), Scalar_Abs( value.y ) ),
        Scalar_Max( Scalar_Abs( value.z ), Scalar_Abs( value.w ) ) );
    if ( maximumComponent == 0.0f ) {
        *pNormalized = CY_VEC4_ZERO;
        *pLength = 0.0f;
        return false;
    }
    const vec4_t scaled = Vec4_DivideScalar( value, maximumComponent );
    const f32 scaledLength = Scalar_Sqrt( Vec4_LengthSquared( scaled ) );
    *pNormalized = Vec4_DivideScalar( scaled, scaledLength );
    *pLength = maximumComponent * scaledLength;
    return Vec4_IsFinite( *pNormalized );
}

} // namespace

vec4_t Vec4_FromArray( const f32 *pValues ) noexcept
{
    CY_ASSERT_MSG( pValues != nullptr, "Vec4_FromArray requires source storage." );
    return pValues != nullptr
        ? Vec4_Make( pValues[0], pValues[1], pValues[2], pValues[3] )
        : CY_VEC4_ZERO;
}

void Vec4_Store( vec4_t value, f32 *pValues ) noexcept
{
    CY_ASSERT_MSG( pValues != nullptr, "Vec4_Store requires destination storage." );
    if ( pValues == nullptr ) {
        return;
    }
    pValues[0] = value.x;
    pValues[1] = value.y;
    pValues[2] = value.z;
    pValues[3] = value.w;
}

f32 Vec4_Component( vec4_t value, u32 iComponent ) noexcept
{
    CY_ASSERT_MSG( iComponent < 4u, "Vec4_Component index is outside the vector." );
    switch ( iComponent ) {
        case 0u: return value.x;
        case 1u: return value.y;
        case 2u: return value.z;
        case 3u: return value.w;
        default: return 0.0f;
    }
}

void Vec4_SetComponent( vec4_t *pValue, u32 iComponent, f32 value ) noexcept
{
    CY_ASSERT_MSG( pValue != nullptr, "Vec4_SetComponent requires vector storage." );
    CY_ASSERT_MSG( iComponent < 4u, "Vec4_SetComponent index is outside the vector." );
    if ( pValue == nullptr ) {
        return;
    }
    switch ( iComponent ) {
        case 0u: pValue->x = value; break;
        case 1u: pValue->y = value; break;
        case 2u: pValue->z = value; break;
        case 3u: pValue->w = value; break;
        default: break;
    }
}

bool_t Vec4_IsFinite( vec4_t value ) noexcept
{
    return Scalar_IsFinite( value.x ) && Scalar_IsFinite( value.y ) &&
           Scalar_IsFinite( value.z ) && Scalar_IsFinite( value.w );
}

bool_t Vec4_NearlyEquals(
    vec4_t a,
    vec4_t b,
    f32 absoluteTolerance,
    f32 relativeTolerance ) noexcept
{
    return Scalar_NearlyEquals( a.x, b.x, absoluteTolerance, relativeTolerance ) &&
           Scalar_NearlyEquals( a.y, b.y, absoluteTolerance, relativeTolerance ) &&
           Scalar_NearlyEquals( a.z, b.z, absoluteTolerance, relativeTolerance ) &&
           Scalar_NearlyEquals( a.w, b.w, absoluteTolerance, relativeTolerance );
}

vec4_t Vec4_Abs( vec4_t value ) noexcept
{
    return Vec4_Make(
        Scalar_Abs( value.x ), Scalar_Abs( value.y ),
        Scalar_Abs( value.z ), Scalar_Abs( value.w ) );
}

vec4_t Vec4_Min( vec4_t a, vec4_t b ) noexcept
{
    return Vec4_Make(
        Scalar_Min( a.x, b.x ), Scalar_Min( a.y, b.y ),
        Scalar_Min( a.z, b.z ), Scalar_Min( a.w, b.w ) );
}

vec4_t Vec4_Max( vec4_t a, vec4_t b ) noexcept
{
    return Vec4_Make(
        Scalar_Max( a.x, b.x ), Scalar_Max( a.y, b.y ),
        Scalar_Max( a.z, b.z ), Scalar_Max( a.w, b.w ) );
}

vec4_t Vec4_Clamp( vec4_t value, vec4_t minimum, vec4_t maximum ) noexcept
{
    const bool_t bValidBounds = minimum.x <= maximum.x &&
                                minimum.y <= maximum.y &&
                                minimum.z <= maximum.z &&
                                minimum.w <= maximum.w;
    CY_ASSERT_MSG( bValidBounds, "Vec4_Clamp requires ordered component bounds." );
    if ( !bValidBounds ) {
        return value;
    }
    return Vec4_Make(
        Scalar_Clamp( value.x, minimum.x, maximum.x ),
        Scalar_Clamp( value.y, minimum.y, maximum.y ),
        Scalar_Clamp( value.z, minimum.z, maximum.z ),
        Scalar_Clamp( value.w, minimum.w, maximum.w ) );
}

f32 Vec4_Length( vec4_t value ) noexcept
{
    return Scalar_Sqrt( Vec4_LengthSquared( value ) );
}

f32 Vec4_Distance( vec4_t a, vec4_t b ) noexcept
{
    return Vec4_Length( Vec4_Subtract( a, b ) );
}

vec4_t Vec4_NormalizeUnchecked( vec4_t value ) noexcept
{
    const f32 lengthSquared = Vec4_LengthSquared( value );
    CY_ASSERT_MSG(
        lengthSquared > 0.0f && Scalar_IsFinite( lengthSquared ),
        "Vec4_NormalizeUnchecked requires a finite nonzero vector." );
    return Vec4_Scale( value, Scalar_InvSqrt( lengthSquared ) );
}

bool_t Vec4_TryNormalize(
    vec4_t value,
    f32 minimumLength,
    vec4_t *pNormalized,
    f32 *pOriginalLength ) noexcept
{
    const bool_t bValidOutput = pNormalized != nullptr;
    const bool_t bValidMinimum = Vec4_ValidTolerance( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec4_TryNormalize requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec4_TryNormalize requires a finite nonnegative minimum length." );
    if ( pOriginalLength != nullptr ) {
        *pOriginalLength = 0.0f;
    }
    if ( !bValidOutput ) {
        return false;
    }
    *pNormalized = CY_VEC4_ZERO;
    if ( !bValidMinimum || !Vec4_IsFinite( value ) ) {
        return false;
    }

    vec4_t normalized{};
    f32 length = 0.0f;
    if ( !Vec4_NormalizeFinite( value, &normalized, &length ) ) {
        return false;
    }
    if ( pOriginalLength != nullptr ) {
        *pOriginalLength = length;
    }

    // Report the measured length even when the caller's degeneracy threshold
    // rejects the vector; the normalized output remains the documented zero.
    if ( length <= minimumLength ) {
        return false;
    }
    *pNormalized = normalized;
    return true;
}

vec4_t Vec4_LerpClamped( vec4_t a, vec4_t b, f32 t ) noexcept
{
    return Vec4_Lerp( a, b, Scalar_Saturate( t ) );
}

bool_t Vec4_TryPerspectiveDivide(
    vec4_t value,
    f32 minimumAbsW,
    vec3_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    const bool_t bValidMinimum = Vec4_ValidTolerance( minimumAbsW );
    CY_ASSERT_MSG( bValidOutput, "Vec4_TryPerspectiveDivide requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec4_TryPerspectiveDivide requires a finite nonnegative w threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_VEC3_ZERO;
    if ( !bValidMinimum || !Vec4_IsFinite( value ) ||
         Scalar_Abs( value.w ) <= minimumAbsW ) {
        return false;
    }

    // Homogeneous projection is undefined at w == 0 and numerically unstable
    // near it, so the caller supplies the minimum accepted magnitude.
    const f32 inverseW = 1.0f / value.w;
    const vec3_t result = Vec3_Make(
        value.x * inverseW,
        value.y * inverseW,
        value.z * inverseW );
    if ( !Vec3_IsFinite( result ) ) {
        return false;
    }
    *pResult = result;
    return true;
}

} // namespace cypher::math
