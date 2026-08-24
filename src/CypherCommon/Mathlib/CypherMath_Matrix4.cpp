//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Matrix4.cpp
//  Purpose: Implements checked Matrix4, camera, and projection operations.
//  Details: General inversion uses partial-pivot Gauss-Jordan elimination in
//           double precision while projection builders state their clip-depth
//           convention explicitly at every call site.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Matrix4.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

namespace
{

bool_t IsValidDepthRange( clip_depth_range_t depthRange ) noexcept
{
    return depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE ||
           depthRange == clip_depth_range_t::ZERO_TO_ONE;
}

bool_t IsValidPerspectiveInput(
    angle_t verticalFieldOfView,
    f32 aspectRatio,
    f32 nearDistance,
    clip_depth_range_t depthRange ) noexcept
{
    return Angle_IsFinite( verticalFieldOfView ) &&
           verticalFieldOfView.radians > 0.0f &&
           verticalFieldOfView.radians < CY_PI_F &&
           Scalar_IsFinite( aspectRatio ) && aspectRatio > 0.0f &&
           Scalar_IsFinite( nearDistance ) && nearDistance > 0.0f &&
           IsValidDepthRange( depthRange );
}

} // namespace

f32 Mat4_Component( mat4_t value, u32 row, u32 column ) noexcept
{
    const bool_t bValidIndex = row < 4u && column < 4u;
    CY_ASSERT_MSG( bValidIndex, "Mat4_Component index is outside the matrix." );
    return bValidIndex ? value.m[Mat4_Index( row, column )] : 0.0f;
}

void Mat4_SetComponent(
    mat4_t *pValue,
    u32 row,
    u32 column,
    f32 component ) noexcept
{
    const bool_t bValidOutput = pValue != nullptr;
    const bool_t bValidIndex = row < 4u && column < 4u;
    CY_ASSERT_MSG( bValidOutput, "Mat4_SetComponent requires matrix storage." );
    CY_ASSERT_MSG( bValidIndex, "Mat4_SetComponent index is outside the matrix." );
    if ( bValidOutput && bValidIndex ) {
        pValue->m[Mat4_Index( row, column )] = component;
    }
}

bool_t Mat4_IsFinite( mat4_t value ) noexcept
{
    for ( f32 component : value.m ) {
        if ( !Scalar_IsFinite( component ) ) {
            return false;
        }
    }
    return true;
}

bool_t Mat4_NearlyEquals(
    mat4_t a,
    mat4_t b,
    f32 absoluteTolerance,
    f32 relativeTolerance ) noexcept
{
    for ( u32 i = 0u; i < 16u; ++i ) {
        if ( !Scalar_NearlyEquals(
                 a.m[i], b.m[i], absoluteTolerance, relativeTolerance ) ) {
            return false;
        }
    }
    return true;
}

bool_t Mat4_TryProjectPoint(
    mat4_t matrix,
    vec3_t point,
    f32 minimumAbsW,
    vec3_t *pProjected ) noexcept
{
    const bool_t bValidOutput = pProjected != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Mat4_TryProjectPoint requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pProjected = CY_VEC3_ZERO;

    if ( !Mat4_IsFinite( matrix ) || !Vec3_IsFinite( point ) ) {
        return false;
    }
    const vec4_t homogeneous =
        Mat4_TransformVector4( matrix, Vec4_FromVec3( point, 1.0f ) );
    return Vec4_TryPerspectiveDivide( homogeneous, minimumAbsW, pProjected );
}

