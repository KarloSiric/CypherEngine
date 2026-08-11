//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector3.cpp
//  Purpose: Implements checked and non-trivial three-dimensional vector operations.
//  Details: The implementation provides scalar Vector3 math for the runtime and
//           tools while keeping invalid-input behavior deterministic in all builds.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Vector3.h"
#include "CypherCommon_Assert.h"

#include <algorithm>
#include <cmath>

namespace cypher::math
{

namespace
{

CYPHER_NODISCARD bool_t Vec3_IsValidTolerance( f32 tolerance ) noexcept
{
    return std::isfinite( tolerance ) && tolerance >= 0.0f;
}

CYPHER_NODISCARD bool_t Vec3_ScalarNearlyEquals(
    f32 a,
    f32 b,
    f32 absoluteTolerance,
    f32 relativeTolerance ) noexcept
{
    if ( a == b ) {
        return true;
    }

    if ( !std::isfinite( a ) || !std::isfinite( b ) ) {
        return false;
    }

    const f32 difference = std::fabs( a - b );
    if ( !std::isfinite( difference ) ) {
        return false;
    }

    const f32 scale = std::max( std::fabs( a ), std::fabs( b ) );
    const f32 tolerance = std::max(
        absoluteTolerance,
        relativeTolerance * scale );
    return difference <= tolerance;
}

CYPHER_NODISCARD bool_t Vec3_NormalizeFinite(
    vec3_t value,
    vec3_t *pNormalized,
    f32 *pLength ) noexcept
{
    const f32 maximumComponent = std::max(
        std::fabs( value.x ),
        std::max( std::fabs( value.y ), std::fabs( value.z ) ) );
    if ( maximumComponent == 0.0f ) {
        *pNormalized = CY_VEC3_ZERO;
        *pLength = 0.0f;
        return false;
    }

    // Scaling first prevents the squared length from overflowing or underflowing.
    const vec3_t scaled = Vec3_DivideScalar( value, maximumComponent );
    const f32 scaledLength = std::sqrt( Vec3_LengthSquared( scaled ) );
    *pNormalized = Vec3_DivideScalar( scaled, scaledLength );
    *pLength = maximumComponent * scaledLength;
    return Vec3_IsFinite( *pNormalized );
}

CYPHER_NODISCARD vec3_t Vec3_LeastAlignedAxis( vec3_t value ) noexcept
{
    const f32 x = std::fabs( value.x );
    const f32 y = std::fabs( value.y );
    const f32 z = std::fabs( value.z );

    if ( x <= y && x <= z ) {
        return CY_VEC3_FORWARD;
    }
    if ( y <= z ) {
        return CY_VEC3_LEFT;
    }
    return CY_VEC3_UP;
}

} // namespace

vec3_t Vec3_FromArray( const f32 *pValues ) noexcept
{
    CY_ASSERT_MSG( pValues != nullptr, "Vec3_FromArray requires source storage." );
    if ( pValues == nullptr ) {
        return CY_VEC3_ZERO;
    }

    return Vec3_Make( pValues[0], pValues[1], pValues[2] );
}

void Vec3_Store( vec3_t value, f32 *pValues ) noexcept
{
    CY_ASSERT_MSG( pValues != nullptr, "Vec3_Store requires destination storage." );
    if ( pValues == nullptr ) {
        return;
    }

    pValues[0] = value.x;
    pValues[1] = value.y;
    pValues[2] = value.z;
}

f32 Vec3_Component( vec3_t value, u32 iComponent ) noexcept
{
    CY_ASSERT_MSG( iComponent < 3u, "Vec3_Component index is outside the vector." );

    switch ( iComponent ) {
        case 0u: return value.x;
        case 1u: return value.y;
        case 2u: return value.z;
        default: return 0.0f;
    }
}

void Vec3_SetComponent( vec3_t *pValue, u32 iComponent, f32 value ) noexcept
{
    CY_ASSERT_MSG( pValue != nullptr, "Vec3_SetComponent requires vector storage." );
    CY_ASSERT_MSG( iComponent < 3u, "Vec3_SetComponent index is outside the vector." );
    if ( pValue == nullptr ) {
        return;
    }

    switch ( iComponent ) {
        case 0u: pValue->x = value; break;
        case 1u: pValue->y = value; break;
        case 2u: pValue->z = value; break;
        default: break;
    }
}

bool_t Vec3_IsFinite( vec3_t value ) noexcept
{
    return std::isfinite( value.x ) &&
           std::isfinite( value.y ) &&
           std::isfinite( value.z );
}

bool_t Vec3_NearlyEquals(
    vec3_t a,
    vec3_t b,
    f32 absoluteTolerance,
    f32 relativeTolerance ) noexcept
{
    const bool_t bValidAbsoluteTolerance = Vec3_IsValidTolerance( absoluteTolerance );
    const bool_t bValidRelativeTolerance = Vec3_IsValidTolerance( relativeTolerance );
    CY_ASSERT_MSG(
        bValidAbsoluteTolerance,
        "Vec3_NearlyEquals requires a finite nonnegative absolute tolerance." );
    CY_ASSERT_MSG(
        bValidRelativeTolerance,
        "Vec3_NearlyEquals requires a finite nonnegative relative tolerance." );
    if ( !bValidAbsoluteTolerance || !bValidRelativeTolerance ) {
        return false;
    }

    return Vec3_ScalarNearlyEquals( a.x, b.x, absoluteTolerance, relativeTolerance ) &&
           Vec3_ScalarNearlyEquals( a.y, b.y, absoluteTolerance, relativeTolerance ) &&
           Vec3_ScalarNearlyEquals( a.z, b.z, absoluteTolerance, relativeTolerance );
}

bool_t Vec3_IsNearZero( vec3_t value, f32 tolerance ) noexcept
{
    const bool_t bValidTolerance = Vec3_IsValidTolerance( tolerance );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Vec3_IsNearZero requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Vec3_IsFinite( value ) ) {
        return false;
    }

