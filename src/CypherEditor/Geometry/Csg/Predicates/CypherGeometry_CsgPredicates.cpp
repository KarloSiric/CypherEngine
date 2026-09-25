//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgPredicates.cpp
//  Purpose: Implements the CSG exact predicates.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgPredicates.h"

#include "CypherMath_Predicates.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

i32 CsgPredicate_Side( math::vec3d_t a, math::vec3d_t b, math::vec3d_t c, math::vec3d_t p ) noexcept
{
    // Orient3D is positive below the plane of (a, b, c); "facing" is above.
    return -math::Orient3D( a, b, c, p );
}

math::vec2d_t CsgPredicate_Project( const csg_projection_t &pr, math::vec3d_t p ) noexcept
{
    const math::vec2d_t q = pr.axis == 0u ? math::vec2d_t{ p.y, p.z } : pr.axis == 1u ? math::vec2d_t{ p.z, p.x } : math::vec2d_t{ p.x, p.y };
    return pr.bFlip ? math::vec2d_t{ q.y, q.x } : q;
}

bool CsgPredicate_TryProjection( math::vec3d_t a, math::vec3d_t b, math::vec3d_t c, csg_projection_t *pOut ) noexcept
{
    const math::vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, a ) );
    const f64 m[3] = { std::fabs( n.x ), std::fabs( n.y ), std::fabs( n.z ) };
    u32 order[3] = { 0u, 1u, 2u };
    // Try axes from the largest normal component down; the first whose
    // projection is exactly non-degenerate wins.
    for ( u32 i = 0u; i < 3u; ++i ) {
        for ( u32 j = i + 1u; j < 3u; ++j ) {
            if ( m[order[j]] > m[order[i]] ) {
                const u32 t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    for ( const u32 axis : order ) {
        csg_projection_t pr{ axis, false };
        const i32 o = math::Orient2D( CsgPredicate_Project( pr, a ), CsgPredicate_Project( pr, b ), CsgPredicate_Project( pr, c ) );
        if ( o != 0 ) {
            pr.bFlip = o < 0;
            *pOut = pr;
            return true;
        }
    }
    return false;
}

i32 CsgPredicate_PointInTriangle2D( math::vec2d_t a, math::vec2d_t b, math::vec2d_t c, math::vec2d_t p ) noexcept
{
    const i32 o1 = math::Orient2D( a, b, p ), o2 = math::Orient2D( b, c, p ), o3 = math::Orient2D( c, a, p );
    if ( o1 < 0 || o2 < 0 || o3 < 0 ) { return -1; }
    return o1 > 0 && o2 > 0 && o3 > 0 ? 1 : 0;
}

i32 CsgPredicate_SegmentThroughTriangle( math::vec3d_t p, math::vec3d_t q, math::vec3d_t a, math::vec3d_t b, math::vec3d_t c, u32 *piBoundary ) noexcept
{
    // The line p->q passes a triangle edge on the same side for all three
    // edges exactly when it pierces the triangle.
    const i32 o[3] = { math::Orient3D( p, q, a, b ), math::Orient3D( p, q, b, c ), math::Orient3D( p, q, c, a ) };
    const bool bNeg = o[0] < 0 || o[1] < 0 || o[2] < 0, bPos = o[0] > 0 || o[1] > 0 || o[2] > 0;
    if ( bNeg && bPos ) { return -1; }
    u32 cZero = 0u, iZero = 0u;
    for ( u32 i = 0u; i < 3u; ++i ) {
        if ( o[i] == 0 ) {
            ++cZero;
            iZero = i;
        }
    }
    if ( cZero == 0u ) { return 1; }
    if ( piBoundary != nullptr ) {
        // Two zero edge tests meet at their shared corner: edges i and i+1
        // share corner i+1.
        if ( cZero == 1u ) {
            *piBoundary = iZero;
        } else {
            const u32 corner = o[0] == 0 && o[1] == 0 ? 1u : o[1] == 0 && o[2] == 0 ? 2u : 0u;
            *piBoundary = 3u + corner;
        }
    }
    return cZero == 3u ? -1 : 0; // all three zero: the line lies in the plane (not a crossing)
}

bool CsgPredicate_ProperCross2D( math::vec2d_t a, math::vec2d_t b, math::vec2d_t c, math::vec2d_t d ) noexcept
{
    const i32 o1 = math::Orient2D( a, b, c ), o2 = math::Orient2D( a, b, d );
    const i32 o3 = math::Orient2D( c, d, a ), o4 = math::Orient2D( c, d, b );
    return o1 * o2 < 0 && o3 * o4 < 0;
}

bool CsgPredicate_OnSegment2D( math::vec2d_t a, math::vec2d_t b, math::vec2d_t p ) noexcept
{
    if ( math::Orient2D( a, b, p ) != 0 ) { return false; }
    const f64 lx = a.x < b.x ? a.x : b.x, hx = a.x < b.x ? b.x : a.x;
    const f64 ly = a.y < b.y ? a.y : b.y, hy = a.y < b.y ? b.y : a.y;
    return p.x >= lx && p.x <= hx && p.y >= ly && p.y <= hy;
}

} // namespace cypher::editor::geometry