f32 Mat4_Determinant( mat4_t value ) noexcept
{
    if ( !Mat4_IsFinite( value ) ) {
        return 0.0f;
    }

    f64 work[4][4]{};
    for ( u32 row = 0u; row < 4u; ++row ) {
        for ( u32 column = 0u; column < 4u; ++column ) {
            work[row][column] =
                static_cast<f64>( value.m[Mat4_Index( row, column )] );
        }
    }

    // Gaussian elimination with partial pivoting converts the matrix to upper
    // triangular form; the determinant is the signed product of its pivots.
    f64 determinant = 1.0;
    f64 sign = 1.0;
    for ( u32 pivotColumn = 0u; pivotColumn < 4u; ++pivotColumn ) {
        u32 pivotRow = pivotColumn;
        f64 pivotMagnitude = Scalar_Abs( work[pivotRow][pivotColumn] );
        for ( u32 row = pivotColumn + 1u; row < 4u; ++row ) {
            const f64 candidate = Scalar_Abs( work[row][pivotColumn] );
            if ( candidate > pivotMagnitude ) {
                pivotMagnitude = candidate;
                pivotRow = row;
            }
        }
        if ( pivotMagnitude == 0.0 ) {
            return 0.0f;
        }
        if ( pivotRow != pivotColumn ) {
            for ( u32 column = 0u; column < 4u; ++column ) {
                const f64 temporary = work[pivotColumn][column];
                work[pivotColumn][column] = work[pivotRow][column];
                work[pivotRow][column] = temporary;
            }
            sign = -sign;
        }

        const f64 pivot = work[pivotColumn][pivotColumn];
        determinant *= pivot;
        for ( u32 row = pivotColumn + 1u; row < 4u; ++row ) {
            const f64 factor = work[row][pivotColumn] / pivot;
            for ( u32 column = pivotColumn + 1u; column < 4u; ++column ) {
                work[row][column] -= factor * work[pivotColumn][column];
            }
        }
    }
    return static_cast<f32>( determinant * sign );
}

bool_t Mat4_TryInverse(
    mat4_t value,
    f32 minimumAbsPivot,
    mat4_t *pInverse ) noexcept
{
    const bool_t bValidOutput = pInverse != nullptr;
    const bool_t bValidThreshold = Scalar_IsFinite( minimumAbsPivot ) &&
                                   minimumAbsPivot >= 0.0f;
    CY_ASSERT_MSG( bValidOutput, "Mat4_TryInverse requires output storage." );
    CY_ASSERT_MSG(
        bValidThreshold,
        "Mat4_TryInverse requires a finite nonnegative pivot threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pInverse = CY_MAT4_IDENTITY;
    if ( !bValidThreshold || !Mat4_IsFinite( value ) ) {
        return false;
    }

    // Gauss-Jordan elimination transforms [M | I] into [I | inverse(M)].
    // Double work storage reduces error before the final f32 conversion.
    f64 augmented[4][8]{};
    for ( u32 row = 0u; row < 4u; ++row ) {
        for ( u32 column = 0u; column < 4u; ++column ) {
            augmented[row][column] =
                static_cast<f64>( value.m[Mat4_Index( row, column )] );
        }
        augmented[row][4u + row] = 1.0;
    }

    const f64 pivotThreshold = static_cast<f64>( minimumAbsPivot );
    for ( u32 pivotColumn = 0u; pivotColumn < 4u; ++pivotColumn ) {
        u32 pivotRow = pivotColumn;
        f64 pivotMagnitude = Scalar_Abs( augmented[pivotRow][pivotColumn] );
        for ( u32 row = pivotColumn + 1u; row < 4u; ++row ) {
            const f64 candidate = Scalar_Abs( augmented[row][pivotColumn] );
            if ( candidate > pivotMagnitude ) {
                pivotMagnitude = candidate;
                pivotRow = row;
            }
        }
        if ( pivotMagnitude <= pivotThreshold ) {
            return false;
        }
        if ( pivotRow != pivotColumn ) {
            for ( u32 column = 0u; column < 8u; ++column ) {
                const f64 temporary = augmented[pivotColumn][column];
                augmented[pivotColumn][column] = augmented[pivotRow][column];
                augmented[pivotRow][column] = temporary;
            }
        }

        const f64 inversePivot = 1.0 / augmented[pivotColumn][pivotColumn];
        for ( u32 column = 0u; column < 8u; ++column ) {
            augmented[pivotColumn][column] *= inversePivot;
        }
        for ( u32 row = 0u; row < 4u; ++row ) {
            if ( row == pivotColumn ) {
                continue;
            }
            const f64 factor = augmented[row][pivotColumn];
            for ( u32 column = 0u; column < 8u; ++column ) {
                augmented[row][column] -= factor * augmented[pivotColumn][column];
            }
        }
    }

    mat4_t inverse{};
    for ( u32 row = 0u; row < 4u; ++row ) {
        for ( u32 column = 0u; column < 4u; ++column ) {
            inverse.m[Mat4_Index( row, column )] =
                static_cast<f32>( augmented[row][4u + column] );
        }
    }
    if ( !Mat4_IsFinite( inverse ) ) {
        return false;
    }
    *pInverse = inverse;
    return true;
}

bool_t Mat4_IsAffine( mat4_t value, f32 tolerance ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( tolerance ) && tolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Mat4_IsAffine requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Mat4_IsFinite( value ) ) {
        return false;
    }
    return Scalar_Abs( value.m[Mat4_Index( 3u, 0u )] ) <= tolerance &&
           Scalar_Abs( value.m[Mat4_Index( 3u, 1u )] ) <= tolerance &&
           Scalar_Abs( value.m[Mat4_Index( 3u, 2u )] ) <= tolerance &&
           Scalar_NearlyEquals(
               value.m[Mat4_Index( 3u, 3u )], 1.0f, tolerance, 0.0f );
}

