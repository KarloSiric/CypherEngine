//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Quaternion.cpp
//  Purpose: Implements quaternion construction, conversion, and interpolation.
//  Details: Checked operations normalize safely and treat q and -q as the same
//           rotation where rotational equivalence rather than storage equality matters.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Quaternion.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

namespace
{

constexpr f32 CY_QUAT_SLERP_LINEAR_THRESHOLD = 0.9995f; // Avoid tiny sin(angle) division.
constexpr f32 CY_QUAT_OPPOSITE_DOT_THRESHOLD = -0.999999f; // Treat nearly antiparallel vectors as 180 degrees.

CYPHER_NODISCARD bool_t Quat_ValidTolerance( f32 tolerance ) noexcept
{
    return Scalar_IsFinite( tolerance ) && tolerance >= 0.0f;
}

CYPHER_NODISCARD bool_t Quat_NormalizeFinite(
    quat_t value,
    quat_t *pNormalized,
    f32 *pLength ) noexcept
{
    // Scale before computing length so extreme finite components do not create
    // an infinite or zero intermediate norm.
    const f32 maximumComponent = Scalar_Max(
        Scalar_Max( Scalar_Abs( value.x ), Scalar_Abs( value.y ) ),
        Scalar_Max( Scalar_Abs( value.z ), Scalar_Abs( value.w ) ) );
    if ( maximumComponent == 0.0f ) {
        *pNormalized = CY_QUAT_IDENTITY;
        *pLength = 0.0f;
        return false;
    }

    const quat_t scaled = Quat_Scale( value, 1.0f / maximumComponent );
    const f32 scaledLength = Scalar_Sqrt( Quat_LengthSquared( scaled ) );
    *pNormalized = Quat_Scale( scaled, 1.0f / scaledLength );
    *pLength = maximumComponent * scaledLength;
    return Quat_IsFinite( *pNormalized );
}

CYPHER_NODISCARD quat_t Quat_FromRotationColumns(
    vec3_t column0,
    vec3_t column1,
    vec3_t column2 ) noexcept
{
    const f32 m00 = column0.x;
    const f32 m01 = column1.x;
    const f32 m02 = column2.x;
    const f32 m10 = column0.y;
    const f32 m11 = column1.y;
    const f32 m12 = column2.y;
    const f32 m20 = column0.z;
    const f32 m21 = column1.z;
    const f32 m22 = column2.z;

    quat_t result{};
    // Choose the matrix-to-quaternion branch with the largest stable diagonal
    // term, avoiding division by a small component near 180-degree rotations.
    const f32 trace = m00 + m11 + m22;
    if ( trace > 0.0f ) {
        const f32 scale = 2.0f * Scalar_Sqrt( trace + 1.0f );
        result.w = 0.25f * scale;
        result.x = ( m21 - m12 ) / scale;
        result.y = ( m02 - m20 ) / scale;
        result.z = ( m10 - m01 ) / scale;
    } else if ( m00 > m11 && m00 > m22 ) {
        const f32 scale = 2.0f * Scalar_Sqrt( 1.0f + m00 - m11 - m22 );
        result.w = ( m21 - m12 ) / scale;
        result.x = 0.25f * scale;
        result.y = ( m01 + m10 ) / scale;
        result.z = ( m02 + m20 ) / scale;
    } else if ( m11 > m22 ) {
        const f32 scale = 2.0f * Scalar_Sqrt( 1.0f + m11 - m00 - m22 );
        result.w = ( m02 - m20 ) / scale;
        result.x = ( m01 + m10 ) / scale;
        result.y = 0.25f * scale;
        result.z = ( m12 + m21 ) / scale;
    } else {
        const f32 scale = 2.0f * Scalar_Sqrt( 1.0f + m22 - m00 - m11 );
        result.w = ( m10 - m01 ) / scale;
        result.x = ( m02 + m20 ) / scale;
        result.y = ( m12 + m21 ) / scale;
        result.z = 0.25f * scale;
    }

    quat_t normalized{};
    f32 ignoredLength = 0.0f;
    return Quat_NormalizeFinite( result, &normalized, &ignoredLength )
        ? normalized
        : CY_QUAT_IDENTITY;
}

} // namespace

