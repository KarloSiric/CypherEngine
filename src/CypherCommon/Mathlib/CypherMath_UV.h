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

// Binary64 authoring UV mapping ---------------------------------------------------
// The mapping origin is a world point, and world-locked texture projection must
// hold a brush's texture still in world space while its faces are dragged. An
// f32 origin cannot deliver that away from the world origin: at a coordinate of
// 1e5 an f32 ULP is roughly 0.008 world units, so the texture would visibly swim
// on distant geometry. Authoring keeps the whole mapping in binary64 and
// converts only at an explicit cook boundary.
struct planar_uv_mappingd_t {
    vec3d_t origin;          // World point corresponding to the unrotated UV origin.
    vec3d_t uAxis;           // Unit tangent defining increasing U.
    vec3d_t vAxis;           // Unit tangent defining increasing V.
    vec3d_t normal;          // Unit surface normal completing the mapping basis.
    vec2d_t worldUnitsPerUv; // World distance represented by one UV unit per axis.
    // Raw radians rather than a binary64 mirror of angle_t: angle_t is f32, and
    // an f64 counterpart would carry one field and have exactly one user.
    f64 rotationRadians;     // In-plane rotation applied around normal.
    vec2d_t offset;          // UV-space translation applied after projection.
};

CYPHER_NODISCARD CYPHER_MATH_API bool_t Uvd_TryBuildPlanarMapping(
    vec3d_t origin,
    vec3d_t surfaceNormal,
    vec3d_t upHint,
    vec2d_t worldUnitsPerUv,
    f64 rotationRadians,
    vec2d_t offset,
    f64 minimumLength,
    CY_OUT planar_uv_mappingd_t *pMapping ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Uvd_TryProjectPlanarPoint(
    planar_uv_mappingd_t mapping,
    vec3d_t worldPoint,
    f64 minimumAbsWorldUnitsPerUv,
    CY_OUT vec2d_t *pUv ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Uvd_TryUnprojectPlanarPoint(
    planar_uv_mappingd_t mapping,
    vec2d_t uv,
    f64 normalOffset,
    f64 minimumAbsWorldUnitsPerUv,
    CY_OUT vec3d_t *pWorldPoint ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_UV_H
