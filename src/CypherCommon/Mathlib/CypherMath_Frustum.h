//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Frustum.h
//  Purpose: Declares view-frustum construction and corner extraction.
//  Details: Frustum planes point inward, so points inside the frustum have
//           nonnegative signed distance from every plane.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Frustum Contract

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state.
================
*/

#ifndef CYPHER_COMMON_MATH_FRUSTUM_H
#define CYPHER_COMMON_MATH_FRUSTUM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Plane.h"

#include <type_traits>

namespace cypher::math
{

enum class frustum_plane_t : common::u8 {
    LEFT = 0u, // Inward-facing left side plane.
    RIGHT,     // Inward-facing right side plane.
    BOTTOM,    // Inward-facing bottom side plane.
    TOP,       // Inward-facing top side plane.
    NEAR,      // Near depth boundary for the selected clip convention.
    FAR,       // Far depth boundary for the selected clip convention.
    COUNT      // Number of planes stored by frustum_t.
};

inline constexpr u32 CY_FRUSTUM_PLANE_COUNT =
    static_cast<u32>( frustum_plane_t::COUNT ); // Six inward half-spaces.
inline constexpr u32 CY_FRUSTUM_CORNER_COUNT = 8u; // Four near and four far corners.

struct frustum_t {
    plane_t planes[CY_FRUSTUM_PLANE_COUNT]; // Indexed by frustum_plane_t.
};

CYPHER_NODISCARD CYPHER_MATH_API bool_t Frustum_IsFinite(
    frustum_t frustum ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API plane_t Frustum_Plane(
    frustum_t frustum, frustum_plane_t which ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Frustum_TryFromViewProjection(
    mat4_t viewProjection, clip_depth_range_t depthRange,
    f32 minimumPlaneNormalLength, CY_OUT frustum_t *pFrustum ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Frustum_TryCorners(
    mat4_t viewProjection, clip_depth_range_t depthRange,
    f32 minimumAbsInversePivot, f32 minimumAbsW,
    CY_OUT_WRITES( CY_FRUSTUM_CORNER_COUNT ) vec3_t *pCorners ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Frustum_TryTransform(
    frustum_t frustum, affine3_t transform,
    f32 minimumAbsDeterminant, f32 minimumPlaneNormalLength,
    CY_OUT frustum_t *pTransformed ) noexcept;

static_assert(
    sizeof( frustum_t ) == sizeof( plane_t ) * CY_FRUSTUM_PLANE_COUNT );
static_assert( std::is_standard_layout_v<frustum_t> );
static_assert( std::is_trivially_copyable_v<frustum_t> );

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_FRUSTUM_H
