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

//==========================================================================
// Binary64 authoring affine transform
//==========================================================================

f64 Affine3d_Component( affine3d_t value, u32 row, u32 column ) noexcept
{
    const bool_t bValidIndex = row < 3u && column < 4u;
    CY_ASSERT_MSG( bValidIndex, "Affine3d_Component index is outside the transform." );
    return bValidIndex ? value.m[Affine3_Index( row, column )] : 0.0;
}

void Affine3d_SetComponent(
    affine3d_t *pValue,
    u32 row,
    u32 column,
    f64 component ) noexcept
{
    const bool_t bValidOutput = pValue != nullptr;
    const bool_t bValidIndex = row < 3u && column < 4u;
    CY_ASSERT_MSG( bValidOutput, "Affine3d_SetComponent requires transform storage." );
    CY_ASSERT_MSG( bValidIndex, "Affine3d_SetComponent index is outside the transform." );
    if ( bValidOutput && bValidIndex ) {
        pValue->m[Affine3_Index( row, column )] = component;
    }
}

bool_t Affine3d_IsFinite( affine3d_t value ) noexcept
{
    for ( f64 component : value.m ) {
        if ( !Scalar_IsFinite( component ) ) {
            return false;
        }
    }
    return true;
}

bool_t Affine3d_NearlyEquals(
    affine3d_t a,
    affine3d_t b,
    f64 absoluteTolerance,
    f64 relativeTolerance ) noexcept
{
    for ( u32 i = 0u; i < 12u; ++i ) {
        if ( !Scalar_NearlyEquals(
                 a.m[i], b.m[i], absoluteTolerance, relativeTolerance ) ) {
            return false;
        }
    }
    return true;
}

bool_t Affine3d_TryToAffine3( affine3d_t value, affine3_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Affine3d_TryToAffine3 requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_AFFINE3_IDENTITY;

    vec3_t narrowedColumn0{};
    vec3_t narrowedColumn1{};
    vec3_t narrowedColumn2{};
    vec3_t narrowedTranslation{};
    if ( !Vec3d_TryToVec3( Affine3d_Column( value, 0u ), &narrowedColumn0 ) ||
         !Vec3d_TryToVec3( Affine3d_Column( value, 1u ), &narrowedColumn1 ) ||
         !Vec3d_TryToVec3( Affine3d_Column( value, 2u ), &narrowedColumn2 ) ||
         !Vec3d_TryToVec3( Affine3d_Translation( value ), &narrowedTranslation ) ) {
        return false;
    }
    *pResult = Affine3_FromColumns(
        narrowedColumn0, narrowedColumn1, narrowedColumn2, narrowedTranslation );
    return true;
}

// Normals use the inverse transpose of the linear block, computed directly from
// column cross products so no mat3d_t type is required.
bool_t Affine3d_TryTransformNormal(
    affine3d_t transform,
    vec3d_t normal,
    f64 minimumAbsDeterminant,
    vec3d_t *pTransformed ) noexcept
{
    const bool_t bValidOutput = pTransformed != nullptr;
    CY_ASSERT_MSG(
        bValidOutput, "Affine3d_TryTransformNormal requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pTransformed = CY_VEC3D_ZERO;

    const vec3d_t c0 = Affine3d_Column( transform, 0u );
    const vec3d_t c1 = Affine3d_Column( transform, 1u );
    const vec3d_t c2 = Affine3d_Column( transform, 2u );

    const vec3d_t row0 = Vec3d_Cross( c1, c2 );
    const f64 determinant = Vec3d_Dot( c0, row0 );
    if ( !Scalar_IsFinite( determinant ) ||
         Scalar_Abs( determinant ) <= minimumAbsDeterminant ) {
        return false;
    }

    const f64 inverseDeterminant = 1.0 / determinant;
    // The inverse-transpose's columns are exactly the inverse's rows.
    const vec3d_t invRow0 = Vec3d_Scale( row0, inverseDeterminant );
    const vec3d_t invRow1 = Vec3d_Scale( Vec3d_Cross( c2, c0 ), inverseDeterminant );
    const vec3d_t invRow2 = Vec3d_Scale( Vec3d_Cross( c0, c1 ), inverseDeterminant );
    const affine3d_t inverseTransposeLinear = Affine3d_FromColumns(
        invRow0, invRow1, invRow2, CY_VEC3D_ZERO );

    const vec3d_t transformed = Affine3d_TransformDirection( inverseTransposeLinear, normal );
    if ( !Vec3d_IsFinite( transformed ) ) {
        return false;
    }
    *pTransformed = transformed;
    return true;
}

bool_t Affine3d_TryInverse(
    affine3d_t value,
    f64 minimumAbsDeterminant,
    affine3d_t *pInverse ) noexcept
{
    const bool_t bValidOutput = pInverse != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Affine3d_TryInverse requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pInverse = CY_AFFINE3D_IDENTITY;

    const vec3d_t c0 = Affine3d_Column( value, 0u );
    const vec3d_t c1 = Affine3d_Column( value, 1u );
    const vec3d_t c2 = Affine3d_Column( value, 2u );

    // The rows of the inverse are cross products of the other two columns
    // scaled by 1/determinant -- the standard cofactor-via-cross-product
    // identity for a 3x3 matrix. This avoids needing a mat3d_t type at all.
    const vec3d_t row0 = Vec3d_Cross( c1, c2 );
    const f64 determinant = Vec3d_Dot( c0, row0 );
    if ( !Scalar_IsFinite( determinant ) ||
         Scalar_Abs( determinant ) <= minimumAbsDeterminant ) {
        return false;
    }

    const f64 inverseDeterminant = 1.0 / determinant;
    const vec3d_t invRow0 = Vec3d_Scale( row0, inverseDeterminant );
    const vec3d_t invRow1 = Vec3d_Scale( Vec3d_Cross( c2, c0 ), inverseDeterminant );
    const vec3d_t invRow2 = Vec3d_Scale( Vec3d_Cross( c0, c1 ), inverseDeterminant );

    // The inverse's columns are the transpose of the rows just computed.
    const vec3d_t invColumn0 = Vec3d_Make( invRow0.x, invRow1.x, invRow2.x );
    const vec3d_t invColumn1 = Vec3d_Make( invRow0.y, invRow1.y, invRow2.y );
    const vec3d_t invColumn2 = Vec3d_Make( invRow0.z, invRow1.z, invRow2.z );

    const affine3d_t inverseLinear = Affine3d_FromColumns(
        invColumn0, invColumn1, invColumn2, CY_VEC3D_ZERO );
    // For x' = Lx + t, the inverse translation is -(L^-1)t.
    const vec3d_t inverseTranslation = Vec3d_Negate(
        Affine3d_TransformDirection( inverseLinear, Affine3d_Translation( value ) ) );

    const affine3d_t inverse = Affine3d_FromColumns(
        invColumn0, invColumn1, invColumn2, inverseTranslation );
    if ( !Affine3d_IsFinite( inverse ) ) {
        return false;
    }
    *pInverse = inverse;
    return true;
}

} // namespace cypher::math
