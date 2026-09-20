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
#include <limits>
#include <cmath>

namespace cypher::math
{

namespace
{

CYPHER_NODISCARD bool_t Vec3_IsValidTolerance( f32 tolerance ) noexcept
{
    return std::isfinite( tolerance ) && tolerance >= 0.0f;
}

CYPHER_NODISCARD bool_t Vec3d_IsValidMinimumLength( f64 minimumLength ) noexcept
{
    return std::isfinite( minimumLength ) && minimumLength >= 0.0;
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

    // Relative tolerance scales with magnitude while absolute tolerance protects zero.
    const f32 scale = std::max( std::fabs( a ), std::fabs( b ) );
    const f32 tolerance = std::max(
        absoluteTolerance,
        relativeTolerance * scale );
    return difference <= tolerance;
}

CYPHER_NODISCARD bool_t Vec3d_ScalarNearlyEquals(
    f64 a,
    f64 b,
    f64 absoluteTolerance,
    f64 relativeTolerance ) noexcept
{
    if ( a == b ) {
        return true;
    }
    if ( !std::isfinite( a ) || !std::isfinite( b ) ) {
        return false;
    }

    const f64 difference = std::fabs( a - b );
    if ( !std::isfinite( difference ) ) {
        return false;
    }

    const f64 scale = std::max( std::fabs( a ), std::fabs( b ) );
    const f64 tolerance = std::max( absoluteTolerance, relativeTolerance * scale );
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

CYPHER_NODISCARD bool_t Vec3d_NormalizeFinite(
    vec3d_t value,
    vec3d_t *pNormalized,
    f64 *pLength ) noexcept
{
    const f64 maximumComponent = std::max(
        std::fabs( value.x ),
        std::max( std::fabs( value.y ), std::fabs( value.z ) ) );
    if ( maximumComponent == 0.0 ) {
        *pNormalized = CY_VEC3D_ZERO;
        *pLength = 0.0;
        return false;
    }

    const vec3d_t scaled = Vec3d_DivideScalar( value, maximumComponent );
    const f64 scaledLength = std::sqrt( Vec3d_LengthSquared( scaled ) );
    const vec3d_t normalized = Vec3d_DivideScalar( scaled, scaledLength );
    const f64 length = maximumComponent * scaledLength;
    if ( !Vec3d_IsFinite( normalized ) || !std::isfinite( length ) ) {
        *pNormalized = CY_VEC3D_ZERO;
        *pLength = 0.0;
        return false;
    }

    *pNormalized = normalized;
    *pLength = length;
    return true;
}

CYPHER_NODISCARD vec3_t Vec3_LeastAlignedAxis( vec3_t value ) noexcept
{
    // Crossing with the least-aligned principal axis maximizes numerical area.
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

CYPHER_NODISCARD vec3d_t Vec3d_LeastAlignedAxis( vec3d_t value ) noexcept
{
    const f64 x = std::fabs( value.x );
    const f64 y = std::fabs( value.y );
    const f64 z = std::fabs( value.z );

    if ( x <= y && x <= z ) {
        return CY_VEC3D_FORWARD;
    }
    if ( y <= z ) {
        return CY_VEC3D_LEFT;
    }
    return CY_VEC3D_UP;
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

// NOTE: Adding new double precisions implementations for 64 bit fixed size length of vec3 -> vec3d_t!!!
vec3d_t Vec3d_FromArray( const f64 *pValues ) noexcept
{
    CY_ASSERT_MSG( pValues != nullptr, "Vec3d_FromArray requires source storage." );
    if ( pValues == nullptr ) {
        return CY_VEC3D_ZERO;
    }
    return Vec3d_Make( pValues[0], pValues[1], pValues[2] );
}

void Vec3d_Store( vec3d_t value, f64 *pValues ) noexcept
{
    CY_ASSERT_MSG( pValues != nullptr, "Vec3d_Store requires destination storage." );
    if ( pValues == nullptr ) {
        return;
    }
    pValues[0] = value.x;
    pValues[1] = value.y;
    pValues[2] = value.z;
}

f64 Vec3d_Component( vec3d_t value, u32 iComponent ) noexcept
{
    CY_ASSERT_MSG( iComponent < 3u, "Vec3d_Component index is outside the vector." );
    switch ( iComponent ) {
        case 0u: return value.x;
        case 1u: return value.y;
        case 2u: return value.z;
        default: return 0.0;
    }
}

void Vec3d_SetComponent( vec3d_t *pValue, u32 iComponent, f64 value ) noexcept
{
    CY_ASSERT_MSG( pValue != nullptr, "Vec3d_SetComponent requires vector storage." );
    CY_ASSERT_MSG( iComponent < 3u, "Vec3d_SetComponent index is outside the vector." );
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

bool_t Vec3d_TryToVec3( vec3d_t value, vec3_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Vec3d_TryToVec3 requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_VEC3_ZERO;
    if ( !Vec3d_IsFinite( value ) ) {
        return false;
    }
    constexpr f64 kMaxF32AsF64 = static_cast<f64>( std::numeric_limits<f32>::max() );
    if ( std::fabs( value.x ) > kMaxF32AsF64 ||
         std::fabs( value.y ) > kMaxF32AsF64 ||
         std::fabs( value.z ) > kMaxF32AsF64 ) {
        return false;
    }
    *pResult = Vec3_Make(
        static_cast<f32>( value.x ),
        static_cast<f32>( value.y ),
        static_cast<f32>( value.z ) );
    return true;
}

bool_t Vec3_IsFinite( vec3_t value ) noexcept
{
    return std::isfinite( value.x ) &&
           std::isfinite( value.y ) &&
           std::isfinite( value.z );
}

bool_t Vec3d_IsFinite( vec3d_t value ) noexcept
{
    return std::isfinite( value.x ) &&
           std::isfinite( value.y ) &&
           std::isfinite( value.z );
}

bool_t Vec3d_TryLength( vec3d_t value, f64 *pLength ) noexcept
{
    const bool_t bValidOutput = pLength != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Vec3d_TryLength requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pLength = 0.0;
    if ( !Vec3d_IsFinite( value ) ) {
        return false;
    }
    if ( Vec3d_EqualsExact( value, CY_VEC3D_ZERO ) ) {
        return true;
    }

    vec3d_t ignoredNormalized{};
    f64 length = 0.0;
    if ( !Vec3d_NormalizeFinite( value, &ignoredNormalized, &length ) ) {
        return false;
    }
    *pLength = length;
    return true;
}

bool_t Vec3d_NearlyEquals(
    vec3d_t a,
    vec3d_t b,
    f64 absoluteTolerance,
    f64 relativeTolerance ) noexcept
{
    const bool_t bValidAbsoluteTolerance = Vec3d_IsValidMinimumLength( absoluteTolerance );
    const bool_t bValidRelativeTolerance = Vec3d_IsValidMinimumLength( relativeTolerance );
    CY_ASSERT_MSG(
        bValidAbsoluteTolerance,
        "Vec3d_NearlyEquals requires a finite nonnegative absolute tolerance." );
    CY_ASSERT_MSG(
        bValidRelativeTolerance,
        "Vec3d_NearlyEquals requires a finite nonnegative relative tolerance." );
    if ( !bValidAbsoluteTolerance || !bValidRelativeTolerance ) {
        return false;
    }

    return Vec3d_ScalarNearlyEquals( a.x, b.x, absoluteTolerance, relativeTolerance ) &&
           Vec3d_ScalarNearlyEquals( a.y, b.y, absoluteTolerance, relativeTolerance ) &&
           Vec3d_ScalarNearlyEquals( a.z, b.z, absoluteTolerance, relativeTolerance );
}

bool_t Vec3d_IsNearZero( vec3d_t value, f64 tolerance ) noexcept
{
    const bool_t bValidTolerance = Vec3d_IsValidMinimumLength( tolerance );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Vec3d_IsNearZero requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Vec3d_IsFinite( value ) ) {
        return false;
    }

    vec3d_t ignoredNormalized{};
    f64 length = 0.0;
    if ( !Vec3d_NormalizeFinite( value, &ignoredNormalized, &length ) ) {
        return true;
    }
    return length <= tolerance;
}

bool_t Vec3d_IsUnitLength( vec3d_t value, f64 tolerance ) noexcept
{
    const bool_t bValidTolerance = Vec3d_IsValidMinimumLength( tolerance );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Vec3d_IsUnitLength requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Vec3d_IsFinite( value ) ) {
        return false;
    }

    vec3d_t ignoredNormalized{};
    f64 length = 0.0;
    if ( !Vec3d_NormalizeFinite( value, &ignoredNormalized, &length ) ) {
        return false;
    }

    return std::fabs( length - 1.0 ) <= tolerance;
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
    // Hot path for inputs whose nonzero finite contract is already established.
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

    // Checked APIs initialize outputs before any failure path.

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

bool_t Vec3d_TryNormalize(
    vec3d_t value,
    f64 minimumLength,
    vec3d_t *pNormalized,
    f64 *pOriginalLength ) noexcept
{
    const bool_t bValidOutput = pNormalized != nullptr;
    const bool_t bValidMinimum = Vec3d_IsValidMinimumLength( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec3d_TryNormalize requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3d_TryNormalize requires a finite nonnegative minimum length." );
    if ( pOriginalLength != nullptr ) {
        *pOriginalLength = 0.0;
    }
    if ( !bValidOutput ) {
        return false;
    }
    *pNormalized = CY_VEC3D_ZERO;
    if ( !bValidMinimum || !Vec3d_IsFinite( value ) ) {
        return false;
    }

    vec3d_t normalized{};
    f64 length = 0.0;
    if ( !Vec3d_NormalizeFinite( value, &normalized, &length ) ) {
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

vec3d_t Vec3d_Abs( vec3d_t value ) noexcept
{
    return Vec3d_Make( std::fabs( value.x ), std::fabs( value.y ), std::fabs( value.z ) );
}

vec3d_t Vec3d_Min( vec3d_t a, vec3d_t b ) noexcept
{
    return Vec3d_Make( std::min( a.x, b.x ), std::min( a.y, b.y ), std::min( a.z, b.z ) );
}

vec3d_t Vec3d_Max( vec3d_t a, vec3d_t b ) noexcept
{
    return Vec3d_Make( std::max( a.x, b.x ), std::max( a.y, b.y ), std::max( a.z, b.z ) );
}

vec3d_t Vec3d_Clamp( vec3d_t value, vec3d_t minimum, vec3d_t maximum ) noexcept
{
    const bool_t bValidBounds = minimum.x <= maximum.x &&
                                minimum.y <= maximum.y &&
                                minimum.z <= maximum.z;
    CY_ASSERT_MSG( bValidBounds, "Vec3d_Clamp requires ordered component bounds." );
    if ( !bValidBounds ) {
        return value;
    }
    return Vec3d_Make(
        std::clamp( value.x, minimum.x, maximum.x ),
        std::clamp( value.y, minimum.y, maximum.y ),
        std::clamp( value.z, minimum.z, maximum.z ) );
}

vec3d_t Vec3d_Floor( vec3d_t value ) noexcept
{
    return Vec3d_Make( std::floor( value.x ), std::floor( value.y ), std::floor( value.z ) );
}

vec3d_t Vec3d_Ceil( vec3d_t value ) noexcept
{
    return Vec3d_Make( std::ceil( value.x ), std::ceil( value.y ), std::ceil( value.z ) );
}

vec3d_t Vec3d_Round( vec3d_t value ) noexcept
{
    return Vec3d_Make( std::round( value.x ), std::round( value.y ), std::round( value.z ) );
}

vec3d_t Vec3d_Truncate( vec3d_t value ) noexcept
{
    return Vec3d_Make( std::trunc( value.x ), std::trunc( value.y ), std::trunc( value.z ) );
}

f64 Vec3d_MinComponent( vec3d_t value ) noexcept
{
    return std::min( value.x, std::min( value.y, value.z ) );
}

f64 Vec3d_MaxComponent( vec3d_t value ) noexcept
{
    return std::max( value.x, std::max( value.y, value.z ) );
}

f64 Vec3d_MaxAbsComponent( vec3d_t value ) noexcept
{
    return Vec3d_MaxComponent( Vec3d_Abs( value ) );
}

bool_t Vec3d_TryDistance( vec3d_t a, vec3d_t b, f64 *pDistance ) noexcept
{
    return Vec3d_TryLength( Vec3d_Subtract( a, b ), pDistance );
}

vec3d_t Vec3d_LerpClamped( vec3d_t a, vec3d_t b, f64 t ) noexcept
{
    return Vec3d_Lerp( a, b, std::clamp( t, 0.0, 1.0 ) );
}

vec3d_t Vec3d_MoveTowards( vec3d_t current, vec3d_t target, f64 maximumDistance ) noexcept
{
    const bool_t bValidDistance = Vec3d_IsValidMinimumLength( maximumDistance );
    CY_ASSERT_MSG(
        bValidDistance,
        "Vec3d_MoveTowards requires a finite nonnegative maximum distance." );
    if ( !bValidDistance ) {
        return current;
    }

    const vec3d_t displacement = Vec3d_Subtract( target, current );
    vec3d_t direction{};
    f64 distance = 0.0;
    if ( !Vec3d_IsFinite( displacement ) ||
         !Vec3d_NormalizeFinite( displacement, &direction, &distance ) ) {
        return Vec3d_EqualsExact( current, target ) ? target : current;
    }
    if ( distance == 0.0 || distance <= maximumDistance ) {
        return target;
    }

    return Vec3d_MulAdd( current, direction, maximumDistance );
}

vec3d_t Vec3d_ClampLength( vec3d_t value, f64 minimumLength, f64 maximumLength ) noexcept
{
    const bool_t bValidMinimum = Vec3d_IsValidMinimumLength( minimumLength );
    const bool_t bValidMaximum = Vec3d_IsValidMinimumLength( maximumLength );
    const bool_t bOrderedBounds = minimumLength <= maximumLength;
    CY_ASSERT_MSG(
        bValidMinimum && bValidMaximum && bOrderedBounds,
        "Vec3d_ClampLength requires finite, nonnegative, ordered bounds." );
    if ( !bValidMinimum || !bValidMaximum || !bOrderedBounds ) {
        return value;
    }
    if ( !Vec3d_IsFinite( value ) ) {
        return value;
    }
    vec3d_t normalized{};
    f64 length = 0.0;
    if ( !Vec3d_NormalizeFinite( value, &normalized, &length ) ) {
        return value;
    }
    if ( length < minimumLength ) {
        return Vec3d_Scale( normalized, minimumLength );
    }
    if ( length > maximumLength ) {
        return Vec3d_Scale( normalized, maximumLength );
    }
    return value;
}

bool_t Vec3d_TrySetLength(
    vec3d_t value,
    f64 requestedLength,
    f64 minimumInputLength,
    vec3d_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    const bool_t bValidRequestedLength = Vec3d_IsValidMinimumLength( requestedLength );
    const bool_t bValidMinimum = Vec3d_IsValidMinimumLength( minimumInputLength );
    CY_ASSERT_MSG( bValidOutput, "Vec3d_TrySetLength requires output storage." );
    CY_ASSERT_MSG(
        bValidRequestedLength,
        "Vec3d_TrySetLength requires a finite nonnegative requested length." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3d_TrySetLength requires a finite nonnegative minimum input length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_VEC3D_ZERO;
    if ( !bValidRequestedLength || !bValidMinimum ) {
        return false;
    }

    vec3d_t normalized{};
    if ( !Vec3d_TryNormalize( value, minimumInputLength, &normalized, nullptr ) ) {
        return false;
    }

    *pResult = Vec3d_Scale( normalized, requestedLength );
    return true;
}

bool_t Vec3d_TryProjectOnto(
    vec3d_t value,
    vec3d_t onto,
    f64 minimumLength,
    vec3d_t *pProjected ) noexcept
{
    const bool_t bValidOutput = pProjected != nullptr;
    const bool_t bValidMinimum = Vec3d_IsValidMinimumLength( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec3d_TryProjectOnto requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3d_TryProjectOnto requires a finite nonnegative minimum length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pProjected = CY_VEC3D_ZERO;

    if ( !bValidMinimum || !Vec3d_IsFinite( value ) || !Vec3d_IsFinite( onto ) ) {
        return false;
    }

    vec3d_t unitDirection{};
    if ( !Vec3d_TryNormalize( onto, minimumLength, &unitDirection, nullptr ) ) {
        return false;
    }

    const vec3d_t projected = Vec3d_ProjectOntoUnit( value, unitDirection );
    if ( !Vec3d_IsFinite( projected ) ) {
        return false;
    }

    *pProjected = projected;
    return true;
}

f64 Vec3d_AngleBetweenUnit( vec3d_t a, vec3d_t b ) noexcept
{
    const f64 cosine = std::clamp( Vec3d_Dot( a, b ), -1.0, 1.0 );
    return std::acos( cosine );
}

bool_t Vec3d_TryAngleBetween(
    vec3d_t a,
    vec3d_t b,
    f64 minimumLength,
    f64 *pAngleRadians ) noexcept
{
    const bool_t bValidOutput = pAngleRadians != nullptr;
    const bool_t bValidMinimum = Vec3d_IsValidMinimumLength( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Vec3d_TryAngleBetween requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3d_TryAngleBetween requires a finite nonnegative minimum length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pAngleRadians = 0.0;

    if ( !bValidMinimum ) {
        return false;
    }

    vec3d_t normalizedA{};
    vec3d_t normalizedB{};
    if ( !Vec3d_TryNormalize( a, minimumLength, &normalizedA, nullptr ) ||
         !Vec3d_TryNormalize( b, minimumLength, &normalizedB, nullptr ) ) {
        return false;
    }

    *pAngleRadians = Vec3d_AngleBetweenUnit( normalizedA, normalizedB );
    return true;
}

bool_t Vec3d_TryBuildUnitPerpendicular(
    vec3d_t value,
    f64 minimumLength,
    vec3d_t *pPerpendicular ) noexcept
{
    const bool_t bValidOutput = pPerpendicular != nullptr;
    const bool_t bValidMinimum = Vec3d_IsValidMinimumLength( minimumLength );
    CY_ASSERT_MSG(
        bValidOutput,
        "Vec3d_TryBuildUnitPerpendicular requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Vec3d_TryBuildUnitPerpendicular requires a finite nonnegative minimum length." );
    if ( !bValidOutput ) {
        return false;
    }
    *pPerpendicular = CY_VEC3D_ZERO;

    if ( !bValidMinimum ) {
        return false;
    }

    vec3d_t unitDirection{};
    if ( !Vec3d_TryNormalize( value, minimumLength, &unitDirection, nullptr ) ) {
        return false;
    }

    const vec3d_t axis = Vec3d_LeastAlignedAxis( unitDirection );
    const vec3d_t perpendicular = Vec3d_Cross( unitDirection, axis );
    return Vec3d_TryNormalize( perpendicular, 0.0, pPerpendicular, nullptr );
}

void Vec3d_BuildOrthonormalBasis(
    vec3d_t unitNormal,
    vec3d_t *pTangent,
    vec3d_t *pBitangent ) noexcept
{
    const bool_t bValidOutputs = pTangent != nullptr &&
                                 pBitangent != nullptr &&
                                 pTangent != pBitangent;
    CY_ASSERT_MSG(
        bValidOutputs,
        "Vec3d_BuildOrthonormalBasis requires distinct output vectors." );
    if ( pTangent != nullptr ) {
        *pTangent = CY_VEC3D_ZERO;
    }
    if ( pBitangent != nullptr ) {
        *pBitangent = CY_VEC3D_ZERO;
    }
    if ( !bValidOutputs ) {
        return;
    }

    vec3d_t tangent{};
    if ( !Vec3d_TryBuildUnitPerpendicular( unitNormal, 0.0, &tangent ) ) {
        CY_ASSERT_MSG(
            false,
            "Vec3d_BuildOrthonormalBasis requires a finite nonzero unit normal." );
        return;
    }

    *pTangent = tangent;
    *pBitangent = Vec3d_Cross( unitNormal, tangent );
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

    // Normalize with scaling so very large finite coordinates remain usable.
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

    // Normalize once, then use the cheaper projection contract for unit axes.
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

    // The least-aligned axis avoids a near-zero cross product.
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

    // Negative discriminant means total internal reflection; no real refracted ray.
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
    // Clamp accumulated floating-point error before acos.
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

    // Cross order preserves the library's right-handed tangent basis.
    *pTangent = tangent;
    *pBitangent = Vec3_Cross( unitNormal, tangent );
}

} // namespace cypher::math
