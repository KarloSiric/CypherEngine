//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector2.cpp
//  Purpose: Implements checked and non-trivial Vector2 operations.
//  Details: Checked normalization scales inputs before squaring so finite extreme
//           values do not fail only because an intermediate overflows.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Vector2.h"
#include "CypherCommon_Assert.h"

#include <algorithm>

namespace cypher::math
{

namespace
{

CYPHER_NODISCARD bool_t Vec2_ValidTolerance( f32 tolerance ) noexcept
{
    return Scalar_IsFinite( tolerance ) && tolerance >= 0.0f;
}

CYPHER_NODISCARD bool_t Vec2_NormalizeFinite(
    vec2_t value,
    vec2_t *pNormalized,
    f32 *pLength ) noexcept
{
    // Normalize a scaled copy so squaring extreme finite components cannot
    // overflow or underflow before the vector length is known.
    const f32 maximumComponent = Scalar_Max(
        Scalar_Abs( value.x ), Scalar_Abs( value.y ) );
    if ( maximumComponent == 0.0f ) {
        *pNormalized = CY_VEC2_ZERO;
        *pLength = 0.0f;
        return false;
    }

    const vec2_t scaled = Vec2_DivideScalar( value, maximumComponent );
    const f32 scaledLength = Scalar_Sqrt( Vec2_LengthSquared( scaled ) );
    *pNormalized = Vec2_DivideScalar( scaled, scaledLength );
    *pLength = maximumComponent * scaledLength;
    return Vec2_IsFinite( *pNormalized );
}

} // namespace

vec2_t Vec2_FromArray( const f32 *pValues ) noexcept
{
    CY_ASSERT_MSG( pValues != nullptr, "Vec2_FromArray requires source storage." );
    return pValues != nullptr ? Vec2_Make( pValues[0], pValues[1] ) : CY_VEC2_ZERO;
}

void Vec2_Store( vec2_t value, f32 *pValues ) noexcept
{
    CY_ASSERT_MSG( pValues != nullptr, "Vec2_Store requires destination storage." );
    if ( pValues != nullptr ) {
        pValues[0] = value.x;
        pValues[1] = value.y;
    }
}

f32 Vec2_Component( vec2_t value, u32 iComponent ) noexcept
{
    CY_ASSERT_MSG( iComponent < 2u, "Vec2_Component index is outside the vector." );
    switch ( iComponent ) {
        case 0u: return value.x;
        case 1u: return value.y;
        default: return 0.0f;
    }
}

void Vec2_SetComponent( vec2_t *pValue, u32 iComponent, f32 value ) noexcept
{
    CY_ASSERT_MSG( pValue != nullptr, "Vec2_SetComponent requires vector storage." );
    CY_ASSERT_MSG( iComponent < 2u, "Vec2_SetComponent index is outside the vector." );
    if ( pValue == nullptr ) {
        return;
    }
    if ( iComponent == 0u ) {
        pValue->x = value;
    } else if ( iComponent == 1u ) {
        pValue->y = value;
    }
}

bool_t Vec2_IsFinite( vec2_t value ) noexcept
{
    return Scalar_IsFinite( value.x ) && Scalar_IsFinite( value.y );
}

bool_t Vec2_NearlyEquals(
    vec2_t a,
    vec2_t b,
    f32 absoluteTolerance,
    f32 relativeTolerance ) noexcept
{
    return Scalar_NearlyEquals( a.x, b.x, absoluteTolerance, relativeTolerance ) &&
           Scalar_NearlyEquals( a.y, b.y, absoluteTolerance, relativeTolerance );
}

bool_t Vec2_IsNearZero( vec2_t value, f32 tolerance ) noexcept
{
    const bool_t bValidTolerance = Vec2_ValidTolerance( tolerance );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Vec2_IsNearZero requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Vec2_IsFinite( value ) ) {
        return false;
    }

    vec2_t normalized{};
    f32 length = 0.0f;
    return !Vec2_NormalizeFinite( value, &normalized, &length ) || length <= tolerance;
}