bool_t Quat_IsFinite( quat_t value ) noexcept
{
    return Scalar_IsFinite( value.x ) && Scalar_IsFinite( value.y ) &&
           Scalar_IsFinite( value.z ) && Scalar_IsFinite( value.w );
}

bool_t Quat_NearlyEquals(
    quat_t a,
    quat_t b,
    f32 absoluteTolerance,
    f32 relativeTolerance ) noexcept
{
    return Scalar_NearlyEquals( a.x, b.x, absoluteTolerance, relativeTolerance ) &&
           Scalar_NearlyEquals( a.y, b.y, absoluteTolerance, relativeTolerance ) &&
           Scalar_NearlyEquals( a.z, b.z, absoluteTolerance, relativeTolerance ) &&
           Scalar_NearlyEquals( a.w, b.w, absoluteTolerance, relativeTolerance );
}

bool_t Quat_RotationEquivalent(
    quat_t a,
    quat_t b,
    f32 toleranceRadians ) noexcept
{
    const bool_t bValidTolerance = Quat_ValidTolerance( toleranceRadians );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Quat_RotationEquivalent requires a finite nonnegative angular tolerance." );
    if ( !bValidTolerance ) {
        return false;
    }
    quat_t unitA{};
    quat_t unitB{};
    if ( !Quat_TryNormalize( a, 0.0f, &unitA, nullptr ) ||
         !Quat_TryNormalize( b, 0.0f, &unitB, nullptr ) ) {
        return false;
    }
    if ( Quat_Dot( unitA, unitB ) < 0.0f ) {
        unitB = Quat_Negate( unitB );
    }
    if ( Quat_EqualsExact( unitA, unitB ) ) {
        return true;
    }
    if ( toleranceRadians >= CY_PI_F ) {
        return true;
    }
    const f32 maximumChord =
        2.0f * Scalar_Sin( toleranceRadians * 0.25f );
    return Quat_LengthSquared( Quat_Subtract( unitA, unitB ) ) <=
           maximumChord * maximumChord;
}

f32 Quat_Length( quat_t value ) noexcept
{
    return Scalar_Sqrt( Quat_LengthSquared( value ) );
}

quat_t Quat_NormalizeUnchecked( quat_t value ) noexcept
{
    const f32 lengthSquared = Quat_LengthSquared( value );
    CY_ASSERT_MSG(
        lengthSquared > 0.0f && Scalar_IsFinite( lengthSquared ),
        "Quat_NormalizeUnchecked requires a finite nonzero quaternion." );
    return Quat_Scale( value, Scalar_InvSqrt( lengthSquared ) );
}