mat4_t Mat4_FromQuaternion( quat_t unitRotation ) noexcept
{
    const mat3_t linear = Mat3_FromQuaternion( unitRotation );
    return Mat4_FromColumns(
        Vec4_FromVec3( Mat3_Column( linear, 0u ), 0.0f ),
        Vec4_FromVec3( Mat3_Column( linear, 1u ), 0.0f ),
        Vec4_FromVec3( Mat3_Column( linear, 2u ), 0.0f ),
        CY_VEC4_W );
}

mat4_t Mat4_FromTRS(
    vec3_t translation,
    quat_t unitRotation,
    vec3_t scale ) noexcept
{
    const mat3_t rotation = Mat3_FromQuaternion( unitRotation );

    // Column-vector convention: scale each rotation basis column and place
    // translation in column three, producing T * R * S.
    return Mat4_FromColumns(
        Vec4_FromVec3( Vec3_Scale( Mat3_Column( rotation, 0u ), scale.x ), 0.0f ),
        Vec4_FromVec3( Vec3_Scale( Mat3_Column( rotation, 1u ), scale.y ), 0.0f ),
        Vec4_FromVec3( Vec3_Scale( Mat3_Column( rotation, 2u ), scale.z ), 0.0f ),
        Vec4_FromVec3( translation, 1.0f ) );
}

bool_t Mat4_TryLookAtRH(
    vec3_t eye,
    vec3_t target,
    vec3_t upHint,
    f32 minimumDirectionLength,
    mat4_t *pView ) noexcept
{
    const bool_t bValidOutput = pView != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Mat4_TryLookAtRH requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pView = CY_MAT4_IDENTITY;

    vec3_t forward{};
    if ( !Vec3_TryNormalize(
             Vec3_Subtract( target, eye ), minimumDirectionLength,
             &forward, nullptr ) ) {
        return false;
    }
    vec3_t right{};
    if ( !Vec3_TryNormalize(
             Vec3_Cross( forward, upHint ), minimumDirectionLength,
             &right, nullptr ) ) {
        return false;
    }
    const vec3_t cameraUp = Vec3_Cross( right, forward );

    // A right-handed view looks down local -Z, hence the negated forward row.
    *pView = Mat4_FromRows(
        Vec4_Make( right.x, right.y, right.z, -Vec3_Dot( right, eye ) ),
        Vec4_Make(
            cameraUp.x, cameraUp.y, cameraUp.z,
            -Vec3_Dot( cameraUp, eye ) ),
        Vec4_Make(
            -forward.x, -forward.y, -forward.z,
            Vec3_Dot( forward, eye ) ),
        CY_VEC4_W );
    return Mat4_IsFinite( *pView );
}

bool_t Mat4_TryPerspectiveRH(
    angle_t verticalFieldOfView,
    f32 aspectRatio,
    f32 nearDistance,
    f32 farDistance,
    clip_depth_range_t depthRange,
    mat4_t *pProjection ) noexcept
{
    const bool_t bValidOutput = pProjection != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Mat4_TryPerspectiveRH requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pProjection = CY_MAT4_IDENTITY;

    const bool_t bValidInput = IsValidPerspectiveInput(
                                   verticalFieldOfView, aspectRatio,
                                   nearDistance, depthRange ) &&
                               Scalar_IsFinite( farDistance ) &&
                               farDistance > nearDistance;
    if ( !bValidInput ) {
        return false;
    }

    const f32 focalLength =
        1.0f / Scalar_Tan( verticalFieldOfView.radians * 0.5f );
    mat4_t result = CY_MAT4_ZERO;
    result.m[Mat4_Index( 0u, 0u )] = focalLength / aspectRatio;
    result.m[Mat4_Index( 1u, 1u )] = focalLength;
    result.m[Mat4_Index( 3u, 2u )] = -1.0f;

    // X/Y projection is shared; only Z mapping differs between OpenGL-style
    // [-1, 1] depth and zero-to-one depth APIs.
    if ( depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE ) {
        result.m[Mat4_Index( 2u, 2u )] =
            ( farDistance + nearDistance ) / ( nearDistance - farDistance );
        result.m[Mat4_Index( 2u, 3u )] =
            ( 2.0f * farDistance * nearDistance ) /
            ( nearDistance - farDistance );
    } else {
        result.m[Mat4_Index( 2u, 2u )] =
            farDistance / ( nearDistance - farDistance );
        result.m[Mat4_Index( 2u, 3u )] =
            ( farDistance * nearDistance ) / ( nearDistance - farDistance );
    }
    if ( !Mat4_IsFinite( result ) ) {
        return false;
    }
    *pProjection = result;
    return true;
}

