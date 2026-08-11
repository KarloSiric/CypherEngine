//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_UV.h
//  Purpose: Declares planar UV projection for material authoring tools.
//  Details: Mapping stores an orthonormal face basis, world units per UV tile,
//           rotation, and offset with unambiguous forward and inverse operations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_UV_H
#define CYPHER_COMMON_MATH_UV_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Angle.h"
#include "CypherMath_Vector2.h"
#include "CypherMath_Vector3.h"

namespace cypher::math
{

struct planar_uv_mapping_t {
    vec3_t origin;
    vec3_t uAxis;
    vec3_t vAxis;
    vec3_t normal;
    vec2_t worldUnitsPerUv;
    angle_t rotation;
    vec2_t offset;
};

CYPHER_NODISCARD CYPHER_MATH_API bool_t Uv_TryBuildPlanarMapping(
    vec3_t origin,
    vec3_t surfaceNormal,
    vec3_t upHint,
    vec2_t worldUnitsPerUv,
    angle_t rotation,
    vec2_t offset,
    f32 minimumLength,
    CY_OUT planar_uv_mapping_t *pMapping ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Uv_TryProjectPlanarPoint(
    planar_uv_mapping_t mapping,
    vec3_t worldPoint,
    f32 minimumAbsWorldUnitsPerUv,
    CY_OUT vec2_t *pUv ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Uv_TryUnprojectPlanarPoint(
    planar_uv_mapping_t mapping,
    vec2_t uv,
    f32 normalOffset,
    f32 minimumAbsWorldUnitsPerUv,
    CY_OUT vec3_t *pWorldPoint ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_UV_H
