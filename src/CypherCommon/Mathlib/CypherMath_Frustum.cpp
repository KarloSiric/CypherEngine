//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Frustum.cpp
//  Purpose: Implements frustum plane extraction and corner reconstruction.
//  Details: Extraction uses rows of the combined view-projection matrix and
//           handles negative-one and zero-to-one near-clip conventions explicitly.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Frustum.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

namespace
{

constexpr u32 FrustumPlaneIndex( frustum_plane_t which ) noexcept
{
    return static_cast<u32>( which );
}

bool_t IsValidDepthRange( clip_depth_range_t depthRange ) noexcept
{
    return depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE ||
           depthRange == clip_depth_range_t::ZERO_TO_ONE;
}

} // namespace

bool_t Frustum_IsFinite( frustum_t frustum ) noexcept
{
    for ( plane_t plane : frustum.planes ) {
        if ( !Plane_IsFinite( plane ) ) {
            return false;
        }
    }
    return true;
}

plane_t Frustum_Plane( frustum_t frustum, frustum_plane_t which ) noexcept
{
    const u32 index = FrustumPlaneIndex( which );
    const bool_t bValidIndex = index < CY_FRUSTUM_PLANE_COUNT;
    CY_ASSERT_MSG( bValidIndex, "Frustum_Plane index is outside the frustum." );
    return bValidIndex ? frustum.planes[index] : CY_PLANE_Z;
}

bool_t Frustum_TryFromViewProjection(
    mat4_t viewProjection,
    clip_depth_range_t depthRange,
    f32 minimumPlaneNormalLength,
    frustum_t *pFrustum ) noexcept
{
    const bool_t bValidOutput = pFrustum != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "Frustum_TryFromViewProjection requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pFrustum = {};
    if ( !Mat4_IsFinite( viewProjection ) || !IsValidDepthRange( depthRange ) ) {
        return false;
    }

    const vec4_t row0 = Mat4_Row( viewProjection, 0u );
    const vec4_t row1 = Mat4_Row( viewProjection, 1u );
    const vec4_t row2 = Mat4_Row( viewProjection, 2u );
    const vec4_t row3 = Mat4_Row( viewProjection, 3u );
    const vec4_t coefficients[CY_FRUSTUM_PLANE_COUNT]{
        Vec4_Add( row3, row0 ),
        Vec4_Subtract( row3, row0 ),
        Vec4_Add( row3, row1 ),
        Vec4_Subtract( row3, row1 ),
        depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE
            ? Vec4_Add( row3, row2 )
            : row2,
        Vec4_Subtract( row3, row2 )
    };

    frustum_t result{};
    for ( u32 i = 0u; i < CY_FRUSTUM_PLANE_COUNT; ++i ) {
        const plane_t raw = Plane_Make(
            Vec4_XYZ( coefficients[i] ), coefficients[i].w );
        if ( !Plane_TryNormalize(
                 raw, minimumPlaneNormalLength, &result.planes[i] ) ) {
            return false;
        }
    }
    *pFrustum = result;
    return true;
}

bool_t Frustum_TryCorners(
    mat4_t viewProjection,
    clip_depth_range_t depthRange,
    f32 minimumAbsInversePivot,
    f32 minimumAbsW,
    vec3_t *pCorners ) noexcept
{
    const bool_t bValidOutput = pCorners != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Frustum_TryCorners requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    for ( u32 i = 0u; i < CY_FRUSTUM_CORNER_COUNT; ++i ) {
        pCorners[i] = CY_VEC3_ZERO;
    }
    if ( !IsValidDepthRange( depthRange ) ) {
        return false;
    }

    mat4_t inverse{};
    if ( !Mat4_TryInverse(
             viewProjection, minimumAbsInversePivot, &inverse ) ) {
        return false;
    }

    const f32 nearZ =
        depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE ? -1.0f : 0.0f;
    for ( u32 i = 0u; i < CY_FRUSTUM_CORNER_COUNT; ++i ) {
        const f32 x = ( i & 1u ) != 0u ? 1.0f : -1.0f;
        const f32 y = ( i & 2u ) != 0u ? 1.0f : -1.0f;
        const f32 z = ( i & 4u ) != 0u ? 1.0f : nearZ;
        const vec4_t worldHomogeneous = Mat4_TransformVector4(
            inverse, Vec4_Make( x, y, z, 1.0f ) );
        if ( !Vec4_TryPerspectiveDivide(
                 worldHomogeneous, minimumAbsW, &pCorners[i] ) ) {
            for ( u32 reset = 0u; reset < CY_FRUSTUM_CORNER_COUNT; ++reset ) {
                pCorners[reset] = CY_VEC3_ZERO;
            }
            return false;
        }
    }
    return true;
}

bool_t Frustum_TryTransform(
    frustum_t frustum,
    affine3_t transform,
    f32 minimumAbsDeterminant,
    f32 minimumPlaneNormalLength,
    frustum_t *pTransformed ) noexcept
{
    const bool_t bValidOutput = pTransformed != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Frustum_TryTransform requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pTransformed = {};

    frustum_t result{};
    for ( u32 i = 0u; i < CY_FRUSTUM_PLANE_COUNT; ++i ) {
        if ( !Plane_TryTransform(
                 frustum.planes[i], transform,
                 minimumAbsDeterminant, minimumPlaneNormalLength,
                 &result.planes[i] ) ) {
            return false;
        }
    }
    *pTransformed = result;
    return true;
}

} // namespace cypher::math