bool_t Mat4_TryPerspectiveInfiniteRH(
    angle_t verticalFieldOfView,
    f32 aspectRatio,
    f32 nearDistance,
    clip_depth_range_t depthRange,
    mat4_t *pProjection ) noexcept
{
    const bool_t bValidOutput = pProjection != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "Mat4_TryPerspectiveInfiniteRH requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pProjection = CY_MAT4_IDENTITY;
    if ( !IsValidPerspectiveInput(
             verticalFieldOfView, aspectRatio, nearDistance, depthRange ) ) {
        return false;
    }

    const f32 focalLength =
        1.0f / Scalar_Tan( verticalFieldOfView.radians * 0.5f );
    mat4_t result = CY_MAT4_ZERO;
    result.m[Mat4_Index( 0u, 0u )] = focalLength / aspectRatio;
    result.m[Mat4_Index( 1u, 1u )] = focalLength;
    result.m[Mat4_Index( 2u, 2u )] = -1.0f;
    result.m[Mat4_Index( 2u, 3u )] =
        depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE
            ? -2.0f * nearDistance
            : -nearDistance;
    result.m[Mat4_Index( 3u, 2u )] = -1.0f;
    if ( !Mat4_IsFinite( result ) ) {
        return false;
    }
    *pProjection = result;
    return true;
}

bool_t Mat4_TryOrthographicRH(
    f32 left,
    f32 right,
    f32 bottom,
    f32 top,
    f32 nearDistance,
    f32 farDistance,
    clip_depth_range_t depthRange,
    mat4_t *pProjection ) noexcept
{
    const bool_t bValidOutput = pProjection != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Mat4_TryOrthographicRH requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pProjection = CY_MAT4_IDENTITY;

    const bool_t bValidInput =
        Scalar_IsFinite( left ) && Scalar_IsFinite( right ) &&
        Scalar_IsFinite( bottom ) && Scalar_IsFinite( top ) &&
        Scalar_IsFinite( nearDistance ) && Scalar_IsFinite( farDistance ) &&
        right > left && top > bottom && nearDistance >= 0.0f &&
        farDistance > nearDistance && IsValidDepthRange( depthRange );
    if ( !bValidInput ) {
        return false;
    }

    const f32 inverseWidth = 1.0f / ( right - left );
    const f32 inverseHeight = 1.0f / ( top - bottom );
    const f32 inverseDepth = 1.0f / ( farDistance - nearDistance );
    mat4_t result = CY_MAT4_IDENTITY;
    result.m[Mat4_Index( 0u, 0u )] = 2.0f * inverseWidth;
    result.m[Mat4_Index( 1u, 1u )] = 2.0f * inverseHeight;
    result.m[Mat4_Index( 0u, 3u )] = -( right + left ) * inverseWidth;
    result.m[Mat4_Index( 1u, 3u )] = -( top + bottom ) * inverseHeight;
    if ( depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE ) {
        result.m[Mat4_Index( 2u, 2u )] = -2.0f * inverseDepth;
        result.m[Mat4_Index( 2u, 3u )] =
            -( farDistance + nearDistance ) * inverseDepth;
    } else {
        result.m[Mat4_Index( 2u, 2u )] = -inverseDepth;
        result.m[Mat4_Index( 2u, 3u )] = -nearDistance * inverseDepth;
    }
    *pProjection = result;
    return true;
}

} // namespace cypher::math
