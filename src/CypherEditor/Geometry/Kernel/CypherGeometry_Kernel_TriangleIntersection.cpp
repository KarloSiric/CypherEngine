//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_TriangleIntersection.cpp
//  Purpose: Implements exact segment/triangle and triangle/triangle tests.
//  Details: Segment vs triangle (non-coplanar) uses the classic sign test:
//           [p, q] meets triangle abc iff p and q are not strictly on the
//           same side of the plane and the three tetrahedra (p, q, edge)
//           all have the same orientation sign (zeros allowed). With exact
//           Orient3D that test has no tolerance and no rounding.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Kernel_TriangleIntersection.h"
#include "CypherGeometry_Planar_Segment.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Exact projections: dropping a coordinate never rounds.
enum class drop_axis_t : u8 { X, Y, Z };

math::vec2d_t Project( math::vec3d_t p, drop_axis_t axis ) noexcept
{
    switch ( axis ) {
        case drop_axis_t::X: return math::Vec2d_Make( p.y, p.z );
        case drop_axis_t::Y: return math::Vec2d_Make( p.z, p.x );
        case drop_axis_t::Z: break;
    }
    return math::Vec2d_Make( p.x, p.y );
}

// Picks a projection in which the triangle is non-degenerate. A triangle
// is non-degenerate in 3D iff at least one axis projection is (exactly).
bool TryPickProjection( math::vec3d_t a, math::vec3d_t b, math::vec3d_t c,
                        drop_axis_t *pAxis ) noexcept
{
    const drop_axis_t axes[3] = { drop_axis_t::Z, drop_axis_t::Y, drop_axis_t::X };
    for ( drop_axis_t axis : axes ) {
        if ( math::Orient2D( Project( a, axis ), Project( b, axis ), Project( c, axis ) ) != 0 ) {
            *pAxis = axis;
            return true;
        }
    }
    return false;
}

bool Degenerate( math::vec3d_t a, math::vec3d_t b, math::vec3d_t c ) noexcept
{
    drop_axis_t axis{};
    return !TryPickProjection( a, b, c, &axis );
}

// Closed point-in-triangle in 2D, exact.
bool PointInTriangle2( math::vec2d_t p, math::vec2d_t a, math::vec2d_t b, math::vec2d_t c ) noexcept
{
    const i32 s0 = math::Orient2D( a, b, p );
    const i32 s1 = math::Orient2D( b, c, p );
    const i32 s2 = math::Orient2D( c, a, p );
    const bool hasNeg = s0 < 0 || s1 < 0 || s2 < 0;
    const bool hasPos = s0 > 0 || s1 > 0 || s2 > 0;
    return !( hasNeg && hasPos );
}

// Strict interior test (boundary excluded), exact. Requires a non-degenerate
// triangle.
bool PointStrictlyInTriangle2( math::vec2d_t p, math::vec2d_t a, math::vec2d_t b,
                               math::vec2d_t c ) noexcept
{
    const i32 s0 = math::Orient2D( a, b, p );
    const i32 s1 = math::Orient2D( b, c, p );
    const i32 s2 = math::Orient2D( c, a, p );
    return ( s0 > 0 && s1 > 0 && s2 > 0 ) || ( s0 < 0 && s1 < 0 && s2 < 0 );
}

bool SegmentMeetsTriangle2( math::vec2d_t p, math::vec2d_t q, math::vec2d_t a, math::vec2d_t b,
                            math::vec2d_t c ) noexcept
{
    if ( PointInTriangle2( p, a, b, c ) || PointInTriangle2( q, a, b, c ) ) { return true; }
    return Planar_ClassifySegments( p, q, a, b ) != planar_segment_relation_t::DISJOINT ||
           Planar_ClassifySegments( p, q, b, c ) != planar_segment_relation_t::DISJOINT ||
           Planar_ClassifySegments( p, q, c, a ) != planar_segment_relation_t::DISJOINT;
}

