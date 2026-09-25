//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgCorefine.cpp
//  Purpose: Implements corefinement.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgCorefine.h"
#include "CypherGeometry_CsgPredicates.h"
#include "CypherGeometry_Planar_Triangulate.h"

#include "CypherMath_Predicates.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct scratch_t {
    vector_t<u32> global{};          // local -> global point
    vector_t<math::vec2d_t> q{};     // local 2D (triangle's projection)
    vector_t<math::vec2d_t> qc{};    // local 2D (canonical projection, for triangulation)
    vector_t<u8> sideMask{};         // bit k: on triangle edge k
    vector_t<u64> lookup{};          // (global << 32) | local, sorted
    vector_t<u64> edges{};           // (lo << 32) | hi local pairs
    vector_t<u32> edgePts[3]{};
    vector_t<u32> interior{};
    vector_t<u32> work{};
    // Half-edge walk.
    vector_t<u32> heOrigin{}, heNext{}, outStart{}, outList{};
    vector_t<u8> visited{};
    vector_t<u32> cycleStart{}, cycleVerts{};
    vector_t<math::vec2d_t> ring{};
    vector_t<planar_ring_triangle_t> ringTris{};
    vector_t<planar_triangle_t> regionTris{};
    vector_t<u32> ringLocal{};
    vector_t<u32> faceTris{}; // local indices, 3 per triangle

    bool Init( const allocator_t *pA ) noexcept
    {
        bool b = Vector_Init( &global, pA ) && Vector_Init( &q, pA ) && Vector_Init( &qc, pA ) && Vector_Init( &sideMask, pA ) && Vector_Init( &lookup, pA ) &&
                 Vector_Init( &edges, pA ) && Vector_Init( &interior, pA ) && Vector_Init( &work, pA ) && Vector_Init( &heOrigin, pA ) &&
                 Vector_Init( &heNext, pA ) && Vector_Init( &outStart, pA ) && Vector_Init( &outList, pA ) && Vector_Init( &visited, pA ) &&
                 Vector_Init( &cycleStart, pA ) && Vector_Init( &cycleVerts, pA ) && Vector_Init( &ring, pA ) && Vector_Init( &ringTris, pA ) &&
                 Vector_Init( &regionTris, pA ) && Vector_Init( &ringLocal, pA ) && Vector_Init( &faceTris, pA );
        for ( auto &e : edgePts ) { b = b && Vector_Init( &e, pA ); }
        return b;
    }
};

struct ctx_t {
    const csg_operand_t *op[2]{};
    const csg_intersection_t *x{ nullptr };
    vector_t<csg_refined_triangle_t> *pOut{ nullptr };
    const allocator_t *pA{ nullptr };
    scratch_t s{};
    geometry_status_t st{ geometry_status_t::OK };
    usize iConstraint{ 0u }, iPoint{ 0u };
};

u32 LocalOf( const scratch_t &s, u32 g ) noexcept
{
    const u64 key = static_cast<u64>( g ) << 32u;
    const u64 *p = std::lower_bound( s.lookup.pData, s.lookup.pData + s.lookup.nCount, key );
    return p != s.lookup.pData + s.lookup.nCount && ( *p >> 32u ) == g ? static_cast<u32>( *p & 0xFFFFFFFFu ) : CY_U32_MAX;
}

bool PushUnique( vector_t<u32> *pV, u32 v ) noexcept
{
    for ( usize i = 0u; i < pV->nCount; ++i ) {
        if ( pV->pData[i] == v ) { return true; }
    }
    return Vector_PushBack( pV, v );
}

// Which edge of the triangle (op, local corners lv) a point lies on, from
// its key; 3 = not on an edge (interior), 4 = a corner.
u32 EdgeOfPoint( const ctx_t &c, u32 op, const u32 *lv, const u32 *gp, const csg_projection_t &pr, u32 p ) noexcept
{
    for ( u32 k = 0u; k < 3u; ++k ) {
        if ( gp[k] == p ) { return 4u; }
    }
    const csg_point_key_t &key = c.x->keys.pData[p];
    auto ownEdge = [&]( u32 e0, u32 e1 ) noexcept -> u32 {
        for ( u32 k = 0u; k < 3u; ++k ) {
            const u32 a = std::min( lv[k], lv[( k + 1u ) % 3u] ), b = std::max( lv[k], lv[( k + 1u ) % 3u] );
            if ( a == e0 && b == e1 ) { return k; }
        }
        return 3u;
    };
    switch ( key.kind ) {
    case csg_point_kind_t::EDGE_FACE: return key.a == op ? ownEdge( key.b, key.c ) : 3u;
    case csg_point_kind_t::EDGE_EDGE: return op == kCsgOperandA ? ownEdge( key.a, key.b ) : ownEdge( key.c, key.d );
    case csg_point_kind_t::VERTEX: {
        // The other operand's corner: exact, since both are input points.
        const math::vec2d_t qp = CsgPredicate_Project( pr, c.x->positions.pData[p] );
        for ( u32 k = 0u; k < 3u; ++k ) {
            if ( CsgPredicate_OnSegment2D( CsgPredicate_Project( pr, c.x->positions.pData[gp[k]] ),
                                           CsgPredicate_Project( pr, c.x->positions.pData[gp[( k + 1u ) % 3u]] ), qp ) ) {
                return k;
            }
        }
        return 3u;
    }
    }
    return 3u;
}