    vec3_t ignoredNormalized{};
    f32 length = 0.0f;
    if ( !Vec3_NormalizeFinite( value, &ignoredNormalized, &length ) ) {
        return true;
    }
    return length <= tolerance;
}

bool_t Vec3_IsUnitLength( vec3_t value, f32 tolerance ) noexcept
{
    const bool_t bValidTolerance = Vec3_IsValidTolerance( tolerance );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Vec3_IsUnitLength requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Vec3_IsFinite( value ) ) {
        return false;
    }

    vec3_t ignoredNormalized{};
    f32 length = 0.0f;
    if ( !Vec3_NormalizeFinite( value, &ignoredNormalized, &length ) ) {
        return false;
    }

    return std::fabs( length - 1.0f ) <= tolerance;
}

vec3_t Vec3_Abs( vec3_t value ) noexcept
{
    return Vec3_Make(
        std::fabs( value.x ),
        std::fabs( value.y ),
        std::fabs( value.z ) );
}

vec3_t Vec3_Min( vec3_t a, vec3_t b ) noexcept
{
    return Vec3_Make(
        std::min( a.x, b.x ),
        std::min( a.y, b.y ),
        std::min( a.z, b.z ) );
}

vec3_t Vec3_Max( vec3_t a, vec3_t b ) noexcept
{
    return Vec3_Make(
        std::max( a.x, b.x ),
        std::max( a.y, b.y ),
        std::max( a.z, b.z ) );
}

vec3_t Vec3_Clamp( vec3_t value, vec3_t minimum, vec3_t maximum ) noexcept
{
    const bool_t bValidBounds = minimum.x <= maximum.x &&
                                minimum.y <= maximum.y &&
                                minimum.z <= maximum.z;
    CY_ASSERT_MSG( bValidBounds, "Vec3_Clamp requires ordered component bounds." );
    if ( !bValidBounds ) {
        return value;
    }

    return Vec3_Make(
        std::clamp( value.x, minimum.x, maximum.x ),
        std::clamp( value.y, minimum.y, maximum.y ),
        std::clamp( value.z, minimum.z, maximum.z ) );
}