bool TrianglesMeet2( const math::vec2d_t t0[3], const math::vec2d_t t1[3] ) noexcept
{
    for ( int i = 0; i < 3; ++i ) {
        if ( SegmentMeetsTriangle2( t0[i], t0[( i + 1 ) % 3], t1[0], t1[1], t1[2] ) ) {
            return true;
        }
    }
    // t1 strictly inside t0 has no edge contact; one vertex test covers it.
    return PointInTriangle2( t1[0], t0[0], t0[1], t0[2] );
}

bool Coplanar( const math::vec3d_t t0[3], const math::vec3d_t t1[3] ) noexcept
{
    return math::Orient3D( t0[0], t0[1], t0[2], t1[0] ) == 0 &&
           math::Orient3D( t0[0], t0[1], t0[2], t1[1] ) == 0 &&
           math::Orient3D( t0[0], t0[1], t0[2], t1[2] ) == 0;
}

} // namespace

bool Kernel_SegmentIntersectsTriangle(
    math::vec3d_t p,
    math::vec3d_t q,
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c ) noexcept
{
    drop_axis_t axis{};
    if ( !TryPickProjection( a, b, c, &axis ) ) { return false; }
    const i32 sp = math::Orient3D( a, b, c, p );
    const i32 sq = math::Orient3D( a, b, c, q );
    if ( sp != 0 && sp == sq ) { return false; } // strictly on one side

    const math::vec2d_t A = Project( a, axis ), B = Project( b, axis ), C = Project( c, axis );
    if ( sp == 0 && sq == 0 ) {
        return SegmentMeetsTriangle2( Project( p, axis ), Project( q, axis ), A, B, C );
    }
    if ( sp == 0 ) { return PointInTriangle2( Project( p, axis ), A, B, C ); }
    if ( sq == 0 ) { return PointInTriangle2( Project( q, axis ), A, B, C ); }

    // Proper plane crossing: inside iff the three edge tetrahedra agree.
    const i32 s1 = math::Orient3D( p, q, a, b );
    const i32 s2 = math::Orient3D( p, q, b, c );
    const i32 s3 = math::Orient3D( p, q, c, a );
    const bool hasNeg = s1 < 0 || s2 < 0 || s3 < 0;
    const bool hasPos = s1 > 0 || s2 > 0 || s3 > 0;
    return !( hasNeg && hasPos );
}

bool Kernel_TrianglesIntersect(
    const math::vec3d_t t0[3],
    const math::vec3d_t t1[3] ) noexcept
{
    if ( Degenerate( t0[0], t0[1], t0[2] ) || Degenerate( t1[0], t1[1], t1[2] ) ) {
        return false;
    }
    if ( Coplanar( t0, t1 ) ) {
        drop_axis_t axis{};
        (void)TryPickProjection( t0[0], t0[1], t0[2], &axis );
        const math::vec2d_t p0[3] = { Project( t0[0], axis ), Project( t0[1], axis ),
                                      Project( t0[2], axis ) };
        const math::vec2d_t p1[3] = { Project( t1[0], axis ), Project( t1[1], axis ),
                                      Project( t1[2], axis ) };
        return TrianglesMeet2( p0, p1 );
    }
    for ( int i = 0; i < 3; ++i ) {
        if ( Kernel_SegmentIntersectsTriangle( t0[i], t0[( i + 1 ) % 3], t1[0], t1[1], t1[2] ) ||
             Kernel_SegmentIntersectsTriangle( t1[i], t1[( i + 1 ) % 3], t0[0], t0[1], t0[2] ) ) {
            return true;
        }
    }
    return false;
}

