//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Viewport.cpp
//  Purpose: Implements world, clip, and editor viewport coordinate conversion.
//  Details: Projection preserves clip W for visibility checks; unprojection and
//           picking accept a precomputed inverse to avoid per-query inversion.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Viewport.h"

#include "CypherCommon_Assert.h"

#include <cmath>

namespace cypher::math
{

namespace
{

bool_t ViewportPoliciesValid(
    viewport_origin_t origin,
    clip_depth_range_t depthRange ) noexcept
{
    return origin < viewport_origin_t::COUNT &&
           depthRange < clip_depth_range_t::COUNT;
}

f32 NdcToNormalizedDepth( f32 ndcDepth, clip_depth_range_t depthRange ) noexcept
{
    return depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE
        ? ndcDepth * 0.5f + 0.5f
        : ndcDepth;
}

f32 NormalizedToNdcDepth(
    f32 normalizedDepth,
    clip_depth_range_t depthRange ) noexcept
{
    return depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE
        ? normalizedDepth * 2.0f - 1.0f
        : normalizedDepth;
}

} // namespace

bool_t Viewport_IsValid( viewport_rect_t viewport ) noexcept
{
    return Scalar_IsFinite( viewport.x ) && Scalar_IsFinite( viewport.y ) &&
           Scalar_IsFinite( viewport.width ) && Scalar_IsFinite( viewport.height ) &&
           viewport.width > 0.0f && viewport.height > 0.0f;
}

bool_t Viewport_TryProjectPoint(
    mat4_t worldToClip,
    viewport_rect_t viewport,
    viewport_origin_t origin,
    clip_depth_range_t depthRange,
    vec3_t worldPoint,
    f32 minimumAbsW,
    viewport_projection_t *pProjection ) noexcept
{
    const bool_t bValidOutput = pProjection != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Viewport_TryProjectPoint requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pProjection = {};
    if ( !Mat4_IsFinite( worldToClip ) || !Viewport_IsValid( viewport ) ||
         !ViewportPoliciesValid( origin, depthRange ) ||
         !Vec3_IsFinite( worldPoint ) || minimumAbsW < 0.0f ||
         !Scalar_IsFinite( minimumAbsW ) ) {
        return false;
    }

    const vec4_t clip = Mat4_TransformVector4(
        worldToClip, Vec4_FromVec3( worldPoint, 1.0f ) );
    if ( !Vec4_IsFinite( clip ) || std::abs( clip.w ) <= minimumAbsW ) {
        return false;
    }
    const f32 inverseW = 1.0f / clip.w;

    // Perspective division moves homogeneous clip coordinates into NDC. Keep
    // clip W separately because its sign distinguishes points behind the camera.
    const vec3_t ndc = Vec3_Make(
        clip.x * inverseW, clip.y * inverseW, clip.z * inverseW );
    const f32 normalizedX = ndc.x * 0.5f + 0.5f;
    const f32 normalizedY = ndc.y * 0.5f + 0.5f;
    const f32 screenY = origin == viewport_origin_t::TOP_LEFT
        ? viewport.y + ( 1.0f - normalizedY ) * viewport.height
        : viewport.y + normalizedY * viewport.height;
    const f32 minimumDepth = depthRange == clip_depth_range_t::NEGATIVE_ONE_TO_ONE
        ? -1.0f
        : 0.0f;
    *pProjection = {
        Vec3_Make(
            viewport.x + normalizedX * viewport.width,
            screenY,
            NdcToNormalizedDepth( ndc.z, depthRange ) ),
        clip.w,
        clip.w > minimumAbsW &&
            ndc.x >= -1.0f && ndc.x <= 1.0f &&
            ndc.y >= -1.0f && ndc.y <= 1.0f &&
            ndc.z >= minimumDepth && ndc.z <= 1.0f
    };
    return Vec3_IsFinite( pProjection->screen );
}

bool_t Viewport_TryUnprojectPoint(
    mat4_t clipToWorld,
    viewport_rect_t viewport,
    viewport_origin_t origin,
    clip_depth_range_t depthRange,
    vec3_t screenPoint,
    f32 minimumAbsW,
    vec3_t *pWorldPoint ) noexcept
{
    const bool_t bValidOutput = pWorldPoint != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Viewport_TryUnprojectPoint requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pWorldPoint = CY_VEC3_ZERO;
    if ( !Mat4_IsFinite( clipToWorld ) || !Viewport_IsValid( viewport ) ||
         !ViewportPoliciesValid( origin, depthRange ) ||
         !Vec3_IsFinite( screenPoint ) || screenPoint.z < 0.0f ||
         screenPoint.z > 1.0f || minimumAbsW < 0.0f ||
         !Scalar_IsFinite( minimumAbsW ) ) {
        return false;
    }

    const f32 normalizedX = ( screenPoint.x - viewport.x ) / viewport.width;
    const f32 screenNormalizedY = ( screenPoint.y - viewport.y ) / viewport.height;

    // Window systems usually grow Y downward; graphics NDC grows Y upward.
    const f32 normalizedY = origin == viewport_origin_t::TOP_LEFT
        ? 1.0f - screenNormalizedY
        : screenNormalizedY;
    const vec4_t clip = Vec4_Make(
        normalizedX * 2.0f - 1.0f,
        normalizedY * 2.0f - 1.0f,
        NormalizedToNdcDepth( screenPoint.z, depthRange ),
        1.0f );
    return Vec4_TryPerspectiveDivide(
        Mat4_TransformVector4( clipToWorld, clip ), minimumAbsW, pWorldPoint );
}

bool_t Viewport_TryBuildPickingRay(
    mat4_t clipToWorld,
    viewport_rect_t viewport,
    viewport_origin_t origin,
    clip_depth_range_t depthRange,
    vec2_t screenPoint,
    f32 minimumAbsW,
    f32 minimumDirectionLength,
    ray_t *pRay ) noexcept
{
    const bool_t bValidOutput = pRay != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Viewport_TryBuildPickingRay requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pRay = {};
    if ( !Vec2_IsFinite( screenPoint ) || minimumDirectionLength < 0.0f ||
         !Scalar_IsFinite( minimumDirectionLength ) ) {
        return false;
    }

    vec3_t nearPoint{};
    vec3_t farPoint{};

    // A picking ray is the normalized line through the near and far points
    // produced by the same screen coordinate.
    if ( !Viewport_TryUnprojectPoint(
             clipToWorld, viewport, origin, depthRange,
             Vec3_Make( screenPoint.x, screenPoint.y, 0.0f ),
             minimumAbsW, &nearPoint ) ||
         !Viewport_TryUnprojectPoint(
             clipToWorld, viewport, origin, depthRange,
             Vec3_Make( screenPoint.x, screenPoint.y, 1.0f ),
             minimumAbsW, &farPoint ) ) {
        return false;
    }
    vec3_t direction{};
    if ( !Vec3_TryNormalize(
             Vec3_Subtract( farPoint, nearPoint ), minimumDirectionLength,
             &direction, nullptr ) ) {
        return false;
    }
    *pRay = Ray_Make( nearPoint, direction );
    return true;
}

} // namespace cypher::math
