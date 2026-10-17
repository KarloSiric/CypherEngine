//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_UvProjection.cpp
//  Purpose: Implements building a planar UV projection from an affine map
//           of a plane onto texture space.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_UvProjection.h"

#include <cmath>
#include <utility>

namespace cypher::editor::geometry
{

using common::f64;

geometry_status_t UvProjection_TryFromPlaneMap( math::planar_uv_mappingd_t *pMapping, math::vec3d_t origin, math::vec3d_t e1,
                                                math::vec3d_t e2, const uv_plane_map_t &k, math::vec2d_t offset ) noexcept
{
    constexpr f64 kPi = 3.14159265358979323846;
    if ( pMapping == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !math::Vec3d_IsFinite( origin ) || !math::Vec3d_IsFinite( e1 ) || !math::Vec3d_IsFinite( e2 ) || !math::Vec2d_IsFinite( offset ) ||
         !std::isfinite( k.k00 ) || !std::isfinite( k.k01 ) || !std::isfinite( k.k10 ) || !std::isfinite( k.k11 ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( !( std::fabs( k.k00 * k.k11 - k.k01 * k.k10 ) > 1.0e-18 ) ) { return geometry_status_t::DEGENERATE; }

    // K = R(phi) * diag(sx, sy) * R(beta), closed form; sy < 0 is a mirror.
    const f64 e = 0.5 * ( k.k00 + k.k11 ), f = 0.5 * ( k.k00 - k.k11 ), g = 0.5 * ( k.k10 + k.k01 ), h = 0.5 * ( k.k10 - k.k01 );
    const f64 q = std::hypot( e, h ), r = std::hypot( f, g );
    f64 sx = q + r, sy = q - r;
    const f64 a1 = std::atan2( g, f ), a2 = std::atan2( h, e );
    f64 beta = 0.5 * ( a2 - a1 ), phi = 0.5 * ( a2 + a1 );
    // R(phi) diag(sx, sy) R(beta) == R(phi + pi/2) diag(sy, sx) R(beta - pi/2):
    // take the smallest turn of the axes.
    while ( beta > 0.25 * kPi + 1.0e-12 ) { phi += 0.5 * kPi; std::swap( sx, sy ); beta -= 0.5 * kPi; }
    while ( beta <= -0.25 * kPi - 1.0e-12 ) { phi -= 0.5 * kPi; std::swap( sx, sy ); beta += 0.5 * kPi; }
    // R(pi) is -I: two mirrors are a half turn.
    if ( sx < 0.0 && sy < 0.0 ) { sx = -sx; sy = -sy; phi += kPi; }
    if ( std::fabs( beta ) < 1.0e-12 ) { beta = 0.0; }
    if ( sx == 0.0 || sy == 0.0 ) { return geometry_status_t::DEGENERATE; }

    math::planar_uv_mappingd_t next = *pMapping;
    next.origin = origin;
    // Rows of R(beta) * [e1; e2].
    const f64 cb = std::cos( beta ), sb = std::sin( beta );
    next.uAxis = beta == 0.0 ? e1 : math::Vec3d_Subtract( math::Vec3d_Scale( e1, cb ), math::Vec3d_Scale( e2, sb ) );
    next.vAxis = beta == 0.0 ? e2 : math::Vec3d_Add( math::Vec3d_Scale( e1, sb ), math::Vec3d_Scale( e2, cb ) );
    next.normal = math::Vec3d_Cross( next.uAxis, next.vAxis );
    next.worldUnitsPerUv = math::vec2d_t{ 1.0 / sx, 1.0 / sy };
    next.rotationRadians = std::remainder( phi, 2.0 * kPi );
    next.offset = offset;
    *pMapping = next;
    return geometry_status_t::OK;
}

uv_plane_map_t UvProjection_PlaneMap( const math::planar_uv_mappingd_t &m ) noexcept
{
    // R(theta) * diag(1/su, 1/sv).
    const f64 c = std::cos( m.rotationRadians ), s = std::sin( m.rotationRadians );
    const f64 iu = 1.0 / m.worldUnitsPerUv.x, iv = 1.0 / m.worldUnitsPerUv.y;
    return { c * iu, -s * iv, s * iu, c * iv };
}

} // namespace cypher::editor::geometry
