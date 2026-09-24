//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_UvTools.cpp
//  Purpose: Implements pivot-preserving UV manipulations.
//  Details: Every edit is computed on a copy and validated
//           (BrushSideAttributes_Validate) before it is written back, so a
//           rejected edit leaves the record untouched.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_UvTools.h"

#include "CypherMath_UV.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using math::vec2d_t;

namespace
{

// base(p): the projection before rotation and offset.
vec2d_t Base( const math::planar_uv_mappingd_t &m, math::vec3d_t p ) noexcept
{
    const math::vec3d_t r = math::Vec3d_Subtract( p, m.origin );
    return vec2d_t{ math::Vec3d_Dot( r, m.uAxis ) / m.worldUnitsPerUv.x, math::Vec3d_Dot( r, m.vAxis ) / m.worldUnitsPerUv.y };
}

vec2d_t Rotated( vec2d_t v, f64 angle ) noexcept
{
    const f64 c = std::cos( angle ), s = std::sin( angle );
    return vec2d_t{ c * v.x - s * v.y, s * v.x + c * v.y };
}

geometry_status_t Commit( geometry_brush_side_attributes_t *pRecord, const geometry_brush_side_attributes_t &next,
                          const geometry_numerical_policy_t &policy ) noexcept
{
    const geometry_status_t st = BrushSideAttributes_Validate( policy, next );
    if ( st == geometry_status_t::OK ) { *pRecord = next; }
    return st;
}

// Keeps `pivot` at the UV it had under `before`.
geometry_brush_side_attributes_t PinPivot( const geometry_brush_side_attributes_t &before, geometry_brush_side_attributes_t next,
                                           math::vec3d_t pivot ) noexcept
{
    vec2d_t uv{};
    (void)math::Uvd_TryProjectPlanarPoint( before.uvProjection, pivot, 0.0, &uv );
    const vec2d_t turned = Rotated( Base( next.uvProjection, pivot ), next.uvProjection.rotationRadians );
    next.uvProjection.offset = vec2d_t{ uv.x - turned.x, uv.y - turned.y };
    return next;
}

} // namespace

geometry_status_t UvTools_TryShift( geometry_brush_side_attributes_t *pRecord, vec2d_t deltaUv, const geometry_numerical_policy_t &policy ) noexcept
{
    if ( pRecord == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !math::Vec2d_IsFinite( deltaUv ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    geometry_brush_side_attributes_t next = *pRecord;
    next.uvProjection.offset = vec2d_t{ next.uvProjection.offset.x + deltaUv.x, next.uvProjection.offset.y + deltaUv.y };
    return Commit( pRecord, next, policy );
}

geometry_status_t UvTools_TryRotateAbout(
    geometry_brush_side_attributes_t *pRecord, math::vec3d_t pivot, f64 angleRadians, const geometry_numerical_policy_t &policy ) noexcept
{
    if ( pRecord == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !math::Vec3d_IsFinite( pivot ) || !std::isfinite( angleRadians ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    geometry_brush_side_attributes_t next = *pRecord;
    next.uvProjection.rotationRadians = std::remainder( next.uvProjection.rotationRadians + angleRadians, 2.0 * 3.14159265358979323846 );
    return Commit( pRecord, PinPivot( *pRecord, next, pivot ), policy );
}

geometry_status_t UvTools_TryScaleAbout(
    geometry_brush_side_attributes_t *pRecord, math::vec3d_t pivot, vec2d_t factor, const geometry_numerical_policy_t &policy ) noexcept
{
    if ( pRecord == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !math::Vec3d_IsFinite( pivot ) || !math::Vec2d_IsFinite( factor ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    if ( factor.x == 0.0 || factor.y == 0.0 ) { return geometry_status_t::DEGENERATE; }
    geometry_brush_side_attributes_t next = *pRecord;
    next.uvProjection.worldUnitsPerUv =
        vec2d_t{ next.uvProjection.worldUnitsPerUv.x * factor.x, next.uvProjection.worldUnitsPerUv.y * factor.y };
    return Commit( pRecord, PinPivot( *pRecord, next, pivot ), policy );
}

geometry_status_t UvTools_TryShear( geometry_brush_side_attributes_t *pRecord, vec2d_t ) noexcept
{
    return pRecord == nullptr ? geometry_status_t::INVALID_ARGUMENT : geometry_status_t::UNSUPPORTED;
}

} // namespace cypher::editor::geometry