bool_t Vec2_IsUnitLength( vec2_t value, f32 tolerance ) noexcept
{
    const bool_t bValidTolerance = Vec2_ValidTolerance( tolerance );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Vec2_IsUnitLength requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Vec2_IsFinite( value ) ) {
        return false;
    }

    vec2_t normalized{};
    f32 length = 0.0f;
    return Vec2_NormalizeFinite( value, &normalized, &length ) &&
           Scalar_Abs( length - 1.0f ) <= tolerance;
}

vec2_t Vec2_Abs( vec2_t value ) noexcept
{
    return Vec2_Make( Scalar_Abs( value.x ), Scalar_Abs( value.y ) );
}

vec2_t Vec2_Min( vec2_t a, vec2_t b ) noexcept
{
    return Vec2_Make( Scalar_Min( a.x, b.x ), Scalar_Min( a.y, b.y ) );
}

vec2_t Vec2_Max( vec2_t a, vec2_t b ) noexcept
{
    return Vec2_Make( Scalar_Max( a.x, b.x ), Scalar_Max( a.y, b.y ) );
}

vec2_t Vec2_Clamp( vec2_t value, vec2_t minimum, vec2_t maximum ) noexcept
{
    const bool_t bValidBounds = minimum.x <= maximum.x && minimum.y <= maximum.y;
    CY_ASSERT_MSG( bValidBounds, "Vec2_Clamp requires ordered component bounds." );
    if ( !bValidBounds ) {
        return value;
    }
    return Vec2_Make(
        Scalar_Clamp( value.x, minimum.x, maximum.x ),
        Scalar_Clamp( value.y, minimum.y, maximum.y ) );
}

vec2_t Vec2_Floor( vec2_t value ) noexcept
{
    return Vec2_Make( Scalar_Floor( value.x ), Scalar_Floor( value.y ) );
}

vec2_t Vec2_Ceil( vec2_t value ) noexcept
{
    return Vec2_Make( Scalar_Ceil( value.x ), Scalar_Ceil( value.y ) );
}

vec2_t Vec2_Round( vec2_t value ) noexcept
{
    return Vec2_Make( Scalar_Round( value.x ), Scalar_Round( value.y ) );
}

vec2_t Vec2_Truncate( vec2_t value ) noexcept
{
    return Vec2_Make( Scalar_Truncate( value.x ), Scalar_Truncate( value.y ) );
}

f32 Vec2_Length( vec2_t value ) noexcept
{
    return Scalar_Sqrt( Vec2_LengthSquared( value ) );
}

f32 Vec2_Distance( vec2_t a, vec2_t b ) noexcept
{
    return Vec2_Length( Vec2_Subtract( a, b ) );
}

vec2_t Vec2_NormalizeUnchecked( vec2_t value ) noexcept
{
    const f32 lengthSquared = Vec2_LengthSquared( value );
    CY_ASSERT_MSG(
        lengthSquared > 0.0f && Scalar_IsFinite( lengthSquared ),
        "Vec2_NormalizeUnchecked requires a finite nonzero vector." );
    return Vec2_Scale( value, Scalar_InvSqrt( lengthSquared ) );
}