vec3_t Vec3_Floor( vec3_t value ) noexcept
{
    return Vec3_Make(
        std::floor( value.x ),
        std::floor( value.y ),
        std::floor( value.z ) );
}

vec3_t Vec3_Ceil( vec3_t value ) noexcept
{
    return Vec3_Make(
        std::ceil( value.x ),
        std::ceil( value.y ),
        std::ceil( value.z ) );
}

vec3_t Vec3_Round( vec3_t value ) noexcept
{
    return Vec3_Make(
        std::round( value.x ),
        std::round( value.y ),
        std::round( value.z ) );
}

vec3_t Vec3_Truncate( vec3_t value ) noexcept
{
    return Vec3_Make(
        std::trunc( value.x ),
        std::trunc( value.y ),
        std::trunc( value.z ) );
}

f32 Vec3_MinComponent( vec3_t value ) noexcept
{
    return std::min( value.x, std::min( value.y, value.z ) );
}

f32 Vec3_MaxComponent( vec3_t value ) noexcept
{
    return std::max( value.x, std::max( value.y, value.z ) );
}

f32 Vec3_MaxAbsComponent( vec3_t value ) noexcept
{
    return Vec3_MaxComponent( Vec3_Abs( value ) );
}

f32 Vec3_Length( vec3_t value ) noexcept
{
    return std::sqrt( Vec3_LengthSquared( value ) );
}

f32 Vec3_LengthXY( vec3_t value ) noexcept
{
    return std::sqrt( Vec3_LengthXYSquared( value ) );
}

f32 Vec3_Distance( vec3_t a, vec3_t b ) noexcept
{
    return Vec3_Length( Vec3_Subtract( a, b ) );
}

vec3_t Vec3_NormalizeUnchecked( vec3_t value ) noexcept
{
    const f32 lengthSquared = Vec3_LengthSquared( value );
    CY_ASSERT_MSG(
        lengthSquared > 0.0f && std::isfinite( lengthSquared ),
        "Vec3_NormalizeUnchecked requires a finite nonzero vector." );

    const f32 inverseLength = 1.0f / std::sqrt( lengthSquared );
    return Vec3_Scale( value, inverseLength );
}

