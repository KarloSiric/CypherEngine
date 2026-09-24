//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Segment.cpp
//  Purpose: Implements exact segment classification and proper-crossing
//           construction.
//  Details: Classic orientation-based classification (e.g. CLRS 33.1),
//           with the collinear case split into "touch at one point" and
//           "overlap" by exact parameter comparison along the dominant
//           axis, which is valid because the four points are collinear.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Planar_Segment.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool WithinBox( math::vec2d_t a, math::vec2d_t b, math::vec2d_t p ) noexcept
{
    const f64 minX = a.x < b.x ? a.x : b.x;
    const f64 maxX = a.x < b.x ? b.x : a.x;
    const f64 minY = a.y < b.y ? a.y : b.y;
    const f64 maxY = a.y < b.y ? b.y : a.y;
    return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY;
}

bool Equal( math::vec2d_t a, math::vec2d_t b ) noexcept
{
    return a.x == b.x && a.y == b.y;
}

// For four collinear points: overlap length along the axis where segment
// A has the larger extent. Using the dominant axis avoids the degenerate
// case of a vertical segment compared along x. All comparisons are exact.
planar_segment_relation_t ClassifyCollinear(
    math::vec2d_t a0, math::vec2d_t a1,
    math::vec2d_t b0, math::vec2d_t b1 ) noexcept
{
    const bool useX = std::fabs( a1.x - a0.x ) + std::fabs( b1.x - b0.x ) >=
                      std::fabs( a1.y - a0.y ) + std::fabs( b1.y - b0.y );
    auto key = [useX]( math::vec2d_t p ) noexcept { return useX ? p.x : p.y; };
    const f64 aLo = key( a0 ) < key( a1 ) ? key( a0 ) : key( a1 );
    const f64 aHi = key( a0 ) < key( a1 ) ? key( a1 ) : key( a0 );
    const f64 bLo = key( b0 ) < key( b1 ) ? key( b0 ) : key( b1 );
    const f64 bHi = key( b0 ) < key( b1 ) ? key( b1 ) : key( b0 );
    const f64 lo = aLo > bLo ? aLo : bLo;
    const f64 hi = aHi < bHi ? aHi : bHi;
    if ( lo > hi ) { return planar_segment_relation_t::DISJOINT; }
    if ( lo == hi ) { return planar_segment_relation_t::TOUCH_ENDPOINT; }
    return planar_segment_relation_t::COLLINEAR_OVERLAP;
}

} // namespace

bool Planar_PointOnSegment(
    math::vec2d_t a,
    math::vec2d_t b,
    math::vec2d_t p ) noexcept
{
    return math::Orient2D( a, b, p ) == 0 && WithinBox( a, b, p );
}

planar_segment_relation_t Planar_ClassifySegments(
    math::vec2d_t a0,
    math::vec2d_t a1,
    math::vec2d_t b0,
    math::vec2d_t b1 ) noexcept
{
    // Degenerate segments collapse to point-on-segment tests.
    const bool aPoint = Equal( a0, a1 );
    const bool bPoint = Equal( b0, b1 );
    if ( aPoint && bPoint ) {
        return Equal( a0, b0 ) ? planar_segment_relation_t::TOUCH_ENDPOINT
                               : planar_segment_relation_t::DISJOINT;
    }
    if ( aPoint ) {
        return Planar_PointOnSegment( b0, b1, a0 )
                   ? planar_segment_relation_t::TOUCH_ENDPOINT
                   : planar_segment_relation_t::DISJOINT;
    }
    if ( bPoint ) {
        return Planar_PointOnSegment( a0, a1, b0 )
                   ? planar_segment_relation_t::TOUCH_ENDPOINT
                   : planar_segment_relation_t::DISJOINT;
    }

    const i32 o1 = math::Orient2D( a0, a1, b0 );
    const i32 o2 = math::Orient2D( a0, a1, b1 );
    const i32 o3 = math::Orient2D( b0, b1, a0 );
    const i32 o4 = math::Orient2D( b0, b1, a1 );

    if ( o1 == 0 && o2 == 0 ) {
        return ClassifyCollinear( a0, a1, b0, b1 );
    }
    if ( o1 * o2 < 0 && o3 * o4 < 0 ) {
        return planar_segment_relation_t::PROPER;
    }
    // Any zero orientation with the point inside the other segment's box is
    // a touch at that endpoint.
    if ( ( o1 == 0 && WithinBox( a0, a1, b0 ) ) ||
         ( o2 == 0 && WithinBox( a0, a1, b1 ) ) ||
         ( o3 == 0 && WithinBox( b0, b1, a0 ) ) ||
         ( o4 == 0 && WithinBox( b0, b1, a1 ) ) ) {
        return planar_segment_relation_t::TOUCH_ENDPOINT;
    }
    return planar_segment_relation_t::DISJOINT;
}

bool Planar_TryIntersectProper(
    math::vec2d_t a0,
    math::vec2d_t a1,
    math::vec2d_t b0,
    math::vec2d_t b1,
    f64 *pTaOut,
    f64 *pTbOut,
    math::vec2d_t *pPointOut ) noexcept
{
    const math::vec2d_t r = math::Vec2d_Subtract( a1, a0 );
    const math::vec2d_t s = math::Vec2d_Subtract( b1, b0 );
    const math::vec2d_t q = math::Vec2d_Subtract( b0, a0 );
    const f64 denom = r.x * s.y - r.y * s.x;
    if ( denom == 0.0 || !std::isfinite( denom ) ) { return false; }
    f64 ta = ( q.x * s.y - q.y * s.x ) / denom;
    f64 tb = ( q.x * r.y - q.y * r.x ) / denom;
    // Rounding can push a PROPER crossing marginally outside (0, 1); clamp
    // so the constructed point never lies beyond either segment's box.
    ta = ta < 0.0 ? 0.0 : ( ta > 1.0 ? 1.0 : ta );
    tb = tb < 0.0 ? 0.0 : ( tb > 1.0 ? 1.0 : tb );
    if ( pTaOut ) { *pTaOut = ta; }
    if ( pTbOut ) { *pTbOut = tb; }
    if ( pPointOut ) {
        *pPointOut = math::Vec2d_Add( a0, math::Vec2d_Scale( r, ta ) );
    }
    return true;
}

bool Planar_PointLess( math::vec2d_t a, math::vec2d_t b ) noexcept
{
    return a.x < b.x || ( a.x == b.x && a.y < b.y );
}

} // namespace cypher::editor::geometry