bool_t Quat_TryNormalize(
    quat_t value,
    f32 minimumLength,
    quat_t *pNormalized,
    f32 *pOriginalLength ) noexcept
{
    const bool_t bValidOutput = pNormalized != nullptr;
    const bool_t bValidMinimum = Quat_ValidTolerance( minimumLength );
    CY_ASSERT_MSG( bValidOutput, "Quat_TryNormalize requires output storage." );
    CY_ASSERT_MSG(
        bValidMinimum,
        "Quat_TryNormalize requires a finite nonnegative minimum length." );
    if ( pOriginalLength != nullptr ) {
        *pOriginalLength = 0.0f;
    }
    if ( !bValidOutput ) {
        return false;
    }
    *pNormalized = CY_QUAT_IDENTITY;
    if ( !bValidMinimum || !Quat_IsFinite( value ) ) {
        return false;
    }

    quat_t normalized{};
    f32 length = 0.0f;
    if ( !Quat_NormalizeFinite( value, &normalized, &length ) ) {
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

bool_t Quat_TryInverse(
    quat_t value,
    f32 minimumLength,
    quat_t *pInverse ) noexcept
{
    const bool_t bValidOutput = pInverse != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Quat_TryInverse requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pInverse = CY_QUAT_IDENTITY;

    quat_t unit{};
    f32 length = 0.0f;
    if ( !Quat_TryNormalize( value, minimumLength, &unit, &length ) ) {
        return false;
    }
    // conjugate(q) / |q|^2; unit is q / |q|, so one further |q| division remains.
    *pInverse = Quat_Scale( Quat_Conjugate( unit ), 1.0f / length );
    return Quat_IsFinite( *pInverse );
}

bool_t Quat_IsUnit( quat_t value, f32 tolerance ) noexcept
{
    const bool_t bValidTolerance = Quat_ValidTolerance( tolerance );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Quat_IsUnit requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Quat_IsFinite( value ) ) {
        return false;
    }

    quat_t ignored{};
    f32 length = 0.0f;
    return Quat_NormalizeFinite( value, &ignored, &length ) &&
           Scalar_Abs( length - 1.0f ) <= tolerance;
}

vec3_t Quat_RotateVectorUnit( quat_t unitRotation, vec3_t value ) noexcept
{
    // Expanded q * v * conjugate(q) avoids constructing two temporary quaternions.
    const vec3_t quaternionVector = Quat_VectorPart( unitRotation );
    const vec3_t doubledCross = Vec3_Scale(
        Vec3_Cross( quaternionVector, value ), 2.0f );
    return Vec3_Add(
        value,
        Vec3_Add(
            Vec3_Scale( doubledCross, unitRotation.w ),
            Vec3_Cross( quaternionVector, doubledCross ) ) );
}

vec3_t Quat_InverseRotateVectorUnit( quat_t unitRotation, vec3_t value ) noexcept
{
    return Quat_RotateVectorUnit( Quat_Conjugate( unitRotation ), value );
}

vec3_t Quat_Forward( quat_t unitRotation ) noexcept
{
    return Quat_RotateVectorUnit( unitRotation, CY_VEC3_FORWARD );
}

vec3_t Quat_Left( quat_t unitRotation ) noexcept
{
    return Quat_RotateVectorUnit( unitRotation, CY_VEC3_LEFT );
}

vec3_t Quat_Up( quat_t unitRotation ) noexcept
{
    return Quat_RotateVectorUnit( unitRotation, CY_VEC3_UP );
}

quat_t Quat_FromUnitAxisAngle( vec3_t unitAxis, angle_t angle ) noexcept
{
    const f32 halfAngle = angle.radians * 0.5f;
    f32 sine = 0.0f;
    f32 cosine = 1.0f;
    Scalar_SinCos( halfAngle, &sine, &cosine );
    return Quat_FromVectorScalar( Vec3_Scale( unitAxis, sine ), cosine );
}

bool_t Quat_TryFromAxisAngle(
    vec3_t axis,
    angle_t angle,
    f32 minimumAxisLength,
    quat_t *pRotation ) noexcept
{
    const bool_t bValidOutput = pRotation != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Quat_TryFromAxisAngle requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pRotation = CY_QUAT_IDENTITY;
    if ( !Angle_IsFinite( angle ) ) {
        return false;
    }

    vec3_t unitAxis{};
    if ( !Vec3_TryNormalize( axis, minimumAxisLength, &unitAxis, nullptr ) ) {
        return false;
    }
    *pRotation = Quat_FromUnitAxisAngle( unitAxis, angle );
    return Quat_IsFinite( *pRotation );
}

quat_t Quat_FromEulerXYZ( vec3_t anglesRadians ) noexcept
{
    const f32 halfX = anglesRadians.x * 0.5f;
    const f32 halfY = anglesRadians.y * 0.5f;
    const f32 halfZ = anglesRadians.z * 0.5f;
    f32 sx = 0.0f;
    f32 cx = 1.0f;
    f32 sy = 0.0f;
    f32 cy = 1.0f;
    f32 sz = 0.0f;
    f32 cz = 1.0f;
    Scalar_SinCos( halfX, &sx, &cx );
    Scalar_SinCos( halfY, &sy, &cy );
    Scalar_SinCos( halfZ, &sz, &cz );

    return Quat_Make(
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz );
}

vec3_t Quat_ToEulerXYZ( quat_t unitRotation ) noexcept
{
    const f32 sinXCosY = 2.0f *
        ( unitRotation.w * unitRotation.x + unitRotation.y * unitRotation.z );
    const f32 cosXCosY = 1.0f - 2.0f *
        ( unitRotation.x * unitRotation.x + unitRotation.y * unitRotation.y );
    const f32 sinY = 2.0f *
        ( unitRotation.w * unitRotation.y - unitRotation.z * unitRotation.x );
    const f32 sinZCosY = 2.0f *
        ( unitRotation.w * unitRotation.z + unitRotation.x * unitRotation.y );
    const f32 cosZCosY = 1.0f - 2.0f *
        ( unitRotation.y * unitRotation.y + unitRotation.z * unitRotation.z );

    return Vec3_Make(
        Scalar_Atan2( sinXCosY, cosXCosY ),
        Scalar_AsinClamped( sinY ),
        Scalar_Atan2( sinZCosY, cosZCosY ) );
}

bool_t Quat_TryToAxisAngle(
    quat_t rotation,
    f32 minimumLength,
    vec3_t *pUnitAxis,
    angle_t *pAngle ) noexcept
{
    const bool_t bValidOutputs = pUnitAxis != nullptr && pAngle != nullptr;
    CY_ASSERT_MSG( bValidOutputs, "Quat_TryToAxisAngle requires output storage." );
    if ( !bValidOutputs ) {
        return false;
    }
    *pUnitAxis = CY_VEC3_FORWARD;
    *pAngle = CY_ANGLE_ZERO;

    quat_t unit{};
    if ( !Quat_TryNormalize( rotation, minimumLength, &unit, nullptr ) ) {
        return false;
    }
    if ( unit.w < 0.0f ) {
        unit = Quat_Negate( unit );
    }

    const f32 clampedW = Scalar_Clamp( unit.w, -1.0f, 1.0f );
    const f32 sinHalfSquared = Scalar_Max( 0.0f, 1.0f - clampedW * clampedW );
    const f32 sinHalf = Scalar_Sqrt( sinHalfSquared );
    *pAngle = Angle_FromRadians( 2.0f * Scalar_AcosClamped( clampedW ) );
    if ( sinHalf > 0.0f ) {
        *pUnitAxis = Vec3_DivideScalar( Quat_VectorPart( unit ), sinHalf );
    }
    return true;
}

bool_t Quat_TryFromToRotation(
    vec3_t from,
    vec3_t to,
    f32 minimumLength,
    quat_t *pRotation ) noexcept
{
    const bool_t bValidOutput = pRotation != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Quat_TryFromToRotation requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pRotation = CY_QUAT_IDENTITY;

    vec3_t unitFrom{};
    vec3_t unitTo{};
    if ( !Vec3_TryNormalize( from, minimumLength, &unitFrom, nullptr ) ||
         !Vec3_TryNormalize( to, minimumLength, &unitTo, nullptr ) ) {
        return false;
    }

    const f32 dot = Scalar_Clamp( Vec3_Dot( unitFrom, unitTo ), -1.0f, 1.0f );
    if ( dot >= 1.0f ) {
        return true;
    }
    if ( dot <= CY_QUAT_OPPOSITE_DOT_THRESHOLD ) {
        // Antiparallel vectors have infinitely many valid axes; choose a stable
        // perpendicular deterministically and rotate by exactly pi.
        vec3_t axis{};
        if ( !Vec3_TryBuildUnitPerpendicular( unitFrom, 0.0f, &axis ) ) {
            return false;
        }
        *pRotation = Quat_FromUnitAxisAngle( axis, CY_ANGLE_HALF_TURN );
        return true;
    }

    const quat_t candidate = Quat_FromVectorScalar(
        Vec3_Cross( unitFrom, unitTo ), 1.0f + dot );
    return Quat_TryNormalize( candidate, 0.0f, pRotation, nullptr );
}

bool_t Quat_TryLookRotation(
    vec3_t forward,
    vec3_t upHint,
    f32 minimumLength,
    quat_t *pRotation ) noexcept
{
    const bool_t bValidOutput = pRotation != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Quat_TryLookRotation requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pRotation = CY_QUAT_IDENTITY;

    vec3_t unitForward{};
    if ( !Vec3_TryNormalize( forward, minimumLength, &unitForward, nullptr ) ) {
        return false;
    }
    vec3_t unitLeft{};
    if ( !Vec3_TryNormalize(
             Vec3_Cross( upHint, unitForward ),
             minimumLength,
             &unitLeft,
             nullptr ) ) {
        return false;
    }
    // Recompute up from the normalized forward/left pair to remove skew in the hint.
    const vec3_t unitUp = Vec3_Cross( unitForward, unitLeft );
    *pRotation = Quat_FromRotationColumns( unitForward, unitLeft, unitUp );
    return true;
}

quat_t Quat_Nlerp( quat_t a, quat_t b, f32 t ) noexcept
{
    quat_t unitA{};
    quat_t unitB{};
    if ( !Quat_TryNormalize( a, 0.0f, &unitA, nullptr ) ||
         !Quat_TryNormalize( b, 0.0f, &unitB, nullptr ) ) {
        return CY_QUAT_IDENTITY;
    }
    if ( Quat_Dot( unitA, unitB ) < 0.0f ) {
        unitB = Quat_Negate( unitB );
    }

    const quat_t blended = Quat_Add(
        unitA,
        Quat_Scale( Quat_Subtract( unitB, unitA ), t ) );
    quat_t result{};
    return Quat_TryNormalize( blended, 0.0f, &result, nullptr )
        ? result
        : CY_QUAT_IDENTITY;
}

quat_t Quat_Slerp( quat_t a, quat_t b, f32 t ) noexcept
{
    quat_t unitA{};
    quat_t unitB{};
    if ( !Quat_TryNormalize( a, 0.0f, &unitA, nullptr ) ||
         !Quat_TryNormalize( b, 0.0f, &unitB, nullptr ) ) {
        return CY_QUAT_IDENTITY;
    }

    f32 dot = Quat_Dot( unitA, unitB );
    if ( dot < 0.0f ) {
        // q and -q represent the same orientation. Pick the same hemisphere so
        // interpolation follows the shortest arc.
        unitB = Quat_Negate( unitB );
        dot = -dot;
    }
    dot = Scalar_Clamp( dot, 0.0f, 1.0f );
    if ( dot >= CY_QUAT_SLERP_LINEAR_THRESHOLD ) {
        return Quat_Nlerp( unitA, unitB, t );
    }

    const f32 angle = Scalar_AcosClamped( dot );
    const f32 sinAngle = Scalar_Sin( angle );
    const f32 weightA = Scalar_Sin( ( 1.0f - t ) * angle ) / sinAngle;
    const f32 weightB = Scalar_Sin( t * angle ) / sinAngle;
    const quat_t result = Quat_Add(
        Quat_Scale( unitA, weightA ),
        Quat_Scale( unitB, weightB ) );
    quat_t normalized{};
    return Quat_TryNormalize( result, 0.0f, &normalized, nullptr )
        ? normalized
        : CY_QUAT_IDENTITY;
}

angle_t Quat_AngleBetween( quat_t a, quat_t b ) noexcept
{
    quat_t unitA{};
    quat_t unitB{};
    if ( !Quat_TryNormalize( a, 0.0f, &unitA, nullptr ) ||
         !Quat_TryNormalize( b, 0.0f, &unitB, nullptr ) ) {
        return CY_ANGLE_HALF_TURN;
    }
    if ( Quat_Dot( unitA, unitB ) < 0.0f ) {
        unitB = Quat_Negate( unitB );
    }
    const f32 halfChord = Scalar_Clamp(
        Quat_Length( Quat_Subtract( unitA, unitB ) ) * 0.5f,
        0.0f, 1.0f );
    return Angle_FromRadians( 4.0f * Scalar_AsinClamped( halfChord ) );
}

} // namespace cypher::math
