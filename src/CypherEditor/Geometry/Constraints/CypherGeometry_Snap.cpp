//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Snap.cpp
//  Purpose: Implements deterministic snapping functions.
//  Details: Grid and angle snap use floor(value / spacing + 0.5) * spacing,
//           which rounds half-values toward positive infinity — a
//           deterministic tie-break that is stable under repeated
//           application (idempotent). Component snap is a brute-force
//           nearest-neighbor search; at typical target counts (tens to
//           low hundreds) this is faster than building a spatial structure.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Snap.h"

#include <cmath>

namespace cypher::editor::geometry
{

namespace
{

// Rounds a scalar to the nearest multiple of spacing. Exact midpoints
// go toward positive infinity: floor(x / spacing + 0.5) * spacing.
common::f64 SnapScalar(
    common::f64 value,
    common::f64 spacing ) noexcept
{
    if ( !math::Scalar_IsFinite( value ) ||
         !math::Scalar_IsFinite( spacing ) || spacing <= 0.0 ) {
        return value;
    }
    const common::f64 quotient = value / spacing;
    if ( !math::Scalar_IsFinite( quotient ) ) {
        return value;
    }
    const common::f64 snapped =
        std::floor( quotient + 0.5 ) * spacing;
    return math::Scalar_IsFinite( snapped ) ? snapped : value;
}

} // namespace

// ---------------------------------------------------------------------------
// Grid snap
// ---------------------------------------------------------------------------

math::vec3d_t Snap_GridPoint(
    math::vec3d_t candidate,
    common::f64 fGridSpacing ) noexcept
{
    if ( !math::Scalar_IsFinite( fGridSpacing ) ||
         fGridSpacing <= 0.0 ) {
        return candidate;
    }

    return math::Vec3d_Make(
        SnapScalar( candidate.x, fGridSpacing ),
        SnapScalar( candidate.y, fGridSpacing ),
        SnapScalar( candidate.z, fGridSpacing ) );
}

common::f64 Snap_GridScalar(
    common::f64 value,
    common::f64 fGridSpacing ) noexcept
{
    if ( !math::Scalar_IsFinite( fGridSpacing ) ||
         fGridSpacing <= 0.0 ) {
        return value;
    }
    return SnapScalar( value, fGridSpacing );
}

// ---------------------------------------------------------------------------
// Angle snap
// ---------------------------------------------------------------------------

common::f64 Snap_Angle(
    common::f64 fAngleRadians,
    common::f64 fAngleIncrement ) noexcept
{
    if ( !math::Scalar_IsFinite( fAngleIncrement ) ||
         fAngleIncrement <= 0.0 ) {
        return fAngleRadians;
    }
    return SnapScalar( fAngleRadians, fAngleIncrement );
}

// ---------------------------------------------------------------------------
// Component snap
// ---------------------------------------------------------------------------

snap_component_result_t Snap_NearestComponent(
    math::vec3d_t candidate,
    const math::vec3d_t *pTargets,
    common::usize cTargets,
    common::f64 fMaxDistance ) noexcept
{
    snap_component_result_t result{};

    if ( pTargets == nullptr || cTargets == 0u ||
         !math::Vec3d_IsFinite( candidate ) ||
         !math::Scalar_IsFinite( fMaxDistance ) ||
         fMaxDistance <= 0.0 ) {
        return result;
    }

    common::f64 fBestDistance = fMaxDistance;

    for ( common::usize i = 0u; i < cTargets; ++i ) {
        if ( !math::Vec3d_IsFinite( pTargets[i] ) ) {
            continue;
        }
        const common::f64 dx = pTargets[i].x - candidate.x;
        const common::f64 dy = pTargets[i].y - candidate.y;
        const common::f64 dz = pTargets[i].z - candidate.z;
        const common::f64 distance = std::hypot( dx, dy, dz );
        if ( !math::Scalar_IsFinite( distance ) ||
             distance > fMaxDistance ||
             ( result.bFound && distance >= fBestDistance ) ) {
            continue;
        }
        const common::f64 distSq = distance * distance;
        if ( !math::Scalar_IsFinite( distSq ) ) {
            continue;
        }

        // Strictly less-than: when two targets are equidistant, the
        // first (lower index) wins because we only update on strict
        // improvement.
        fBestDistance = distance;
        result.iTarget = i;
        result.fDistanceSquared = distSq;
        result.bFound = true;
    }

    return result;
}

} // namespace cypher::editor::geometry
