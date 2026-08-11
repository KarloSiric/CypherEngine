//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Snap.h
//  Purpose: Declares deterministic editor grid and angle snapping.
//  Details: Snapping has explicit origin and rounding policy and can convert
//           world positions to stable integer grid coordinates.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_SNAP_H
#define CYPHER_COMMON_MATH_SNAP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Plane.h"

namespace cypher::math
{

using common::i64;

enum class snap_mode_t : common::u8 {
    NEAREST = 0u,
    FLOOR,
    CEIL,
    COUNT
};

struct grid_coord3_t {
    i64 x;
    i64 y;
    i64 z;
};

CYPHER_NODISCARD CYPHER_MATH_API bool_t Snap_TryScalar(
    f32 value,
    f32 step,
    f32 origin,
    snap_mode_t mode,
    CY_OUT f32 *pSnapped ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Snap_TryVec3(
    vec3_t value,
    vec3_t step,
    vec3_t origin,
    snap_mode_t mode,
    CY_OUT vec3_t *pSnapped ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Snap_TryAngle(
    angle_t value,
    angle_t step,
    angle_t origin,
    snap_mode_t mode,
    bool_t bNormalizePositive,
    CY_OUT angle_t *pSnapped ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Snap_TryWorldToGrid(
    vec3_t value,
    vec3_t step,
    vec3_t origin,
    snap_mode_t mode,
    CY_OUT grid_coord3_t *pGrid ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Snap_TryGridToWorld(
    grid_coord3_t grid,
    vec3_t step,
    vec3_t origin,
    CY_OUT vec3_t *pWorld ) noexcept;

// The plane must be unit length for metric projection.
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Snap_ProjectPointToPlaneUnit(
    vec3_t point, plane_t unitPlane ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Snap_DirectionToPrincipalAxis(
    vec3_t direction ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_SNAP_H