f64 RingArea( const vector_t<math::vec2d_t> &q, const u32 *pIdx, u32 n ) noexcept
{
    f64 a = 0.0;
    for ( u32 i = 0u; i < n; ++i ) {
        const math::vec2d_t p0 = q.pData[pIdx[i]], p1 = q.pData[pIdx[( i + 1u ) % n]];
        a += p0.x * p1.y - p1.x * p0.y;
    }
    return 0.5 * a;
}

// Winding test: p strictly inside the ring (crossing count on a +x ray).
bool InsideRing( const vector_t<math::vec2d_t> &q, const u32 *pIdx, u32 n, math::vec2d_t p ) noexcept
{
    bool bIn = false;
    for ( u32 i = 0u, j = n - 1u; i < n; j = i++ ) {
        const math::vec2d_t a = q.pData[pIdx[i]], b = q.pData[pIdx[j]];
        if ( ( a.y > p.y ) != ( b.y > p.y ) && p.x < ( b.x - a.x ) * ( p.y - a.y ) / ( b.y - a.y ) + a.x ) { bIn = !bIn; }
    }
    return bIn;
}

bool Emit( ctx_t *c, u32 a, u32 b, u32 d, u32 iSource ) noexcept
{
    csg_refined_triangle_t t{};
    t.v[0] = a;
    t.v[1] = b;
    t.v[2] = d;
    t.iSource = iSource;
    return Vector_PushBack( c->pOut, t );
}

// Ear clipping may leave a zero-area triangle where a face has collinear
// boundary points (three of them end up as the last ear). Such a triangle
// (p, m, q with m between p and q) is removed and the triangle on the other
// side of its long edge p-q is split at m, which keeps the refinement
// conforming. Then everything is emitted with the parent's winding.
// Zero area, or so little against its longest edge that it is a rounding
// artefact of constructed points rather than surface.
bool Sliver( const scratch_t &s, u32 a, u32 b, u32 d ) noexcept
{
    const math::vec2d_t p = s.qc.pData[a], q = s.qc.pData[b], r = s.qc.pData[d];
    if ( math::Orient2D( p, q, r ) == 0 ) { return true; }
    const f64 area2 = std::fabs( ( q.x - p.x ) * ( r.y - p.y ) - ( q.y - p.y ) * ( r.x - p.x ) );
    const f64 l0 = ( q.x - p.x ) * ( q.x - p.x ) + ( q.y - p.y ) * ( q.y - p.y );
    const f64 l1 = ( r.x - q.x ) * ( r.x - q.x ) + ( r.y - q.y ) * ( r.y - q.y );
    const f64 l2 = ( p.x - r.x ) * ( p.x - r.x ) + ( p.y - r.y ) * ( p.y - r.y );
    return area2 <= 1e-12 * std::fmax( l0, std::fmax( l1, l2 ) );
}

