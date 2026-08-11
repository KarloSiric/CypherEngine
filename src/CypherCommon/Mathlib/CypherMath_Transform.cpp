//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Transform.cpp
//  Purpose: Implements decomposed transform operations and affine conversion.
//  Details: Decomposition rejects shear instead of silently discarding it, and
//           exact composition stays in affine form for predictable editor behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Transform.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

bool_t Transform_IsFinite( transform_t value ) noexcept
{
    return Vec3_IsFinite( value.position ) &&
           Quat_IsFinite( value.rotation ) &&
           Vec3_IsFinite( value.scale );
}

bool_t Transform_NearlyEquals(
    transform_t a,
    transform_t b,
    f32 linearAbsoluteTolerance,
    f32 linearRelativeTolerance,
    f32 angularToleranceRadians ) noexcept
{
    return Vec3_NearlyEquals(
               a.position, b.position,
               linearAbsoluteTolerance, linearRelativeTolerance ) &&
           Quat_RotationEquivalent(
               a.rotation, b.rotation, angularToleranceRadians ) &&
           Vec3_NearlyEquals(
               a.scale, b.scale,
               linearAbsoluteTolerance, linearRelativeTolerance );
}

bool_t Transform_HasUniformScale( transform_t value, f32 tolerance ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( tolerance ) && tolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Transform_HasUniformScale requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Vec3_IsFinite( value.scale ) ) {
        return false;
    }
    return Scalar_NearlyEquals( value.scale.x, value.scale.y, tolerance, 0.0f ) &&
           Scalar_NearlyEquals( value.scale.x, value.scale.z, tolerance, 0.0f );
}

vec3_t Transform_TransformPoint( transform_t transform, vec3_t point ) noexcept
{
    const vec3_t scaled = Vec3_MultiplyComponents( point, transform.scale );
    return Vec3_Add(
        transform.position,
        Quat_RotateVectorUnit( transform.rotation, scaled ) );
}

vec3_t Transform_TransformDirection(
    transform_t transform,
    vec3_t direction ) noexcept
{
    return Quat_RotateVectorUnit(
        transform.rotation,
        Vec3_MultiplyComponents( direction, transform.scale ) );
}