bool_t Vec2_TryNormalize(
    vec2_t value,
    f32 minimumLength,
    vec2_t *pNormalized,
    f32 *pOriginalLength ) noexcept
{
    const bool_t bValidOutput = pNormalized != nullptr;
    const bool_t bValidMinimum = Vec2_ValidTolerance( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec2_TryNormalize requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec2_TryNormalize requires a finite nonnegative minimum length." );
    if ( pOriginalLength != nullptr ) {
        *pOriginalLength = 0.0f;
    }
    if ( !bValidOutput ) {
        return false;
    }
    *pNormalized = CY_VEC2_ZERO;
    if ( !bValidMinimum || !Vec2_IsFinite( value ) ) {
        return false;
    }

    vec2_t normalized{};
    f32 length = 0.0f;
    if ( !Vec2_NormalizeFinite( value, &normalized, &length ) ) {
        return false;
    }
    if ( pOriginalLength != nullptr ) {
        *pOriginalLength = length;
    }
    if ( length <= minimumLength ) {
        return false;
    }
    *pNormalized = normalized;
    return true;
}

vec2_t Vec2_LerpClamped( vec2_t a, vec2_t b, f32 t ) noexcept
{
    return Vec2_Lerp( a, b, Scalar_Saturate( t ) );
}

vec2_t Vec2_MoveTowards( vec2_t current, vec2_t target, f32 maximumDistance ) noexcept
{
    const bool_t bValidDistance = Vec2_ValidTolerance( maximumDistance );
    CY_ASSERT_MSG(
        bValidDistance,
        "Vec2_MoveTowards requires a finite nonnegative maximum distance." );
    if ( !bValidDistance ) {
        return current;
    }

    const vec2_t displacement = Vec2_Subtract( target, current );
    if ( Vec2_EqualsExact( displacement, CY_VEC2_ZERO ) ) {
        return target;
    }
    if ( !Vec2_IsFinite( displacement ) ) {
        return current;
    }

    vec2_t direction{};
    f32 distance = 0.0f;
    if ( !Vec2_NormalizeFinite( displacement, &direction, &distance ) ) {
        return current;
    }
    // Clamp to the target instead of stepping past it.
    return distance <= maximumDistance
        ? target
        : Vec2_MulAdd( current, direction, maximumDistance );
}

vec2_t Vec2_ClampLength( vec2_t value, f32 minimumLength, f32 maximumLength ) noexcept
{
    const bool_t bValidBounds = Vec2_ValidTolerance( minimumLength ) &&
                                Vec2_ValidTolerance( maximumLength ) &&
                                minimumLength <= maximumLength;
    CY_ASSERT_MSG(
        bValidBounds,
        "Vec2_ClampLength requires finite nonnegative ordered bounds." );
    if ( !bValidBounds || !Vec2_IsFinite( value ) ) {
        return value;
    }

    vec2_t normalized{};
    f32 length = 0.0f;
    if ( !Vec2_NormalizeFinite( value, &normalized, &length ) ) {
        return value;
    }
    if ( length < minimumLength ) {
        return Vec2_Scale( normalized, minimumLength );
    }
    if ( length > maximumLength ) {
        return Vec2_Scale( normalized, maximumLength );
    }
    return value;
}

bool_t Vec2_TryProjectOnto(
    vec2_t value,
    vec2_t onto,
    f32 minimumLength,
    vec2_t *pProjected ) noexcept
{
    const bool_t bValidOutput = pProjected != nullptr;
    const bool_t bValidMinimum = Vec2_ValidTolerance( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec2_TryProjectOnto requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec2_TryProjectOnto requires a finite nonnegative minimum length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pProjected = CY_VEC2_ZERO;
    if ( !bValidMinimum || !Vec2_IsFinite( value ) ) {
        return false;
    }

    vec2_t unitDirection{};
    if ( !Vec2_TryNormalize( onto, minimumLength, &unitDirection, nullptr ) ) {
        return false;
    }
    const vec2_t projected = Vec2_ProjectOntoUnit( value, unitDirection );
    if ( !Vec2_IsFinite( projected ) ) {
        return false;
    }
    *pProjected = projected;
    return true;
}

bool_t Vec2_TryAngleBetween(
    vec2_t a,
    vec2_t b,
    f32 minimumLength,
    f32 *pAngleRadians ) noexcept
{
    const bool_t bValidOutput = pAngleRadians != nullptr;
    const bool_t bValidMinimum = Vec2_ValidTolerance( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec2_TryAngleBetween requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec2_TryAngleBetween requires a finite nonnegative minimum length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pAngleRadians = 0.0f;
    if ( !bValidMinimum ) {
        return false;
    }

    vec2_t normalizedA{};
    vec2_t normalizedB{};
    if ( !Vec2_TryNormalize( a, minimumLength, &normalizedA, nullptr ) ||
         !Vec2_TryNormalize( b, minimumLength, &normalizedB, nullptr ) ) {
        return false;
    }
    // Floating-point dot products can drift just outside [-1, 1]; the clamped
    // inverse cosine keeps parallel vectors from producing NaN.
    *pAngleRadians = Scalar_AcosClamped( Vec2_Dot( normalizedA, normalizedB ) );
    return true;
}

} // namespace cypher::math
