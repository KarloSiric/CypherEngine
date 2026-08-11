//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Matrix3.cpp
//  Purpose: Implements checked Matrix3 operations and rotation conversion.
//  Details: Inversion exposes an explicit determinant threshold, and basis repair
//           uses Gram-Schmidt with a right-handed reconstructed third axis.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Matrix3.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

f32 Mat3_Component( mat3_t value, u32 row, u32 column ) noexcept
{
    const bool_t bValidIndex = row < 3u && column < 3u;
    CY_ASSERT_MSG( bValidIndex, "Mat3_Component index is outside the matrix." );
    return bValidIndex ? value.m[Mat3_Index( row, column )] : 0.0f;
}

void Mat3_SetComponent(
    mat3_t *pValue,
    u32 row,
    u32 column,
    f32 component ) noexcept
{
    const bool_t bValidOutput = pValue != nullptr;
    const bool_t bValidIndex = row < 3u && column < 3u;
    CY_ASSERT_MSG( bValidOutput, "Mat3_SetComponent requires matrix storage." );
    CY_ASSERT_MSG( bValidIndex, "Mat3_SetComponent index is outside the matrix." );
    if ( bValidOutput && bValidIndex ) {
        pValue->m[Mat3_Index( row, column )] = component;
    }
}

bool_t Mat3_IsFinite( mat3_t value ) noexcept
{
    for ( f32 component : value.m ) {
        if ( !Scalar_IsFinite( component ) ) {
            return false;
        }
    }
    return true;
}

bool_t Mat3_NearlyEquals(
    mat3_t a,
    mat3_t b,
    f32 absoluteTolerance,
    f32 relativeTolerance ) noexcept
{
    for ( u32 i = 0u; i < 9u; ++i ) {
        if ( !Scalar_NearlyEquals(
                 a.m[i], b.m[i], absoluteTolerance, relativeTolerance ) ) {
            return false;
        }
    }
    return true;
}

bool_t Mat3_TryInverse(
    mat3_t value,
    f32 minimumAbsDeterminant,
    mat3_t *pInverse ) noexcept
{
    const bool_t bValidOutput = pInverse != nullptr;
    const bool_t bValidThreshold = Scalar_IsFinite( minimumAbsDeterminant ) &&
                                   minimumAbsDeterminant >= 0.0f;
    CY_ASSERT_MSG( bValidOutput, "Mat3_TryInverse requires output storage." );
    CY_ASSERT_MSG(
        bValidThreshold,
        "Mat3_TryInverse requires a finite nonnegative determinant threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pInverse = CY_MAT3_IDENTITY;
    if ( !bValidThreshold || !Mat3_IsFinite( value ) ) {
        return false;
    }

    const vec3_t column0 = Mat3_Column( value, 0u );
    const vec3_t column1 = Mat3_Column( value, 1u );
    const vec3_t column2 = Mat3_Column( value, 2u );
    const vec3_t cofactor0 = Vec3_Cross( column1, column2 );
    const f32 determinant = Vec3_Dot( column0, cofactor0 );
    if ( !Scalar_IsFinite( determinant ) ||
         Scalar_Abs( determinant ) <= minimumAbsDeterminant ) {
        return false;
    }

    const f32 inverseDeterminant = 1.0f / determinant;
    const mat3_t inverse = Mat3_FromRows(
        Vec3_Scale( cofactor0, inverseDeterminant ),
        Vec3_Scale( Vec3_Cross( column2, column0 ), inverseDeterminant ),
        Vec3_Scale( Vec3_Cross( column0, column1 ), inverseDeterminant ) );
    if ( !Mat3_IsFinite( inverse ) ) {
        return false;
    }
    *pInverse = inverse;
    return true;
}

mat3_t Mat3_FromQuaternion( quat_t unitRotation ) noexcept
{
    const f32 xx = unitRotation.x * unitRotation.x;
    const f32 yy = unitRotation.y * unitRotation.y;
    const f32 zz = unitRotation.z * unitRotation.z;
    const f32 xy = unitRotation.x * unitRotation.y;
    const f32 xz = unitRotation.x * unitRotation.z;
    const f32 yz = unitRotation.y * unitRotation.z;
    const f32 wx = unitRotation.w * unitRotation.x;
    const f32 wy = unitRotation.w * unitRotation.y;
    const f32 wz = unitRotation.w * unitRotation.z;

    return Mat3_FromColumns(
        Vec3_Make( 1.0f - 2.0f * ( yy + zz ),
                   2.0f * ( xy + wz ),
                   2.0f * ( xz - wy ) ),
        Vec3_Make( 2.0f * ( xy - wz ),
                   1.0f - 2.0f * ( xx + zz ),
                   2.0f * ( yz + wx ) ),
        Vec3_Make( 2.0f * ( xz + wy ),
                   2.0f * ( yz - wx ),
                   1.0f - 2.0f * ( xx + yy ) ) );
}

bool_t Mat3_TryToQuaternion(
    mat3_t rotation,
    f32 minimumColumnLength,
    quat_t *pRotation ) noexcept
{
    const bool_t bValidOutput = pRotation != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Mat3_TryToQuaternion requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pRotation = CY_QUAT_IDENTITY;

    mat3_t orthonormal{};
    if ( !Mat3_TryOrthonormalize(
             rotation, minimumColumnLength, &orthonormal ) ) {
        return false;
    }
    return Quat_TryLookRotation(
        Mat3_Column( orthonormal, 0u ),
        Mat3_Column( orthonormal, 2u ),
        minimumColumnLength,
        pRotation );
}

bool_t Mat3_IsOrthonormal( mat3_t value, f32 tolerance ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( tolerance ) && tolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Mat3_IsOrthonormal requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Mat3_IsFinite( value ) ) {
        return false;
    }

    const vec3_t x = Mat3_Column( value, 0u );
    const vec3_t y = Mat3_Column( value, 1u );
    const vec3_t z = Mat3_Column( value, 2u );
    return Vec3_IsUnitLength( x, tolerance ) &&
           Vec3_IsUnitLength( y, tolerance ) &&
           Vec3_IsUnitLength( z, tolerance ) &&
           Scalar_Abs( Vec3_Dot( x, y ) ) <= tolerance &&
           Scalar_Abs( Vec3_Dot( x, z ) ) <= tolerance &&
           Scalar_Abs( Vec3_Dot( y, z ) ) <= tolerance &&
           Scalar_NearlyEquals( Mat3_Determinant( value ), 1.0f, tolerance, 0.0f );
}

bool_t Mat3_TryOrthonormalize(
    mat3_t value,
    f32 minimumColumnLength,
    mat3_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Mat3_TryOrthonormalize requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_MAT3_IDENTITY;

    vec3_t x{};
    if ( !Vec3_TryNormalize(
             Mat3_Column( value, 0u ), minimumColumnLength, &x, nullptr ) ) {
        return false;
    }

    const vec3_t sourceY = Mat3_Column( value, 1u );
    const vec3_t rejectedY = Vec3_RejectFromUnit( sourceY, x );
    vec3_t y{};
    if ( !Vec3_TryNormalize( rejectedY, minimumColumnLength, &y, nullptr ) ) {
        return false;
    }

    vec3_t z{};
    if ( !Vec3_TryNormalize(
             Vec3_Cross( x, y ), minimumColumnLength, &z, nullptr ) ) {
        return false;
    }
    *pResult = Mat3_FromColumns( x, y, z );
    return true;
}

} // namespace cypher::math
