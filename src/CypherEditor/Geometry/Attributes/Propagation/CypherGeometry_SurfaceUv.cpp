//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SurfaceUv.cpp
//  Purpose: Implements texture-alignment operations.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SurfaceUv.h"

namespace cypher::editor::geometry
{

namespace
{

using math::f64;
using math::planar_uv_mappingd_t;
using math::vec2d_t;
using math::vec3d_t;

geometry_status_t Finish(
    const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &mapping,
    planar_uv_mappingd_t *pOut ) noexcept
{
    geometry_brush_side_attributes_t probe{};
    probe.uvProjection = mapping;
    const geometry_status_t status = BrushSideAttributes_Validate( policy, probe );
    if ( status == geometry_status_t::OK ) {
        *pOut = mapping;
    }
    return status;
}

bool Project( const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &mapping,
              vec3d_t point, vec2d_t *pUv ) noexcept
{
    return math::Uvd_TryProjectPlanarPoint( mapping, point, policy.fAbsoluteDistanceTolerance, pUv );
}

// Shifts the offset so pivot projects where it did under `before`.
geometry_status_t KeepPivot(
    const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &before,
    planar_uv_mappingd_t *pAfter, vec3d_t pivot ) noexcept
{
    vec2d_t uvBefore{};
    vec2d_t uvAfter{};
    if ( !Project( policy, before, pivot, &uvBefore ) || !Project( policy, *pAfter, pivot, &uvAfter ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    pAfter->offset = math::Vec2d_Add( pAfter->offset, math::Vec2d_Subtract( uvBefore, uvAfter ) );
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t SurfaceUv_TryShift(
    const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &mapping,
    vec2d_t delta, planar_uv_mappingd_t *pOut ) noexcept
{
    if ( pOut == nullptr || !math::Vec2d_IsFinite( delta ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    planar_uv_mappingd_t result = mapping;
    result.offset = math::Vec2d_Add( result.offset, delta );
    return Finish( policy, result, pOut );
}

geometry_status_t SurfaceUv_TryRotate(
    const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &mapping,
    f64 radians, vec3d_t pivot, planar_uv_mappingd_t *pOut ) noexcept
{
    if ( pOut == nullptr || !math::Scalar_IsFinite( radians ) || !math::Vec3d_IsFinite( pivot ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    planar_uv_mappingd_t result = mapping;
    result.rotationRadians += radians;
    const geometry_status_t status = KeepPivot( policy, mapping, &result, pivot );
    return status == geometry_status_t::OK ? Finish( policy, result, pOut ) : status;
}

geometry_status_t SurfaceUv_TryScale(
    const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &mapping,
    vec2d_t factors, vec3d_t pivot, planar_uv_mappingd_t *pOut ) noexcept
{
    if ( pOut == nullptr || !math::Vec2d_IsFinite( factors ) || factors.x == 0.0 ||
         factors.y == 0.0 || !math::Vec3d_IsFinite( pivot ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    planar_uv_mappingd_t result = mapping;
    result.worldUnitsPerUv.x *= factors.x;
    result.worldUnitsPerUv.y *= factors.y;
    const geometry_status_t status = KeepPivot( policy, mapping, &result, pivot );
    return status == geometry_status_t::OK ? Finish( policy, result, pOut ) : status;
}

geometry_status_t SurfaceUv_TryFlip(
    const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &mapping,
    bool_t bFlipU, bool_t bFlipV, vec3d_t pivot, planar_uv_mappingd_t *pOut ) noexcept
{
    return SurfaceUv_TryScale( policy, mapping,
                               math::Vec2d_Make( bFlipU ? -1.0 : 1.0, bFlipV ? -1.0 : 1.0 ), pivot, pOut );
}

geometry_status_t SurfaceUv_TryFit(
    const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &mapping,
    common::span_t<const vec3d_t> facePoints, f64 repeatsU, f64 repeatsV,
    planar_uv_mappingd_t *pOut ) noexcept
{
    if ( pOut == nullptr || !common::Span_IsValid( facePoints ) || facePoints.nCount < 3u ||
         !math::Scalar_IsFinite( repeatsU ) || !math::Scalar_IsFinite( repeatsV ) ||
         !( repeatsU > 0.0 ) || !( repeatsV > 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    planar_uv_mappingd_t result = mapping;
    result.rotationRadians = 0.0;
    result.offset = math::CY_VEC2D_ZERO;

    // Extent of the face along the projection axes in world units.
    f64 uMin = 0.0;
    f64 uMax = 0.0;
    f64 vMin = 0.0;
    f64 vMax = 0.0;
    for ( common::usize i = 0u; i < facePoints.nCount; ++i ) {
        const vec3d_t relative = math::Vec3d_Subtract( facePoints.pData[i], result.origin );
        const f64 u = math::Vec3d_Dot( relative, result.uAxis );
        const f64 v = math::Vec3d_Dot( relative, result.vAxis );
        uMin = i == 0u || u < uMin ? u : uMin;
        uMax = i == 0u || u > uMax ? u : uMax;
        vMin = i == 0u || v < vMin ? v : vMin;
        vMax = i == 0u || v > vMax ? v : vMax;
    }
    const f64 spanU = uMax - uMin;
    const f64 spanV = vMax - vMin;
    if ( !( spanU > policy.fAbsoluteDistanceTolerance ) || !( spanV > policy.fAbsoluteDistanceTolerance ) ) {
        return geometry_status_t::DEGENERATE;
    }
    // Keep each axis's mirroring sign.
    const f64 signU = mapping.worldUnitsPerUv.x < 0.0 ? -1.0 : 1.0;
    const f64 signV = mapping.worldUnitsPerUv.y < 0.0 ? -1.0 : 1.0;
    result.worldUnitsPerUv = math::Vec2d_Make( signU * spanU / repeatsU, signV * spanV / repeatsV );
    // Start at UV 0 on each axis.
    result.offset = math::Vec2d_Make( -( signU > 0.0 ? uMin : uMax ) / result.worldUnitsPerUv.x,
                                      -( signV > 0.0 ? vMin : vMax ) / result.worldUnitsPerUv.y );
    return Finish( policy, result, pOut );
}

geometry_status_t SurfaceUv_TryAlignToEdge(
    const geometry_numerical_policy_t &policy, const planar_uv_mappingd_t &mapping,
    vec3d_t a, vec3d_t b, vec3d_t faceNormal, planar_uv_mappingd_t *pOut ) noexcept
{
    if ( pOut == nullptr || !math::Vec3d_IsFinite( a ) || !math::Vec3d_IsFinite( b ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    vec3d_t n{};
    if ( !math::Vec3d_TryNormalize( faceNormal, 1.0e-12, &n, nullptr ) ) {
        return geometry_status_t::DEGENERATE;
    }
    const vec3d_t edge = math::Vec3d_Subtract( b, a );
    vec3d_t u{};
    if ( !math::Vec3d_TryNormalize(
             math::Vec3d_Subtract( edge, math::Vec3d_Scale( n, math::Vec3d_Dot( edge, n ) ) ), 1.0e-12, &u,
             nullptr ) ) {
        return geometry_status_t::DEGENERATE;
    }
    planar_uv_mappingd_t result = mapping;
    const f64 handedness =
        math::Vec3d_Dot( math::Vec3d_Cross( mapping.uAxis, mapping.vAxis ), mapping.normal ) < 0.0 ? -1.0
                                                                                                  : 1.0;
    result.uAxis = u;
    result.vAxis = math::Vec3d_Scale( math::Vec3d_Cross( n, u ), handedness );
    result.normal = n;
    result.rotationRadians = 0.0;
    const geometry_status_t status = KeepPivot( policy, mapping, &result, a );
    return status == geometry_status_t::OK ? Finish( policy, result, pOut ) : status;
}

geometry_status_t SurfaceUv_TryReset(
    const geometry_numerical_policy_t &policy, vec3d_t faceNormal, planar_uv_mappingd_t *pOut ) noexcept
{
    if ( pOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return AttributePropagation_TryRebaseProjection(
        policy, BrushSideAttributes_MakeDefault().uvProjection, faceNormal, pOut );
}

} // namespace cypher::editor::geometry
