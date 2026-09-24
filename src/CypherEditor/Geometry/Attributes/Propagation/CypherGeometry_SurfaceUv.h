//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SurfaceUv.h
//  Purpose: Declares texture-alignment operations on planar projections:
//           shift, rotate, scale, flip, fit, align to edge, and reset.
//  Details: The projection maps uv = R(rotation) * (local / worldUnitsPerUv)
//           + offset. Rotation and scale take a world pivot and adjust the
//           offset so the pivot's UV does not move, the way map editors
//           rotate a texture about the face or a chosen point instead of
//           the projection origin. Fit resets rotation, then scales each
//           axis so the face spans the requested number of repeats and
//           starts at UV 0. Align-to-edge turns the U axis along an edge.
//           Every result is validated and orthonormal.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SURFACE_UV_H
#define CYPHER_EDITOR_GEOMETRY_SURFACE_UV_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Propagation.h"

#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

CYPHER_NODISCARD geometry_status_t SurfaceUv_TryShift(
    const geometry_numerical_policy_t &policy, const math::planar_uv_mappingd_t &mapping,
    math::vec2d_t delta, math::planar_uv_mappingd_t *pOut ) noexcept;

CYPHER_NODISCARD geometry_status_t SurfaceUv_TryRotate(
    const geometry_numerical_policy_t &policy, const math::planar_uv_mappingd_t &mapping,
    math::f64 radians, math::vec3d_t pivot, math::planar_uv_mappingd_t *pOut ) noexcept;

// Multiplies worldUnitsPerUv (texel size in world units) by the factors;
// 2 makes the texture twice as large on the surface. Zero is refused.
CYPHER_NODISCARD geometry_status_t SurfaceUv_TryScale(
    const geometry_numerical_policy_t &policy, const math::planar_uv_mappingd_t &mapping,
    math::vec2d_t factors, math::vec3d_t pivot, math::planar_uv_mappingd_t *pOut ) noexcept;

CYPHER_NODISCARD geometry_status_t SurfaceUv_TryFlip(
    const geometry_numerical_policy_t &policy, const math::planar_uv_mappingd_t &mapping,
    common::bool_t bFlipU, common::bool_t bFlipV, math::vec3d_t pivot,
    math::planar_uv_mappingd_t *pOut ) noexcept;

// Fits the face polygon to repeatsU x repeatsV texture repeats.
CYPHER_NODISCARD geometry_status_t SurfaceUv_TryFit(
    const geometry_numerical_policy_t &policy, const math::planar_uv_mappingd_t &mapping,
    common::span_t<const math::vec3d_t> facePoints, math::f64 repeatsU, math::f64 repeatsV,
    math::planar_uv_mappingd_t *pOut ) noexcept;

// Turns U along edge a->b (in the face plane) and zeroes rotation; the
// face normal fixes V. The offset is adjusted so `a` keeps its UV.
CYPHER_NODISCARD geometry_status_t SurfaceUv_TryAlignToEdge(
    const geometry_numerical_policy_t &policy, const math::planar_uv_mappingd_t &mapping,
    math::vec3d_t a, math::vec3d_t b, math::vec3d_t faceNormal,
    math::planar_uv_mappingd_t *pOut ) noexcept;

// The default world-aligned projection re-based onto the face.
CYPHER_NODISCARD geometry_status_t SurfaceUv_TryReset(
    const geometry_numerical_policy_t &policy, math::vec3d_t faceNormal,
    math::planar_uv_mappingd_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SURFACE_UV_H
