//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshPlanar.h
//  Purpose: Internal exact 2D polygon checks shared by the mesh operations
//           that reshape faces (knife, bevel): axis-drop projection, exact
//           simplicity, and exact orientation.
//  Details: Not part of the public geometry API (Representations/Mesh only).
//           Every decision goes through the exact Orient2D predicate, and the
//           projection drops a coordinate instead of rotating, so no rounding
//           happens between the stored doubles and the predicate. Whether a
//           reshaped face is still a valid face is therefore never a matter
//           of tolerance; only "is it large enough" is (FaceAreaDescribable).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_PLANAR_H
#define CYPHER_EDITOR_GEOMETRY_MESH_PLANAR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Predicates.h"
#include "CypherMath_Vector2.h"
#include "CypherMath_Vector3.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry::mesh_detail
{

// Drops the given axis. The cyclic coordinate order (yz, zx, xy) ties the
// projection's handedness to the sign of the dropped normal component, and
// dropping a coordinate introduces no rounding, so the exact predicates see
// the stored doubles.
inline math::vec2d_t Project( math::vec3d_t p, common::u32 axis ) noexcept
{
    switch ( axis ) {
        case 0u: return math::Vec2d_Make( p.y, p.z );
        case 1u: return math::Vec2d_Make( p.z, p.x );
        default: return math::Vec2d_Make( p.x, p.y );
    }
}

inline common::i32 SignOf( common::f64 v ) noexcept { return ( v > 0.0 ) - ( v < 0.0 ); }

// p, known collinear with a and b, lies on the closed segment [a, b].
inline bool OnSegment( math::vec2d_t a, math::vec2d_t b, math::vec2d_t p ) noexcept
{
    return std::min( a.x, b.x ) <= p.x && p.x <= std::max( a.x, b.x ) && std::min( a.y, b.y ) <= p.y &&
           p.y <= std::max( a.y, b.y );
}

// Closed segments [p1, p2] and [q1, q2] share at least one point.
inline bool SegmentsMeet( math::vec2d_t p1, math::vec2d_t p2, math::vec2d_t q1, math::vec2d_t q2 ) noexcept
{
    const common::i32 d1 = math::Orient2D( q1, q2, p1 ), d2 = math::Orient2D( q1, q2, p2 );
    const common::i32 d3 = math::Orient2D( p1, p2, q1 ), d4 = math::Orient2D( p1, p2, q2 );
    if ( d1 * d2 < 0 && d3 * d4 < 0 ) { return true; }
    return ( d1 == 0 && OnSegment( q1, q2, p1 ) ) || ( d2 == 0 && OnSegment( q1, q2, p2 ) ) ||
           ( d3 == 0 && OnSegment( p1, p2, q1 ) ) || ( d4 == 0 && OnSegment( p1, p2, q2 ) );
}

// Simple polygon, decided exactly: no zero-length edge, adjacent edges meet
// only at their shared corner (no fold-back), non-adjacent edges never meet.
// Quadratic, which is fine for face-sized rings (<= 256 corners).
inline bool IsSimple( const math::vec2d_t *p, common::u32 n ) noexcept
{
    if ( n < 3u ) { return false; }
    for ( common::u32 i = 0u; i < n; ++i ) {
        const math::vec2d_t a = p[i], b = p[( i + 1u ) % n], c = p[( i + 2u ) % n];
        if ( a.x == b.x && a.y == b.y ) { return false; }
        // (a, b) then (b, c) folds back when c is collinear and on a's side
        // of b. Signs of coordinate differences are exact.
        if ( math::Orient2D( a, b, c ) == 0 && SignOf( c.x - b.x ) == SignOf( a.x - b.x ) &&
             SignOf( c.y - b.y ) == SignOf( a.y - b.y ) ) {
            return false;
        }
    }
    for ( common::u32 i = 0u; i < n; ++i ) {
        for ( common::u32 j = i + 2u; j < n; ++j ) {
            if ( i == 0u && j == n - 1u ) { continue; } // adjacent across the wrap
            if ( SegmentsMeet( p[i], p[i + 1u], p[j], p[( j + 1u ) % n] ) ) { return false; }
        }
    }
    return true;
}

// Orientation of a simple polygon: the turn at its lexicographically
// smallest corner. That corner is extreme, so the turn there cannot be zero
// for a simple polygon and its sign is the polygon's orientation - exact,
// unlike the sign of a floating-point area sum.
inline common::i32 OrientationOf( const math::vec2d_t *p, common::u32 n ) noexcept
{
    common::u32 m = 0u;
    for ( common::u32 i = 1u; i < n; ++i ) {
        if ( p[i].x < p[m].x || ( p[i].x == p[m].x && p[i].y < p[m].y ) ) { m = i; }
    }
    return math::Orient2D( p[( m + n - 1u ) % n], p[m], p[( m + 1u ) % n] );
}

// Axis to drop for a polygon with this (unnormalized) normal: its largest
// component. Only the choice depends on floating point; the tests are exact.
inline common::u32 DominantAxis( math::vec3d_t normal ) noexcept
{
    const common::f64 ax = std::fabs( normal.x ), ay = std::fabs( normal.y ), az = std::fabs( normal.z );
    return ( ax >= ay && ax >= az ) ? 0u : ( ay >= az ? 1u : 2u );
}

} // namespace cypher::editor::geometry::mesh_detail

#endif // CYPHER_EDITOR_GEOMETRY_MESH_PLANAR_H