bool Kernel_TrianglesIntersectExcludingShared(
    const math::vec3d_t t0[3],
    const u32 i0[3],
    const math::vec3d_t t1[3],
    const u32 i1[3] ) noexcept
{
    int shared = 0;
    int s0[3] = { -1, -1, -1 }; // for each corner of t0: matching corner of t1
    for ( int a = 0; a < 3; ++a ) {
        for ( int b = 0; b < 3; ++b ) {
            if ( i0[a] == i1[b] ) { s0[a] = b; ++shared; }
        }
    }
    if ( shared == 0 ) { return Kernel_TrianglesIntersect( t0, t1 ); }
    if ( Degenerate( t0[0], t0[1], t0[2] ) || Degenerate( t1[0], t1[1], t1[2] ) ) {
        return false;
    }
    if ( shared >= 3 ) { return true; } // duplicate triangle

    const bool coplanar = Coplanar( t0, t1 );
    if ( shared == 2 ) {
        // Planes differ -> their only common points lie on the shared edge.
        if ( !coplanar ) { return false; }
        int a = 0;
        while ( s0[a] >= 0 ) { ++a; }                // t0's opposite corner
        int b = 0;
        while ( b == s0[0] || b == s0[1] || b == s0[2] ) { ++b; } // t1's opposite corner
        int u = ( a + 1 ) % 3, v = ( a + 2 ) % 3;     // shared edge in t0
        drop_axis_t axis{};
        (void)TryPickProjection( t0[0], t0[1], t0[2], &axis );
        const math::vec2d_t U = Project( t0[u], axis ), V = Project( t0[v], axis );
        const i32 sa = math::Orient2D( U, V, Project( t0[a], axis ) );
        const i32 sb = math::Orient2D( U, V, Project( t1[b], axis ) );
        return sa == sb; // both opposite corners on one side: the faces fold onto each other
    }

    // shared == 1: rotate so corner 0 of each triangle is the shared vertex.
    int k0 = 0;
    while ( s0[k0] < 0 ) { ++k0; }
    const int k1 = s0[k0];
    const math::vec3d_t r0[3] = { t0[k0], t0[( k0 + 1 ) % 3], t0[( k0 + 2 ) % 3] };
    const math::vec3d_t r1[3] = { t1[k1], t1[( k1 + 1 ) % 3], t1[( k1 + 2 ) % 3] };

    if ( !coplanar ) {
        // The common part of two non-coplanar triangles sharing a vertex is
        // a segment from that vertex; any extent beyond it ends on an
        // opposite edge (see header), so opposite edges decide.
        return Kernel_SegmentIntersectsTriangle( r0[1], r0[2], r1[0], r1[1], r1[2] ) ||
               Kernel_SegmentIntersectsTriangle( r1[1], r1[2], r0[0], r0[1], r0[2] );
    }

    drop_axis_t axis{};
    (void)TryPickProjection( r0[0], r0[1], r0[2], &axis );
    const math::vec2d_t P0[3] = { Project( r0[0], axis ), Project( r0[1], axis ), Project( r0[2], axis ) };
    const math::vec2d_t P1[3] = { Project( r1[0], axis ), Project( r1[1], axis ), Project( r1[2], axis ) };
    // Opposite edge of either against every edge of the other.
    for ( int e = 0; e < 3; ++e ) {
        if ( Planar_ClassifySegments( P0[1], P0[2], P1[e], P1[( e + 1 ) % 3] ) !=
                 planar_segment_relation_t::DISJOINT ||
             Planar_ClassifySegments( P1[1], P1[2], P0[e], P0[( e + 1 ) % 3] ) !=
                 planar_segment_relation_t::DISJOINT ) {
            return true;
        }
    }
    // Incident edges running along each other beyond the shared vertex.
    for ( int e0 = 1; e0 <= 2; ++e0 ) {
        for ( int e1 = 1; e1 <= 2; ++e1 ) {
            if ( Planar_ClassifySegments( P0[0], P0[e0], P1[0], P1[e1] ) ==
                 planar_segment_relation_t::COLLINEAR_OVERLAP ) {
                return true;
            }
        }
    }
    // One triangle's far corner strictly inside the other: overlapping wedges.
    return PointStrictlyInTriangle2( P1[1], P0[0], P0[1], P0[2] ) ||
           PointStrictlyInTriangle2( P1[2], P0[0], P0[1], P0[2] ) ||
           PointStrictlyInTriangle2( P0[1], P1[0], P1[1], P1[2] ) ||
           PointStrictlyInTriangle2( P0[2], P1[0], P1[1], P1[2] );
}

} // namespace cypher::editor::geometry
