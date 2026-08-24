//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Affine3.cpp
//  Purpose: Implements checked affine transform operations.
//  Details: Inversion operates on the linear block and derives inverse translation,
//           preserving exact affine composition without forcing a TRS decomposition.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Affine3.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

//==========================================================================
// Component access
//==========================================================================

f32 Affine3_Component( affine3_t value, u32 row, u32 column ) noexcept
{
    const bool_t bValidIndex = row < 3u && column < 4u;
    CY_ASSERT_MSG( bValidIndex, "Affine3_Component index is outside the transform." );
    return bValidIndex ? value.m[Affine3_Index( row, column )] : 0.0f;
}

void Affine3_SetComponent(
    affine3_t *pValue,
    u32 row,
    u32 column,
    f32 component ) noexcept
{
    const bool_t bValidOutput = pValue != nullptr;
    const bool_t bValidIndex = row < 3u && column < 4u;
    CY_ASSERT_MSG( bValidOutput, "Affine3_SetComponent requires transform storage." );
    CY_ASSERT_MSG( bValidIndex, "Affine3_SetComponent index is outside the transform." );
    if ( bValidOutput && bValidIndex ) {
        pValue->m[Affine3_Index( row, column )] = component;
    }
}

bool_t Affine3_IsFinite( affine3_t value ) noexcept
{
    for ( f32 component : value.m ) {
        if ( !Scalar_IsFinite( component ) ) {
            return false;
        }
    }
    return true;
}

bool_t Affine3_NearlyEquals(
    affine3_t a,
    affine3_t b,
    f32 absoluteTolerance,
    f32 relativeTolerance ) noexcept
{
    for ( u32 i = 0u; i < 12u; ++i ) {
        if ( !Scalar_NearlyEquals(
                 a.m[i], b.m[i], absoluteTolerance, relativeTolerance ) ) {
            return false;
        }
    }
    return true;
}

// Normals use the inverse transpose of the linear block. Applying the affine
// matrix directly would produce incorrect results under non-uniform scale.
bool_t Affine3_TryTransformNormal(
    affine3_t transform,
    vec3_t normal,
    f32 minimumAbsDeterminant,
    vec3_t *pTransformed ) noexcept
{
    const bool_t bValidOutput = pTransformed != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "Affine3_TryTransformNormal requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pTransformed = CY_VEC3_ZERO;

    mat3_t inverse{};
    if ( !Mat3_TryInverse(
             Affine3_LinearPart( transform ), minimumAbsDeterminant, &inverse ) ) {
        return false;
    }
    const vec3_t transformed =
        Mat3_TransformVector( Mat3_Transpose( inverse ), normal );
    if ( !Vec3_IsFinite( transformed ) ) {
        return false;
    }
    *pTransformed = transformed;
    return true;
}

affine3_t Affine3_FromQuaternion( quat_t unitRotation ) noexcept
{
    const mat3_t rotation = Mat3_FromQuaternion( unitRotation );
    return Affine3_FromColumns(
        Mat3_Column( rotation, 0u ),
        Mat3_Column( rotation, 1u ),
        Mat3_Column( rotation, 2u ),
        CY_VEC3_ZERO );
}

affine3_t Affine3_FromTRS(
    vec3_t translation,
    quat_t unitRotation,
    vec3_t scale ) noexcept
{
    const mat3_t rotation = Mat3_FromQuaternion( unitRotation );
    // Scale the basis columns so translation remains isolated in column three.
    return Affine3_FromColumns(
        Vec3_Scale( Mat3_Column( rotation, 0u ), scale.x ),
        Vec3_Scale( Mat3_Column( rotation, 1u ), scale.y ),
        Vec3_Scale( Mat3_Column( rotation, 2u ), scale.z ),
        translation );
}

bool_t Affine3_TryInverse(
    affine3_t value,
    f32 minimumAbsDeterminant,
    affine3_t *pInverse ) noexcept
{
    const bool_t bValidOutput = pInverse != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Affine3_TryInverse requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pInverse = CY_AFFINE3_IDENTITY;

    mat3_t inverseLinear{};
    if ( !Mat3_TryInverse(
             Affine3_LinearPart( value ), minimumAbsDeterminant,
             &inverseLinear ) ) {
        return false;
    }
    // For x' = Lx + t, the inverse translation is -(L^-1)t.
    const vec3_t inverseTranslation = Vec3_Negate(
        Mat3_TransformVector( inverseLinear, Affine3_Translation( value ) ) );
    const affine3_t inverse = Affine3_FromColumns(
        Mat3_Column( inverseLinear, 0u ),
        Mat3_Column( inverseLinear, 1u ),
        Mat3_Column( inverseLinear, 2u ),
        inverseTranslation );
    if ( !Affine3_IsFinite( inverse ) ) {
        return false;
    }
    *pInverse = inverse;
    return true;
}

bool_t Affine3_TryFromMat4(
    mat4_t value,
    f32 affineTolerance,
    affine3_t *pAffine ) noexcept
{
    const bool_t bValidOutput = pAffine != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Affine3_TryFromMat4 requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pAffine = CY_AFFINE3_IDENTITY;
    // Reject projective matrices; dropping their final row would change meaning.
    if ( !Mat4_IsAffine( value, affineTolerance ) ) {
        return false;
    }
    *pAffine = Affine3_FromColumns(
        Vec4_XYZ( Mat4_Column( value, 0u ) ),
        Vec4_XYZ( Mat4_Column( value, 1u ) ),
        Vec4_XYZ( Mat4_Column( value, 2u ) ),
        Mat4_Translation( value ) );
    return true;
}

} // namespace cypher::math
