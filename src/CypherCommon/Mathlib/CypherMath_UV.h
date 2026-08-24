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

/*
================
UV Contract

UV helpers operate in texture-coordinate space and do not assume a particular image origin, wrap
mode, or renderer backend.
================
*/

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
    vec3_t origin;           // World point corresponding to the unrotated UV origin.
    vec3_t uAxis;            // Unit tangent defining increasing U.
    vec3_t vAxis;            // Unit tangent defining increasing V.
    vec3_t normal;           // Unit surface normal completing the mapping basis.
    vec2_t worldUnitsPerUv;  // World distance represented by one UV unit per axis.
    angle_t rotation;        // In-plane rotation applied around normal.
    vec2_t offset;           // UV-space translation applied after projection.
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
