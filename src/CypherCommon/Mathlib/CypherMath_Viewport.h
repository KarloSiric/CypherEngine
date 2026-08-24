//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Viewport.h
//  Purpose: Declares world, clip, and editor viewport coordinate conversion.
//  Details: Viewport origin and clip-depth conventions are explicit so renderer
//           backends and Qt editor views cannot silently disagree.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_VIEWPORT_H
#define CYPHER_COMMON_MATH_VIEWPORT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Matrix4.h"
#include "CypherMath_Ray.h"
#include "CypherMath_Vector2.h"

namespace cypher::math
{

enum class viewport_origin_t : common::u8 {
    TOP_LEFT = 0u, // Screen Y increases downward, matching common UI coordinates.
    BOTTOM_LEFT,   // Screen Y increases upward, matching OpenGL viewport coordinates.
    COUNT          // Enum bound; not a valid origin policy.
};

struct viewport_rect_t {
    f32 x;      // Screen-space left coordinate.
    f32 y;      // Screen-space origin under viewport_origin_t.
    f32 width;  // Positive horizontal extent.
    f32 height; // Positive vertical extent.
};

struct viewport_projection_t {
    vec3_t screen;             // Pixel X/Y and normalized [0, 1] depth.
    f32 clipW;                 // Homogeneous W before perspective division.
    bool_t bInsideClipVolume;  // True when all NDC coordinates are in range.
};

CYPHER_NODISCARD CYPHER_MATH_API bool_t Viewport_IsValid(
    viewport_rect_t viewport ) noexcept;

// screen.z is normalized to [0, 1] for both supported clip-depth policies.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Viewport_TryProjectPoint(
    mat4_t worldToClip,
    viewport_rect_t viewport,
    viewport_origin_t origin,
    clip_depth_range_t depthRange,
    vec3_t worldPoint,
    f32 minimumAbsW,
    CY_OUT viewport_projection_t *pProjection ) noexcept;

// clipToWorld is normally the inverse of projection * view.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Viewport_TryUnprojectPoint(
    mat4_t clipToWorld,
    viewport_rect_t viewport,
    viewport_origin_t origin,
    clip_depth_range_t depthRange,
    vec3_t screenPoint,
    f32 minimumAbsW,
    CY_OUT vec3_t *pWorldPoint ) noexcept;

// The returned origin lies on the near clip plane and direction is unit length.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Viewport_TryBuildPickingRay(
    mat4_t clipToWorld,
    viewport_rect_t viewport,
    viewport_origin_t origin,
    clip_depth_range_t depthRange,
    vec2_t screenPoint,
    f32 minimumAbsW,
    f32 minimumDirectionLength,
    CY_OUT ray_t *pRay ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_VIEWPORT_H