bool_t Vec3_TryNormalize(
    vec3_t value,
    f32 minimumLength,
    vec3_t *pNormalized,
    f32 *pOriginalLength ) noexcept
{
    const bool_t bValidOutput = ( pNormalized != nullptr );
    const bool_t bValidMinimum = Vec3_IsValidTolerance( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec3_TryNormalize requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3_TryNormalize requires a finite nonnegative minimum length." );

    if ( pOriginalLength != nullptr ) {
        *pOriginalLength = 0.0f;
    }
    if ( !bValidOutput ) {
        return false;
    }
    *pNormalized = CY_VEC3_ZERO;

    if ( !bValidMinimum || !Vec3_IsFinite( value ) ) {
        return false;
    }

    vec3_t normalized{};
    f32 length = 0.0f;
    if ( !Vec3_NormalizeFinite( value, &normalized, &length ) ) {
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

bool_t Vec3_TrySetLength(
    vec3_t value,
    f32 requestedLength,
    f32 minimumInputLength,
    vec3_t *pResult ) noexcept
{
    const bool_t bValidOutput = ( pResult != nullptr );
    const bool_t bValidRequestedLength = Vec3_IsValidTolerance( requestedLength );
    const bool_t bValidMinimum = Vec3_IsValidTolerance( minimumInputLength );
    CY_ASSERT_MSG( bValidOutput, "Vec3_TrySetLength requires output storage." );
    CY_ASSERT_MSG(
        bValidRequestedLength,
        "Vec3_TrySetLength requires a finite nonnegative requested length." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3_TrySetLength requires a finite nonnegative minimum input length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_VEC3_ZERO;

    if ( !bValidRequestedLength || !bValidMinimum ) {
        return false;
    }

    vec3_t normalized{};
    if ( !Vec3_TryNormalize( value, minimumInputLength, &normalized, nullptr ) ) {
        return false;
    }

    *pResult = Vec3_Scale( normalized, requestedLength );
    return true;
}

vec3_t Vec3_LerpClamped( vec3_t a, vec3_t b, f32 t ) noexcept
{
    return Vec3_Lerp( a, b, std::clamp( t, 0.0f, 1.0f ) );
}

vec3_t Vec3_MoveTowards( vec3_t current, vec3_t target, f32 maximumDistance ) noexcept
{
    const bool_t bValidDistance = Vec3_IsValidTolerance( maximumDistance );
    CY_ASSERT_MSG(
        bValidDistance,
        "Vec3_MoveTowards requires a finite nonnegative maximum distance." );
    if ( !bValidDistance ) {
        return current;
    }

    const vec3_t displacement = Vec3_Subtract( target, current );
    vec3_t direction{};
    f32 distance = 0.0f;
    if ( !Vec3_IsFinite( displacement ) ||
         !Vec3_NormalizeFinite( displacement, &direction, &distance ) ) {
        return Vec3_EqualsExact( current, target ) ? target : current;
    }
    if ( distance == 0.0f || distance <= maximumDistance ) {
        return target;
    }

    return Vec3_MulAdd( current, direction, maximumDistance );
}

vec3_t Vec3_ClampLength( vec3_t value, f32 minimumLength, f32 maximumLength ) noexcept
{
    const bool_t bValidMinimum = Vec3_IsValidTolerance( minimumLength );
    const bool_t bValidMaximum = Vec3_IsValidTolerance( maximumLength );
    const bool_t bOrderedBounds = minimumLength <= maximumLength;
    CY_ASSERT_MSG(
        bValidMinimum && bValidMaximum && bOrderedBounds,
        "Vec3_ClampLength requires finite, nonnegative, ordered bounds." );
    if ( !bValidMinimum || !bValidMaximum || !bOrderedBounds ) {
        return value;
    }

    if ( !Vec3_IsFinite( value ) ) {
        return value;
    }

    vec3_t normalized{};
    f32 length = 0.0f;
    if ( !Vec3_NormalizeFinite( value, &normalized, &length ) ) {
        return value;
    }
    if ( length < minimumLength ) {
        return Vec3_Scale( normalized, minimumLength );
    }
    if ( length > maximumLength ) {
        return Vec3_Scale( normalized, maximumLength );
    }
    return value;
}

bool_t Vec3_TryProjectOnto(
    vec3_t value,
    vec3_t onto,
    f32 minimumLength,
    vec3_t *pProjected ) noexcept
{
    const bool_t bValidOutput = ( pProjected != nullptr );
    const bool_t bValidMinimum = Vec3_IsValidTolerance( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec3_TryProjectOnto requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3_TryProjectOnto requires a finite nonnegative minimum length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pProjected = CY_VEC3_ZERO;

    if ( !bValidMinimum || !Vec3_IsFinite( value ) || !Vec3_IsFinite( onto ) ) {
        return false;
    }

    vec3_t unitDirection{};
    if ( !Vec3_TryNormalize( onto, minimumLength, &unitDirection, nullptr ) ) {
        return false;
    }

    const vec3_t projected = Vec3_ProjectOntoUnit( value, unitDirection );
    if ( !Vec3_IsFinite( projected ) ) {
        return false;
    }

    *pProjected = projected;
    return true;
}

bool_t Vec3_TryAngleBetween(
    vec3_t a,
    vec3_t b,
    f32 minimumLength,
    f32 *pAngleRadians ) noexcept
{
    const bool_t bValidOutput = ( pAngleRadians != nullptr );
    const bool_t bValidMinimum = Vec3_IsValidTolerance( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec3_TryAngleBetween requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3_TryAngleBetween requires a finite nonnegative minimum length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pAngleRadians = 0.0f;

    if ( !bValidMinimum ) {
        return false;
    }

    vec3_t normalizedA{};
    vec3_t normalizedB{};
    if ( !Vec3_TryNormalize( a, minimumLength, &normalizedA, nullptr ) ||
         !Vec3_TryNormalize( b, minimumLength, &normalizedB, nullptr ) ) {
        return false;
    }

    *pAngleRadians = Vec3_AngleBetweenUnit( normalizedA, normalizedB );
    return true;
}

bool_t Vec3_TryBuildUnitPerpendicular(
    vec3_t value,
    f32 minimumLength,
    vec3_t *pPerpendicular ) noexcept
{
    const bool_t bValidOutput = ( pPerpendicular != nullptr );
    const bool_t bValidMinimum = Vec3_IsValidTolerance( minimumLength );
    CY_ASSERT_MSG(
        bValidOutput,
        "Vec3_TryBuildUnitPerpendicular requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3_TryBuildUnitPerpendicular requires a finite nonnegative minimum length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pPerpendicular = CY_VEC3_ZERO;

    if ( !bValidMinimum ) {
        return false;
    }

    vec3_t unitDirection{};
    if ( !Vec3_TryNormalize( value, minimumLength, &unitDirection, nullptr ) ) {
        return false;
    }

    const vec3_t axis = Vec3_LeastAlignedAxis( unitDirection );
    const vec3_t perpendicular = Vec3_Cross( unitDirection, axis );
    return Vec3_TryNormalize( perpendicular, 0.0f, pPerpendicular, nullptr );
}

bool_t Vec3_TryRefractUnitNormal(
    vec3_t incident,
    vec3_t unitNormal,
    f32 eta,
    vec3_t *pRefracted ) noexcept
{
    const bool_t bValidOutput = ( pRefracted != nullptr );
    const bool_t bValidEta = std::isfinite( eta ) && eta > 0.0f;
    CY_ASSERT_MSG( bValidOutput, "Vec3_TryRefractUnitNormal requires output storage." );
    CY_ASSERT_MSG(
        bValidEta,
        "Vec3_TryRefractUnitNormal requires a finite positive refractive ratio." );
    if ( !bValidOutput ) {
        return false;
    }
    *pRefracted = CY_VEC3_ZERO;

    if ( !bValidEta || !Vec3_IsFinite( incident ) || !Vec3_IsFinite( unitNormal ) ) {
        return false;
    }

    const f32 normalDotIncident = Vec3_Dot( unitNormal, incident );
    const f32 discriminant = 1.0f - eta * eta *
        ( 1.0f - normalDotIncident * normalDotIncident );
    if ( discriminant < 0.0f ) {
        return false;
    }

    const vec3_t refracted = Vec3_Subtract(
        Vec3_Scale( incident, eta ),
        Vec3_Scale(
            unitNormal,
            eta * normalDotIncident + std::sqrt( discriminant ) ) );
    if ( !Vec3_IsFinite( refracted ) ) {
        return false;
    }

    *pRefracted = refracted;
    return true;
}

f32 Vec3_AngleBetweenUnit( vec3_t a, vec3_t b ) noexcept
{
    const f32 cosine = std::clamp( Vec3_Dot( a, b ), -1.0f, 1.0f );
    return std::acos( cosine );
}

void Vec3_BuildOrthonormalBasis(
    vec3_t unitNormal,
    vec3_t *pTangent,
    vec3_t *pBitangent ) noexcept
{
    const bool_t bValidOutputs = pTangent != nullptr &&
                                 pBitangent != nullptr &&
                                 pTangent != pBitangent;
    CY_ASSERT_MSG(
        bValidOutputs,
        "Vec3_BuildOrthonormalBasis requires distinct output vectors." );
    if ( pTangent != nullptr ) {
        *pTangent = CY_VEC3_ZERO;
    }
    if ( pBitangent != nullptr ) {
        *pBitangent = CY_VEC3_ZERO;
    }
    if ( !bValidOutputs ) {
        return;
    }

    vec3_t tangent{};
    if ( !Vec3_TryBuildUnitPerpendicular( unitNormal, 0.0f, &tangent ) ) {
        CY_ASSERT_MSG(
            false,
            "Vec3_BuildOrthonormalBasis requires a finite nonzero unit normal." );
        return;
    }

    *pTangent = tangent;
    *pBitangent = Vec3_Cross( unitNormal, tangent );
}

} // namespace cypher::math
