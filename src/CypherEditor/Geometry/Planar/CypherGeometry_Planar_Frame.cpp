//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Frame.cpp
//  Purpose: Implements plane frames and 3D <-> 2D projection.
//  Details: See header. The axis-selection rule is the only subtle part:
//           crossing the normal with the world axis least aligned with it
//           keeps the cross product well conditioned (its length is at
//           least sqrt(2/3)), so u never degrades toward zero.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Planar_Frame.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Smallest normal length accepted before normalization. Anything shorter
// is not a direction but rounding noise.
constexpr f64 kMinNormalLength = 1.0e-300;

// Returns the unit world axis least aligned with n (ties: X, then Y, then Z).
math::vec3d_t LeastAlignedAxis( math::vec3d_t n ) noexcept
{
    const f64 ax = std::fabs( n.x );
    const f64 ay = std::fabs( n.y );
    const f64 az = std::fabs( n.z );
    if ( ax <= ay && ax <= az ) { return math::Vec3d_Make( 1.0, 0.0, 0.0 ); }
    if ( ay <= az ) { return math::Vec3d_Make( 0.0, 1.0, 0.0 ); }
    return math::Vec3d_Make( 0.0, 0.0, 1.0 );
}

// Completes a unit normal into a right-handed frame at `origin`.
bool CompleteFrame(
    math::vec3d_t unitNormal,
    math::vec3d_t origin,
    planar_frame_t *pFrame ) noexcept
{
    math::vec3d_t u{};
    if ( !math::Vec3d_TryNormalize(
             math::Vec3d_Cross( LeastAlignedAxis( unitNormal ), unitNormal ),
             kMinNormalLength, &u, nullptr ) ) {
        return false;
    }
    // v = n x u is already unit length because n and u are orthonormal;
    // recomputing it instead of normalizing avoids a second rounding step.
    const math::vec3d_t v = math::Vec3d_Cross( unitNormal, u );
    pFrame->origin = origin;
    pFrame->u = u;
    pFrame->v = v;
    pFrame->normal = unitNormal;
    return true;
}

} // namespace

bool PlanarFrame_IsValid(
    const planar_frame_t &frame,
    f64 fUnitTolerance ) noexcept
{
    if ( !math::Vec3d_IsFinite( frame.origin ) ||
         !math::Vec3d_IsFinite( frame.u ) ||
         !math::Vec3d_IsFinite( frame.v ) ||
         !math::Vec3d_IsFinite( frame.normal ) ||
         !( fUnitTolerance >= 0.0 ) ) {
        return false;
    }
    auto unit = [&]( math::vec3d_t a ) noexcept {
        return std::fabs( math::Vec3d_LengthSquared( a ) - 1.0 ) <= fUnitTolerance;
    };
    auto orth = [&]( math::vec3d_t a, math::vec3d_t b ) noexcept {
        return std::fabs( math::Vec3d_Dot( a, b ) ) <= fUnitTolerance;
    };
    if ( !unit( frame.u ) || !unit( frame.v ) || !unit( frame.normal ) ) {
        return false;
    }
    if ( !orth( frame.u, frame.v ) || !orth( frame.u, frame.normal ) ||
         !orth( frame.v, frame.normal ) ) {
        return false;
    }
    // Right-handedness: cross(u, v) must point along normal, not against it.
    return math::Vec3d_Dot( math::Vec3d_Cross( frame.u, frame.v ),
                            frame.normal ) > 0.0;
}

geometry_status_t PlanarFrame_TryFromPlane(
    math::planed_t plane,
    planar_frame_t *pFrameOut ) noexcept
{
    if ( pFrameOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( plane.normal ) ||
         !math::Scalar_IsFinite( plane.d ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    math::vec3d_t n{};
    f64 len = 0.0;
    if ( !math::Vec3d_TryNormalize( plane.normal, kMinNormalLength, &n, &len ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    // Scaling d by the same factor keeps the plane unchanged when the
    // caller passed a non-unit normal.
    const f64 d = plane.d / len;
    planar_frame_t frame{};
    if ( !CompleteFrame( n, math::Vec3d_Scale( n, -d ), &frame ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    *pFrameOut = frame;
    return geometry_status_t::OK;
}

planar_frame_result_t PlanarFrame_TryFromLoop(
    const math::vec3d_t *pPoints,
    usize cPoints,
    f64 fPlanarityTolerance ) noexcept
{
    planar_frame_result_t result{};
    if ( pPoints == nullptr || cPoints < 3u || !( fPlanarityTolerance >= 0.0 ) ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    math::vec3d_t centroid = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    math::vec3d_t newell = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    for ( usize i = 0u; i < cPoints; ++i ) {
        const math::vec3d_t a = pPoints[i];
        const math::vec3d_t b = pPoints[( i + 1u ) % cPoints];
        if ( !math::Vec3d_IsFinite( a ) ) {
            result.status = geometry_status_t::NUMERIC_FAILURE;
            return result;
        }
        centroid = math::Vec3d_Add( centroid, a );
        newell.x += ( a.y - b.y ) * ( a.z + b.z );
        newell.y += ( a.z - b.z ) * ( a.x + b.x );
        newell.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    centroid = math::Vec3d_Scale( centroid, 1.0 / static_cast<f64>( cPoints ) );

    math::vec3d_t n{};
    if ( !math::Vec3d_IsFinite( newell ) ||
         !math::Vec3d_TryNormalize( newell, kMinNormalLength, &n, nullptr ) ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }

    planar_frame_t frame{};
    if ( !CompleteFrame( n, centroid, &frame ) ) {
        result.status = geometry_status_t::NUMERIC_FAILURE;
        return result;
    }

    f64 maxDev = 0.0;
    for ( usize i = 0u; i < cPoints; ++i ) {
        const f64 dev = std::fabs( PlanarFrame_SignedDistance( frame, pPoints[i] ) );
        if ( dev > maxDev ) { maxDev = dev; }
    }
    result.frame = frame;
    result.fMaxDeviation = maxDev;
    result.status = maxDev > fPlanarityTolerance ? geometry_status_t::NON_PLANAR
                                                 : geometry_status_t::OK;
    return result;
}

math::vec2d_t PlanarFrame_Project(
    const planar_frame_t &frame,
    math::vec3d_t point ) noexcept
{
    const math::vec3d_t rel = math::Vec3d_Subtract( point, frame.origin );
    return math::Vec2d_Make( math::Vec3d_Dot( rel, frame.u ),
                             math::Vec3d_Dot( rel, frame.v ) );
}

math::vec3d_t PlanarFrame_Unproject(
    const planar_frame_t &frame,
    math::vec2d_t point ) noexcept
{
    return math::Vec3d_Add(
        frame.origin,
        math::Vec3d_Add( math::Vec3d_Scale( frame.u, point.x ),
                         math::Vec3d_Scale( frame.v, point.y ) ) );
}

f64 PlanarFrame_SignedDistance(
    const planar_frame_t &frame,
    math::vec3d_t point ) noexcept
{
    return math::Vec3d_Dot( math::Vec3d_Subtract( point, frame.origin ),
                            frame.normal );
}

math::planed_t PlanarFrame_Plane( const planar_frame_t &frame ) noexcept
{
    return math::Planed_Make( frame.normal,
                              -math::Vec3d_Dot( frame.normal, frame.origin ) );
}

} // namespace cypher::editor::geometry