geometry_status_t FinishFace( ctx_t *c, bool bReversed, u32 iSource ) noexcept
{
    scratch_t &s = c->s;
    u32 *t = s.faceTris.pData;
    for ( usize guard = 0u; guard < 4u * s.faceTris.nCount + 8u; ++guard ) {
        usize bad = CY_INVALID_SIZE;
        for ( usize i = 0u; i + 2u < s.faceTris.nCount && bad == CY_INVALID_SIZE; i += 3u ) {
            if ( Sliver( s, t[i], t[i + 1u], t[i + 2u] ) ) { bad = i; }
        }
        if ( bad == CY_INVALID_SIZE ) { break; }
        // The middle point lies between the other two along their line.
        u32 m = 0u, p = 0u, q = 0u;
        for ( u32 k = 0u; k < 3u; ++k ) {
            const math::vec2d_t a = s.qc.pData[t[bad + k]], b = s.qc.pData[t[bad + ( k + 1u ) % 3u]], x = s.qc.pData[t[bad + ( k + 2u ) % 3u]];
            if ( ( x.x - a.x ) * ( x.x - b.x ) + ( x.y - a.y ) * ( x.y - b.y ) <= 0.0 ) {
                p = t[bad + k];
                q = t[bad + ( k + 1u ) % 3u];
                m = t[bad + ( k + 2u ) % 3u];
                break;
            }
        }
        usize other = CY_INVALID_SIZE;
        u32 slot = 0u;
        for ( usize i = 0u; i + 2u < s.faceTris.nCount && other == CY_INVALID_SIZE; i += 3u ) {
            if ( i == bad ) { continue; }
            for ( u32 k = 0u; k < 3u; ++k ) {
                const u32 x = t[i + k], y = t[i + ( k + 1u ) % 3u];
                if ( ( x == p && y == q ) || ( x == q && y == p ) ) {
                    other = i;
                    slot = k;
                }
            }
        }
        if ( other == CY_INVALID_SIZE ) { return geometry_status_t::DEGENERATE; }
        const u32 x = t[other + slot], y = t[other + ( slot + 1u ) % 3u], r = t[other + ( slot + 2u ) % 3u];
        // (x, y, r) -> (x, m, r) + (m, y, r): same winding, split at m.
        t[other] = x;
        t[other + 1u] = m;
        t[other + 2u] = r;
        t[bad] = m;
        t[bad + 1u] = y;
        t[bad + 2u] = r;
    }
    for ( usize i = 0u; i + 2u < s.faceTris.nCount; i += 3u ) {
        if ( Sliver( s, t[i], t[i + 1u], t[i + 2u] ) ) { return geometry_status_t::DEGENERATE; }
        const u32 a = s.global.pData[t[i]], b = s.global.pData[t[i + 1u]], d = s.global.pData[t[i + 2u]];
        if ( !( bReversed ? Emit( c, a, d, b, iSource ) : Emit( c, a, b, d, iSource ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    return geometry_status_t::OK;
}

// Triangulates one face (outer ring + holes, local indices) canonically
// and emits the triangles with the parent's winding.
geometry_status_t TriangulateFace( ctx_t *c, const u32 *pOuter, u32 cOuter, const u32 *pHoleStarts, const u32 *pHoleVerts, u32 cHoles, u32 iSource ) noexcept
{
    scratch_t &s = c->s;
    // Canonical ring: counter-clockwise in the canonical frame, starting at
    // its lowest global point. Reversing may flip the parent's winding; the
    // emitted triangles are reversed back in that case.
    const f64 area = RingArea( s.qc, pOuter, cOuter );
    const bool bReversed = area < 0.0;
    if ( !Vector_Resize( &s.ringLocal, cOuter ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( u32 i = 0u; i < cOuter; ++i ) { s.ringLocal.pData[i] = pOuter[bReversed ? cOuter - 1u - i : i]; }
    u32 first = 0u;
    for ( u32 i = 1u; i < cOuter; ++i ) {
        if ( s.global.pData[s.ringLocal.pData[i]] < s.global.pData[s.ringLocal.pData[first]] ) { first = i; }
    }
    std::rotate( s.ringLocal.pData, s.ringLocal.pData + first, s.ringLocal.pData + cOuter );
    // Triangles are gathered first (local indices, canonical winding) so
    // zero-area ones can be repaired before anything is published.
    Vector_Clear( &s.faceTris );
    auto emit = [&]( u32 la, u32 lb, u32 lc ) noexcept -> bool {
        return Vector_PushBack( &s.faceTris, la ) && Vector_PushBack( &s.faceTris, lb ) && Vector_PushBack( &s.faceTris, lc );
    };
    if ( cHoles == 0u ) {
        if ( !Vector_Resize( &s.ring, cOuter ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 i = 0u; i < cOuter; ++i ) { s.ring.pData[i] = s.qc.pData[s.ringLocal.pData[i]]; }
        Vector_Clear( &s.ringTris );
        const geometry_status_t st = Planar_TryTriangulateRing( Vector_Span( static_cast<const vector_t<math::vec2d_t> *>( &s.ring ) ), c->pA, &s.ringTris );
        if ( st != geometry_status_t::OK ) { return st; }
        for ( usize t = 0u; t < s.ringTris.nCount; ++t ) {
            if ( !emit( s.ringLocal.pData[s.ringTris.pData[t].a], s.ringLocal.pData[s.ringTris.pData[t].b], s.ringLocal.pData[s.ringTris.pData[t].c] ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
        return FinishFace( c, bReversed, iSource );
    }
    // With holes: a planar region polygon (outer CCW, holes CW) in the
    // canonical frame; its point array is the contours back to back.
    planar_region_t region{};
    planar_frame_t frame{};
    frame.u = math::Vec3d_Make( 1, 0, 0 );
    frame.v = math::Vec3d_Make( 0, 1, 0 );
    frame.normal = math::Vec3d_Make( 0, 0, 1 );
    geometry_source_id_t one{}, two{};
    one.value = 1u;
    two.value = 2u;
    geometry_status_t st = PlanarRegion_Init( &region, c->pA, frame, one );
    vector_t<u32> order{}; // region point -> local
    if ( st == geometry_status_t::OK && !Vector_Init( &order, c->pA ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
    auto addContour = [&]( const u32 *pIdx, u32 n, bool bHole ) noexcept -> geometry_status_t {
        if ( !Vector_Resize( &s.ring, n ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 i = 0u; i < n; ++i ) {
            s.ring.pData[i] = s.qc.pData[pIdx[i]];
            if ( !Vector_PushBack( &order, pIdx[i] ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
        const span_t<const math::vec2d_t> ring = Vector_Span( static_cast<const vector_t<math::vec2d_t> *>( &s.ring ) );
        return bHole ? PlanarRegion_TryAddHole( &region, two, ring ) : PlanarRegion_TryAddPolygon( &region, one, two, ring, nullptr );
    };
    if ( st == geometry_status_t::OK ) { st = addContour( s.ringLocal.pData, cOuter, false ); }
    for ( u32 h = 0u; st == geometry_status_t::OK && h < cHoles; ++h ) {
        const u32 b = pHoleStarts[h], n = pHoleStarts[h + 1u] - b;
        // Holes wind clockwise in the canonical frame (reverse if needed).
        if ( !Vector_Resize( &s.work, n ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        const bool bFlip = RingArea( s.qc, pHoleVerts + b, n ) > 0.0;
        for ( u32 i = 0u; i < n; ++i ) { s.work.pData[i] = pHoleVerts[b + ( bFlip ? n - 1u - i : i )]; }
        st = addContour( s.work.pData, n, true );
    }
    if ( st == geometry_status_t::OK ) {
        Vector_Clear( &s.regionTris );
        st = Planar_TryTriangulatePolygon( &region, 0u, &s.regionTris );
    }
    for ( usize t = 0u; st == geometry_status_t::OK && t < s.regionTris.nCount; ++t ) {
        if ( !emit( order.pData[s.regionTris.pData[t].a], order.pData[s.regionTris.pData[t].b], order.pData[s.regionTris.pData[t].c] ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
        }
    }
    PlanarRegion_Shutdown( &region );
    return st == geometry_status_t::OK ? FinishFace( c, bReversed, iSource ) : st;
}

geometry_status_t RefineTriangle( ctx_t *c, u32 iGlobal ) noexcept
{
    scratch_t &s = c->s;
    const csg_intersection_t &x = *c->x;
    const u32 op = iGlobal < x.cTrianglesA ? kCsgOperandA : kCsgOperandB;
    const u32 iLocalTri = op == kCsgOperandA ? iGlobal : iGlobal - x.cTrianglesA;
    const csg_source_triangle_t &tri = c->op[op]->triangles.pData[iLocalTri];
    u32 gp[3];
    math::vec3d_t P[3];
    for ( u32 k = 0u; k < 3u; ++k ) {
        gp[k] = CsgIntersection_VertexPoint( &x, op, tri.v[k] );
        P[k] = x.positions.pData[gp[k]];
    }
    // Everything recorded against this triangle or its edges.
    const usize cFirst = c->iConstraint;
    while ( c->iConstraint < x.constraints.nCount && x.constraints.pData[c->iConstraint].iTriangle == iGlobal ) { ++c->iConstraint; }
    const usize pFirst = c->iPoint;
    while ( c->iPoint < x.trianglePoints.nCount && x.trianglePoints.pData[c->iPoint].iTriangle == iGlobal ) { ++c->iPoint; }
    for ( auto &e : s.edgePts ) { Vector_Clear( &e ); }
    Vector_Clear( &s.interior );
    for ( u32 k = 0u; k < 3u; ++k ) {
        const u32 v0 = std::min( tri.v[k], tri.v[( k + 1u ) % 3u] ), v1 = std::max( tri.v[k], tri.v[( k + 1u ) % 3u] );
        const csg_edge_point_t probe{ op, v0, v1, 0u };
        const csg_edge_point_t *pBegin = x.edgePoints.pData, *pEnd = x.edgePoints.pData + x.edgePoints.nCount;
        const csg_edge_point_t *p = std::lower_bound( pBegin, pEnd, probe, []( const csg_edge_point_t &l, const csg_edge_point_t &r ) {
            return l.iOperand != r.iOperand ? l.iOperand < r.iOperand : l.v0 != r.v0 ? l.v0 < r.v0 : l.v1 != r.v1 ? l.v1 < r.v1 : l.p < r.p;
        } );
        for ( ; p != pEnd && p->iOperand == op && p->v0 == v0 && p->v1 == v1; ++p ) {
            if ( !PushUnique( &s.edgePts[k], p->p ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
    }
    csg_projection_t pr{};
    if ( !CsgPredicate_TryProjection( P[0], P[1], P[2], &pr ) ) { return geometry_status_t::DEGENERATE; }
    for ( usize i = pFirst; i < c->iPoint; ++i ) {
        const u32 p = x.trianglePoints.pData[i].p;
        const u32 e = EdgeOfPoint( *c, op, tri.v, gp, pr, p );
        if ( e == 4u ) { continue; }
        // Already recorded on an edge (e.g. merged onto one by the rounding step).
        bool bOnEdge = false;
        for ( const auto &list : s.edgePts ) {
            for ( usize k = 0u; k < list.nCount && !bOnEdge; ++k ) { bOnEdge = list.pData[k] == p; }
        }
        if ( bOnEdge ) { continue; }
        if ( !PushUnique( e < 3u ? &s.edgePts[e] : &s.interior, p ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    const bool bTouched = c->iConstraint > cFirst || s.interior.nCount > 0u || s.edgePts[0].nCount + s.edgePts[1].nCount + s.edgePts[2].nCount > 0u;
    if ( !bTouched ) { return Emit( c, gp[0], gp[1], gp[2], iGlobal ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED; }

    // Local vertices: corners, then each edge's points in order along it,
    // then interior points.
    Vector_Clear( &s.global );
    Vector_Clear( &s.sideMask );
    bool bOk = true;
    for ( u32 k = 0u; k < 3u; ++k ) {
        bOk = bOk && Vector_PushBack( &s.global, gp[k] ) && Vector_PushBack( &s.sideMask, static_cast<u8>( ( 1u << k ) | ( 1u << ( ( k + 2u ) % 3u ) ) ) );
    }
    for ( u32 k = 0u; k < 3u && bOk; ++k ) {
        const math::vec3d_t a = P[k], d = math::Vec3d_Subtract( P[( k + 1u ) % 3u], P[k] );
        vector_t<u32> &e = s.edgePts[k];
        std::sort( e.pData, e.pData + e.nCount, [&]( u32 l, u32 r ) {
            return math::Vec3d_Dot( math::Vec3d_Subtract( x.positions.pData[l], a ), d ) < math::Vec3d_Dot( math::Vec3d_Subtract( x.positions.pData[r], a ), d );
        } );
        for ( usize i = 0u; i < e.nCount && bOk; ++i ) { bOk = Vector_PushBack( &s.global, e.pData[i] ) && Vector_PushBack( &s.sideMask, static_cast<u8>( 1u << k ) ); }
    }
    for ( usize i = 0u; i < s.interior.nCount && bOk; ++i ) { bOk = Vector_PushBack( &s.global, s.interior.pData[i] ) && Vector_PushBack( &s.sideMask, u8{ 0u } ); }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    const u32 cL = static_cast<u32>( s.global.nCount );
    if ( !Vector_Resize( &s.q, cL ) || !Vector_Resize( &s.qc, cL ) || !Vector_Resize( &s.lookup, cL ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    const csg_projection_t canonical{ pr.axis, false };
    for ( u32 i = 0u; i < cL; ++i ) {
        s.q.pData[i] = CsgPredicate_Project( pr, x.positions.pData[s.global.pData[i]] );
        s.qc.pData[i] = CsgPredicate_Project( canonical, x.positions.pData[s.global.pData[i]] );
        s.lookup.pData[i] = ( static_cast<u64>( s.global.pData[i] ) << 32u ) | i;
    }
    std::sort( s.lookup.pData, s.lookup.pData + cL );

    // Edges: the subdivided boundary, then constraints not along it.
    Vector_Clear( &s.edges );
    auto addEdge = [&]( u32 u, u32 v ) noexcept -> bool {
        if ( u == v ) { return true; }
        const u32 lo = std::min( u, v ), hi = std::max( u, v );
        return Vector_PushBack( &s.edges, ( static_cast<u64>( lo ) << 32u ) | hi );
    };
    {
        u32 prev = 0u, base = 3u;
        for ( u32 k = 0u; k < 3u && bOk; ++k ) {
            for ( usize i = 0u; i < s.edgePts[k].nCount && bOk; ++i ) {
                bOk = addEdge( prev, base );
                prev = base++;
            }
            bOk = bOk && addEdge( prev, ( k + 1u ) % 3u );
            prev = ( k + 1u ) % 3u;
        }
    }
    Vector_Clear( &s.work );
    for ( usize i = cFirst; i < c->iConstraint && bOk; ++i ) {
        const u32 u = LocalOf( s, x.constraints.pData[i].p0 ), v = LocalOf( s, x.constraints.pData[i].p1 );
        if ( u == CY_U32_MAX || v == CY_U32_MAX ) { return geometry_status_t::CORRUPT_STATE; }
        if ( ( s.sideMask.pData[u] & s.sideMask.pData[v] ) != 0u ) { continue; } // along the boundary
        bOk = Vector_PushBack( &s.work, u ) && Vector_PushBack( &s.work, v );
    }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    // Split constraints at points lying on them, then reject crossings.
    for ( usize i = 0u; i + 1u < s.work.nCount && bOk; i += 2u ) {
        const u32 u = s.work.pData[i], v = s.work.pData[i + 1u];
        u32 split = CY_U32_MAX;
        for ( u32 w = 0u; w < cL && split == CY_U32_MAX; ++w ) {
            if ( w == u || w == v ) { continue; }
            if ( CsgPredicate_OnSegment2D( s.q.pData[u], s.q.pData[v], s.q.pData[w] ) ) { split = w; }
        }
        if ( split != CY_U32_MAX ) {
            s.work.pData[i + 1u] = split;
            bOk = Vector_PushBack( &s.work, split ) && Vector_PushBack( &s.work, v );
        }
    }
    for ( usize i = 0u; i + 1u < s.work.nCount && bOk; i += 2u ) {
        for ( usize j = i + 2u; j + 1u < s.work.nCount; j += 2u ) {
            const u32 a = s.work.pData[i], b = s.work.pData[i + 1u], d = s.work.pData[j], e = s.work.pData[j + 1u];
            if ( a == d || a == e || b == d || b == e ) { continue; }
            if ( CsgPredicate_ProperCross2D( s.q.pData[a], s.q.pData[b], s.q.pData[d], s.q.pData[e] ) ) { return geometry_status_t::INVALID_TOPOLOGY; }
        }
        if ( ( s.sideMask.pData[s.work.pData[i]] & s.sideMask.pData[s.work.pData[i + 1u]] ) == 0u ) { bOk = addEdge( s.work.pData[i], s.work.pData[i + 1u] ); }
    }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    std::sort( s.edges.pData, s.edges.pData + s.edges.nCount );
    s.edges.nCount = static_cast<usize>( std::unique( s.edges.pData, s.edges.pData + s.edges.nCount ) - s.edges.pData );

    // Prune dangling edges (a touching contact that splits nothing).
    for ( bool bChanged = true; bChanged; ) {
        bChanged = false;
        if ( !Vector_Resize( &s.outStart, cL ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 i = 0u; i < cL; ++i ) { s.outStart.pData[i] = 0u; }
        for ( usize e = 0u; e < s.edges.nCount; ++e ) {
            ++s.outStart.pData[s.edges.pData[e] >> 32u];
            ++s.outStart.pData[s.edges.pData[e] & 0xFFFFFFFFu];
        }
        usize w = 0u;
        for ( usize e = 0u; e < s.edges.nCount; ++e ) {
            const u32 u = static_cast<u32>( s.edges.pData[e] >> 32u ), v = static_cast<u32>( s.edges.pData[e] & 0xFFFFFFFFu );
            if ( s.outStart.pData[u] < 2u || s.outStart.pData[v] < 2u ) {
                bChanged = true;
                continue;
            }
            s.edges.pData[w++] = s.edges.pData[e];
        }
        s.edges.nCount = w;
    }

    // Half-edges: 2e and 2e+1 are the two directions of edge e.
    const usize cH = 2u * s.edges.nCount;
    if ( !Vector_Resize( &s.heOrigin, cH ) || !Vector_Resize( &s.heNext, cH ) || !Vector_Resize( &s.outStart, cL + 1u ) || !Vector_Resize( &s.outList, cH ) ||
         !Vector_Resize( &s.visited, cH ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize e = 0u; e < s.edges.nCount; ++e ) {
        s.heOrigin.pData[2u * e] = static_cast<u32>( s.edges.pData[e] >> 32u );
        s.heOrigin.pData[2u * e + 1u] = static_cast<u32>( s.edges.pData[e] & 0xFFFFFFFFu );
    }
    for ( u32 i = 0u; i <= cL; ++i ) { s.outStart.pData[i] = 0u; }
    for ( usize h = 0u; h < cH; ++h ) { ++s.outStart.pData[s.heOrigin.pData[h] + 1u]; }
    for ( u32 i = 0u; i < cL; ++i ) { s.outStart.pData[i + 1u] += s.outStart.pData[i]; }
    {
        if ( !Vector_Resize( &s.work, cL ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 i = 0u; i < cL; ++i ) { s.work.pData[i] = s.outStart.pData[i]; }
        for ( usize h = 0u; h < cH; ++h ) { s.outList.pData[s.work.pData[s.heOrigin.pData[h]]++] = static_cast<u32>( h ); }
    }
    auto dest = [&]( u32 h ) noexcept { return s.heOrigin.pData[h ^ 1u]; };
    auto angle = [&]( u32 h ) noexcept {
        const math::vec2d_t a = s.q.pData[s.heOrigin.pData[h]], b = s.q.pData[dest( h )];
        return std::atan2( b.y - a.y, b.x - a.x );
    };
    for ( u32 v = 0u; v < cL; ++v ) {
        std::sort( s.outList.pData + s.outStart.pData[v], s.outList.pData + s.outStart.pData[v + 1u], [&]( u32 l, u32 r ) { return angle( l ) < angle( r ); } );
    }
    // next(h) = the out half-edge just clockwise of twin(h) at h's head:
    // walking that way keeps each face on the left (counter-clockwise).
    for ( usize h = 0u; h < cH; ++h ) {
        const u32 v = dest( static_cast<u32>( h ) ), twin = static_cast<u32>( h ) ^ 1u;
        const u32 b = s.outStart.pData[v], n = s.outStart.pData[v + 1u] - b;
        u32 i = 0u;
        while ( i < n && s.outList.pData[b + i] != twin ) { ++i; }
        s.heNext.pData[h] = s.outList.pData[b + ( i + n - 1u ) % n];
        s.visited.pData[h] = 0u;
    }
    Vector_Clear( &s.cycleStart );
    Vector_Clear( &s.cycleVerts );
    for ( usize h0 = 0u; h0 < cH && bOk; ++h0 ) {
        if ( s.visited.pData[h0] != 0u ) { continue; }
        bOk = Vector_PushBack( &s.cycleStart, static_cast<u32>( s.cycleVerts.nCount ) );
        for ( u32 h = static_cast<u32>( h0 ); bOk && s.visited.pData[h] == 0u; h = s.heNext.pData[h] ) {
            s.visited.pData[h] = 1u;
            bOk = Vector_PushBack( &s.cycleVerts, s.heOrigin.pData[h] );
        }
    }
    bOk = bOk && Vector_PushBack( &s.cycleStart, static_cast<u32>( s.cycleVerts.nCount ) );
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    const u32 cCycles = static_cast<u32>( s.cycleStart.nCount - 1u );
    // Positive cycles are faces; negative ones are the outer boundary (it
    // contains corner 0 and walks clockwise) or holes.
    vector_t<u32> holeOwner{}, holeStarts{}, holeVerts{};
    if ( !Vector_Init( &holeOwner, c->pA ) || !Vector_Init( &holeStarts, c->pA ) || !Vector_Init( &holeVerts, c->pA ) ||
         !Vector_Resize( &holeOwner, cCycles ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 cy = 0u; cy < cCycles; ++cy ) {
        holeOwner.pData[cy] = CY_U32_MAX;
        const u32 b = s.cycleStart.pData[cy], n = s.cycleStart.pData[cy + 1u] - b;
        if ( RingArea( s.q, s.cycleVerts.pData + b, n ) >= 0.0 ) { continue; }
        bool bOuter = false;
        for ( u32 i = 0u; i < n && !bOuter; ++i ) { bOuter = s.cycleVerts.pData[b + i] < 3u; }
        if ( bOuter ) { continue; }
        // The innermost face containing the hole.
        const math::vec2d_t probe = s.q.pData[s.cycleVerts.pData[b]];
        f64 bestArea = 0.0;
        for ( u32 f = 0u; f < cCycles; ++f ) {
            const u32 fb = s.cycleStart.pData[f], fn = s.cycleStart.pData[f + 1u] - fb;
            const f64 area = RingArea( s.q, s.cycleVerts.pData + fb, fn );
            if ( area <= 0.0 || !InsideRing( s.q, s.cycleVerts.pData + fb, fn, probe ) ) { continue; }
            if ( holeOwner.pData[cy] == CY_U32_MAX || area < bestArea ) {
                holeOwner.pData[cy] = f;
                bestArea = area;
            }
        }
        if ( holeOwner.pData[cy] == CY_U32_MAX ) { return geometry_status_t::INVALID_TOPOLOGY; }
    }
    for ( u32 f = 0u; f < cCycles; ++f ) {
        const u32 b = s.cycleStart.pData[f], n = s.cycleStart.pData[f + 1u] - b;
        if ( RingArea( s.q, s.cycleVerts.pData + b, n ) <= 0.0 ) { continue; }
        Vector_Clear( &holeStarts );
        Vector_Clear( &holeVerts );
        for ( u32 h = 0u; h < cCycles; ++h ) {
            if ( holeOwner.pData[h] != f ) { continue; }
            if ( !Vector_PushBack( &holeStarts, static_cast<u32>( holeVerts.nCount ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            for ( u32 i = s.cycleStart.pData[h]; i < s.cycleStart.pData[h + 1u]; ++i ) {
                if ( !Vector_PushBack( &holeVerts, s.cycleVerts.pData[i] ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            }
        }
        const u32 cHoles = static_cast<u32>( holeStarts.nCount );
        if ( !Vector_PushBack( &holeStarts, static_cast<u32>( holeVerts.nCount ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        // Copy the ring out: TriangulateFace reuses scratch arrays.
        vector_t<u32> outer{};
        if ( !Vector_Init( &outer, c->pA ) || !Vector_Resize( &outer, n ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 i = 0u; i < n; ++i ) { outer.pData[i] = s.cycleVerts.pData[b + i]; }
        const geometry_status_t st = TriangulateFace( c, outer.pData, n, holeStarts.pData, holeVerts.pData, cHoles, iGlobal );
        if ( st != geometry_status_t::OK ) { return st; }
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t CsgCorefine_TryRefine( const csg_operand_t *pA, const csg_operand_t *pB, const csg_intersection_t *pX,
                                         vector_t<csg_refined_triangle_t> *pOut, usize *pcTrianglesAOut, csg_diagnostics_t *pDiag ) noexcept
{
    if ( pA == nullptr || pB == nullptr || pX == nullptr || pOut == nullptr || pOut->pAllocator == nullptr || pcTrianglesAOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    Vector_Clear( pOut );
    ctx_t c{};
    c.op[0] = pA;
    c.op[1] = pB;
    c.x = pX;
    c.pOut = pOut;
    c.pA = pOut->pAllocator;
    if ( !c.s.Init( c.pA ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    const u32 cTotal = pX->cTrianglesA + pX->cTrianglesB;
    for ( u32 t = 0u; t < cTotal; ++t ) {
        if ( t == pX->cTrianglesA ) { *pcTrianglesAOut = pOut->nCount; }
        const geometry_status_t st = RefineTriangle( &c, t );
        if ( st != geometry_status_t::OK ) {
            if ( pDiag != nullptr ) {
                pDiag->iWitnessTriangle = t;
                const u32 op = t < pX->cTrianglesA ? kCsgOperandA : kCsgOperandB;
                const u32 local = op == kCsgOperandA ? t : t - pX->cTrianglesA;
                pDiag->witness = pX->positions.pData[CsgIntersection_VertexPoint( pX, op, c.op[op]->triangles.pData[local].v[0] )];
            }
            Vector_Clear( pOut );
            return st;
        }
    }
    if ( pX->cTrianglesB == 0u ) { *pcTrianglesAOut = pOut->nCount; }
    if ( pDiag != nullptr ) { pDiag->cRefinedTriangles = pOut->nCount; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