bool_t Transform_TryInversePoint(
    transform_t transform,
    vec3_t point,
    f32 minimumAbsScale,
    vec3_t *pLocalPoint ) noexcept
{
    const bool_t bValidOutput = pLocalPoint != nullptr;
    const bool_t bValidThreshold = Scalar_IsFinite( minimumAbsScale ) &&
                                   minimumAbsScale >= 0.0f;
    CY_ASSERT_MSG(
        bValidOutput,
        "Transform_TryInversePoint requires output storage." );
    CY_ASSERT_MSG(
        bValidThreshold,
        "Transform_TryInversePoint requires a finite nonnegative scale threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pLocalPoint = CY_VEC3_ZERO;
    if ( !bValidThreshold || !Transform_IsFinite( transform ) ||
         Scalar_Abs( transform.scale.x ) <= minimumAbsScale ||
         Scalar_Abs( transform.scale.y ) <= minimumAbsScale ||
         Scalar_Abs( transform.scale.z ) <= minimumAbsScale ) {
        return false;
    }

    const vec3_t unrotated = Quat_InverseRotateVectorUnit(
        transform.rotation,
        Vec3_Subtract( point, transform.position ) );
    *pLocalPoint = Vec3_DivideComponents( unrotated, transform.scale );
    return Vec3_IsFinite( *pLocalPoint );
}

bool_t Transform_TryInverseDirection(
    transform_t transform,
    vec3_t direction,
    f32 minimumAbsScale,
    vec3_t *pLocalDirection ) noexcept
{
    const bool_t bValidOutput = pLocalDirection != nullptr;
    const bool_t bValidThreshold = Scalar_IsFinite( minimumAbsScale ) &&
                                   minimumAbsScale >= 0.0f;
    CY_ASSERT_MSG(
        bValidOutput,
        "Transform_TryInverseDirection requires output storage." );
    CY_ASSERT_MSG(
        bValidThreshold,
        "Transform_TryInverseDirection requires a finite nonnegative scale threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pLocalDirection = CY_VEC3_ZERO;
    if ( !bValidThreshold || !Transform_IsFinite( transform ) ||
         Scalar_Abs( transform.scale.x ) <= minimumAbsScale ||
         Scalar_Abs( transform.scale.y ) <= minimumAbsScale ||
         Scalar_Abs( transform.scale.z ) <= minimumAbsScale ) {
        return false;
    }

    const vec3_t unrotated =
        Quat_InverseRotateVectorUnit( transform.rotation, direction );
    *pLocalDirection = Vec3_DivideComponents( unrotated, transform.scale );
    return Vec3_IsFinite( *pLocalDirection );
}

affine3_t Transform_ToAffine3( transform_t value ) noexcept
{
    return Affine3_FromTRS( value.position, value.rotation, value.scale );
}

mat4_t Transform_ToMat4( transform_t value ) noexcept
{
    return Affine3_ToMat4( Transform_ToAffine3( value ) );
}

affine3_t Transform_ComposeAffine(
    transform_t parent,
    transform_t local ) noexcept
{
    return Affine3_Multiply(
        Transform_ToAffine3( parent ),
        Transform_ToAffine3( local ) );
}

bool_t Transform_TryInverseAffine(
    transform_t value,
    f32 minimumAbsDeterminant,
    affine3_t *pInverse ) noexcept
{
    return Affine3_TryInverse(
        Transform_ToAffine3( value ), minimumAbsDeterminant, pInverse );
}

bool_t Transform_TryFromAffine3(
    affine3_t value,
    f32 minimumAbsScale,
    f32 orthogonalityTolerance,
    transform_t *pTransform ) noexcept
{
    const bool_t bValidOutput = pTransform != nullptr;
    const bool_t bValidThresholds =
        Scalar_IsFinite( minimumAbsScale ) && minimumAbsScale >= 0.0f &&
        Scalar_IsFinite( orthogonalityTolerance ) &&
        orthogonalityTolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidOutput,
        "Transform_TryFromAffine3 requires output storage." );
    CY_ASSERT_MSG(
        bValidThresholds,
        "Transform_TryFromAffine3 requires finite nonnegative thresholds." );
    if ( !bValidOutput ) {
        return false;
    }
    *pTransform = CY_TRANSFORM_IDENTITY;
    if ( !bValidThresholds || !Affine3_IsFinite( value ) ) {
        return false;
    }

    vec3_t basis[3]{};
    vec3_t scale{};
    const vec3_t columns[3]{
        Affine3_Column( value, 0u ),
        Affine3_Column( value, 1u ),
        Affine3_Column( value, 2u )
    };
    f32 *const pScale[3]{ &scale.x, &scale.y, &scale.z };
    for ( u32 i = 0u; i < 3u; ++i ) {
        if ( !Vec3_TryNormalize(
                 columns[i], minimumAbsScale, &basis[i], pScale[i] ) ) {
            return false;
        }
    }

    if ( Scalar_Abs( Vec3_Dot( basis[0], basis[1] ) ) > orthogonalityTolerance ||
         Scalar_Abs( Vec3_Dot( basis[0], basis[2] ) ) > orthogonalityTolerance ||
         Scalar_Abs( Vec3_Dot( basis[1], basis[2] ) ) > orthogonalityTolerance ) {
        return false;
    }

    const f32 orientation = Vec3_Dot( basis[0], Vec3_Cross( basis[1], basis[2] ) );
    if ( orientation < 0.0f ) {
        u32 reflectionAxis = 0u;
        if ( scale.y > scale.x ) {
            reflectionAxis = 1u;
        }
        if ( scale.z > ( reflectionAxis == 0u ? scale.x : scale.y ) ) {
            reflectionAxis = 2u;
        }
        basis[reflectionAxis] = Vec3_Negate( basis[reflectionAxis] );
        *pScale[reflectionAxis] = -*pScale[reflectionAxis];
    }

    quat_t rotation{};
    if ( !Mat3_TryToQuaternion(
             Mat3_FromColumns( basis[0], basis[1], basis[2] ),
             0.0f, &rotation ) ) {
        return false;
    }
    *pTransform = Transform_Make(
        Affine3_Translation( value ), rotation, scale );
    return true;
}

transform_t Transform_Interpolate(
    transform_t a,
    transform_t b,
    f32 t ) noexcept
{
    return Transform_Make(
        Vec3_Lerp( a.position, b.position, t ),
        Quat_Slerp( a.rotation, b.rotation, t ),
        Vec3_Lerp( a.scale, b.scale, t ) );
}

transform_t Transform_InterpolateClamped(
    transform_t a,
    transform_t b,
    f32 t ) noexcept
{
    return Transform_Interpolate( a, b, Scalar_Saturate( t ) );
}

} // namespace cypher::math
