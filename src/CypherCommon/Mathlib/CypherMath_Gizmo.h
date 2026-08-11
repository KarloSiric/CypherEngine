//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Gizmo.h
//  Purpose: Declares transform-gizmo picking geometry for editor viewports.
//  Details: Axis, plane, and rotation-ring queries expose world-space hit data
//           while remaining independent of Qt, renderer, and selection state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_GIZMO_H
#define CYPHER_COMMON_MATH_GIZMO_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Intersection.h"
#include "CypherMath_Vector2.h"

namespace cypher::math
{

struct gizmo_axis_hit_t {
    vec3_t pointOnRay;
    vec3_t pointOnAxis;
    f32 rayDistance;
    f32 axisDistance;
    f32 separation;
};

struct gizmo_plane_hit_t {
    vec3_t point;
    vec2_t coordinates;
    f32 rayDistance;
};

struct gizmo_ring_hit_t {
    vec3_t point;
    vec3_t radialDirection;
    f32 rayDistance;
    f32 radialDistance;
};

// Ray and axis directions are normalized internally; distances are world units.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Gizmo_TryHitAxis(
    ray_t ray,
    vec3_t axisOrigin,
    vec3_t axisDirection,
    f32 minimumDirectionLength,
    f32 relativeParallelTolerance,
    f32 pickRadius,
    CY_OUT gizmo_axis_hit_t *pHit ) noexcept;

// uAxis and vAxis define a centered rectangular handle in world space.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Gizmo_TryHitPlane(
    ray_t ray,
    vec3_t planeOrigin,
    vec3_t uAxis,
    vec3_t vAxis,
    vec2_t halfExtent,
    f32 minimumDirectionLength,
    f32 minimumAbsDenominator,
    CY_OUT gizmo_plane_hit_t *pHit ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Gizmo_TryHitRing(
    ray_t ray,
    vec3_t center,
    vec3_t normal,
    f32 radius,
    f32 halfThickness,
    f32 minimumDirectionLength,
    f32 minimumAbsDenominator,
    CY_OUT gizmo_ring_hit_t *pHit ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_GIZMO_H
