//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgIntersections.cpp
//  Purpose: Implements CSG intersection construction.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgIntersections.h"
#include "CypherGeometry_CsgPredicates.h"

#include "CypherMath_Predicates.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

hash64_t csg_point_key_hash_t::operator()( const csg_point_key_t &k ) const noexcept
{
    // FNV-style mix of the five fields; only distributes entries in memory.
    u64 h = 0xCBF29CE484222325ull ^ static_cast<u64>( k.kind );
    const u32 f[4] = { k.a, k.b, k.c, k.d };
    for ( const u32 v : f ) {
        h ^= v;
        h *= 0x100000001B3ull;
        h ^= h >> 29u;
    }
    return h;
}

bool_t csg_point_key_equal_t::operator()( const csg_point_key_t &x, const csg_point_key_t &y ) const noexcept
{
    return x.kind == y.kind && x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d;
}

namespace
{

struct tri_view_t {
    u32 op{ 0u };
    u32 global{ 0u };
    u32 lv[3]{};           // operand-local vertices
    u32 gp[3]{};           // global points
    math::vec3d_t P[3]{};
};

struct ctx_t {
    csg_intersection_t *x{ nullptr };
    geometry_status_t st{ geometry_status_t::OK };
    // Points found for the current pair (deduplicated).
    u32 pts[48]{};
    u32 cPts{ 0u };
};

void AddPt( ctx_t *c, u32 p ) noexcept
{
    for ( u32 i = 0u; i < c->cPts; ++i ) {
        if ( c->pts[i] == p ) { return; }
    }
    if ( c->cPts < 48u ) { c->pts[c->cPts++] = p; }
}

u32 GetOrCreate( ctx_t *c, const csg_point_key_t &key, math::vec3d_t pos ) noexcept
{
    if ( c->st != geometry_status_t::OK ) { return CY_U32_MAX; }
    if ( const u32 *pFound = HashMap_Find( &c->x->lookup, key ) ) { return *pFound; }
    if ( c->x->positions.nCount >= kCsgPointsMax ) {
        c->st = geometry_status_t::LIMIT_EXCEEDED;
        return CY_U32_MAX;
    }
    const u32 index = static_cast<u32>( c->x->positions.nCount );
    if ( !Vector_PushBack( &c->x->positions, pos ) || !Vector_PushBack( &c->x->keys, key ) || !HashMap_Insert( &c->x->lookup, key, index ).pValue ) {
        c->st = geometry_status_t::ALLOCATION_FAILED;
        return CY_U32_MAX;
    }
    return index;
}

void RegisterEdge( ctx_t *c, u32 op, u32 a, u32 b, u32 p ) noexcept
{
    if ( c->st != geometry_status_t::OK || p == CY_U32_MAX ) { return; }
    const u32 v0 = std::min( a, b ), v1 = std::max( a, b );
    // The edge's own corners are not points *on* it.
    if ( p == CsgIntersection_VertexPoint( c->x, op, v0 ) || p == CsgIntersection_VertexPoint( c->x, op, v1 ) ) { return; }
    if ( !Vector_PushBack( &c->x->edgePoints, csg_edge_point_t{ op, v0, v1, p } ) ) { c->st = geometry_status_t::ALLOCATION_FAILED; }
}

// Puts X on the plane of T by solving the plane equation for X's
// coordinate along the normal's dominant axis. For an axis-aligned plane
// (the common case in level geometry) that coordinate becomes exactly the
// plane's, so points on a floor really are on the floor; for others it
// removes all but one rounding of distance to the plane.
math::vec3d_t OntoPlane( math::vec3d_t X, const math::vec3d_t *T ) noexcept
{
    const math::vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( T[1], T[0] ), math::Vec3d_Subtract( T[2], T[0] ) );
    const f64 m[3] = { std::fabs( n.x ), std::fabs( n.y ), std::fabs( n.z ) };
    const u32 k = m[0] >= m[1] && m[0] >= m[2] ? 0u : m[1] >= m[2] ? 1u : 2u, i = ( k + 1u ) % 3u, j = ( k + 2u ) % 3u;
    const f64 nk = math::Vec3d_Component( n, k );
    if ( nk == 0.0 ) { return X; }
    const f64 v = math::Vec3d_Component( T[0], k ) - ( math::Vec3d_Component( n, i ) * ( math::Vec3d_Component( X, i ) - math::Vec3d_Component( T[0], i ) ) +
                                                       math::Vec3d_Component( n, j ) * ( math::Vec3d_Component( X, j ) - math::Vec3d_Component( T[0], j ) ) ) /
                                                          nk;
    math::Vec3d_SetComponent( &X, k, v );
    return X;
}

// Where edge P->Q (canonical order) meets the plane of (a, b, c).
math::vec3d_t Crossing( math::vec3d_t P, math::vec3d_t Q, const math::vec3d_t *T ) noexcept
{
    const math::vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( T[1], T[0] ), math::Vec3d_Subtract( T[2], T[0] ) );
    const f64 dp = math::Vec3d_Dot( n, math::Vec3d_Subtract( P, T[0] ) ), dq = math::Vec3d_Dot( n, math::Vec3d_Subtract( Q, T[0] ) );
    f64 t = dp / ( dp - dq );
    t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    return OntoPlane( math::Vec3d_Lerp( P, Q, t ), T );
}

f64 Cross2( math::vec2d_t a, math::vec2d_t b ) noexcept { return a.x * b.y - a.y * b.x; }

// EDGE_EDGE key: A's edge first, B's second, each (low, high).
csg_point_key_t EdgeEdgeKey( const tri_view_t &X, u32 xi, u32 xj, const tri_view_t &Y, u32 yi, u32 yj ) noexcept
{
    const u32 x0 = std::min( X.lv[xi], X.lv[xj] ), x1 = std::max( X.lv[xi], X.lv[xj] );
    const u32 y0 = std::min( Y.lv[yi], Y.lv[yj] ), y1 = std::max( Y.lv[yi], Y.lv[yj] );
    return X.op == kCsgOperandA ? csg_point_key_t{ csg_point_kind_t::EDGE_EDGE, x0, x1, y0, y1 } : csg_point_key_t{ csg_point_kind_t::EDGE_EDGE, y0, y1, x0, x1 };
}

// X's corners in Y's plane that lie in Y.
void VerticesOnPlane( ctx_t *c, const tri_view_t &X, const tri_view_t &Y, const i32 *sX ) noexcept
{
    csg_projection_t pr{};
    if ( !CsgPredicate_TryProjection( Y.P[0], Y.P[1], Y.P[2], &pr ) ) { return; }
    const math::vec2d_t y2[3] = { CsgPredicate_Project( pr, Y.P[0] ), CsgPredicate_Project( pr, Y.P[1] ), CsgPredicate_Project( pr, Y.P[2] ) };
    for ( u32 k = 0u; k < 3u; ++k ) {
        if ( sX[k] != 0 ) { continue; }
        const math::vec2d_t q = CsgPredicate_Project( pr, X.P[k] );
        const i32 in = CsgPredicate_PointInTriangle2D( y2[0], y2[1], y2[2], q );
        if ( in < 0 ) { continue; }
        AddPt( c, X.gp[k] );
        if ( in == 0 ) {
            for ( u32 m = 0u; m < 3u; ++m ) {
                if ( CsgPredicate_OnSegment2D( y2[m], y2[( m + 1u ) % 3u], q ) ) { RegisterEdge( c, Y.op, Y.lv[m], Y.lv[( m + 1u ) % 3u], X.gp[k] ); }
            }
        }
    }
}

// X's edges that cross Y's plane, where they meet Y.
void EdgesThroughPlane( ctx_t *c, const tri_view_t &X, const tri_view_t &Y, const i32 *sX ) noexcept
{
    for ( u32 k = 0u; k < 3u && c->st == geometry_status_t::OK; ++k ) {
        const u32 i = k, j = ( k + 1u ) % 3u;
        if ( sX[i] * sX[j] >= 0 ) { continue; }
        u32 bnd = 0u;
        const i32 r = CsgPredicate_SegmentThroughTriangle( X.P[i], X.P[j], Y.P[0], Y.P[1], Y.P[2], &bnd );
        if ( r < 0 ) { continue; }
        const u32 lo = X.lv[i] < X.lv[j] ? i : j, hi = lo == i ? j : i;
        if ( r > 0 ) {
            const csg_point_key_t key{ csg_point_kind_t::EDGE_FACE, X.op, X.lv[lo], X.lv[hi], Y.global };
            const u32 p = GetOrCreate( c, key, Crossing( X.P[lo], X.P[hi], Y.P ) );
            RegisterEdge( c, X.op, X.lv[lo], X.lv[hi], p );
            AddPt( c, p );
        } else if ( bnd < 3u ) {
            const u32 m = bnd, n = ( bnd + 1u ) % 3u;
            const u32 p = GetOrCreate( c, EdgeEdgeKey( X, i, j, Y, m, n ), Crossing( X.P[lo], X.P[hi], Y.P ) );
            RegisterEdge( c, X.op, X.lv[i], X.lv[j], p );
            RegisterEdge( c, Y.op, Y.lv[m], Y.lv[n], p );
            AddPt( c, p );
        } else {
            const u32 p = Y.gp[bnd - 3u];
            RegisterEdge( c, X.op, X.lv[i], X.lv[j], p );
            AddPt( c, p );
        }
    }
}

// X's edges that lie in Y's plane: where they cross Y's edges.
void EdgesInPlane( ctx_t *c, const tri_view_t &X, const tri_view_t &Y, const i32 *sX ) noexcept
{
    csg_projection_t pr{};
    if ( !CsgPredicate_TryProjection( Y.P[0], Y.P[1], Y.P[2], &pr ) ) { return; }
    for ( u32 k = 0u; k < 3u && c->st == geometry_status_t::OK; ++k ) {
        const u32 i = k, j = ( k + 1u ) % 3u;
        if ( sX[i] != 0 || sX[j] != 0 ) { continue; }
        const u32 lo = X.lv[i] < X.lv[j] ? i : j, hi = lo == i ? j : i;
        const math::vec2d_t a = CsgPredicate_Project( pr, X.P[lo] ), b = CsgPredicate_Project( pr, X.P[hi] );
        for ( u32 m = 0u; m < 3u; ++m ) {
            const u32 n = ( m + 1u ) % 3u;
            const math::vec2d_t p0 = CsgPredicate_Project( pr, Y.P[m] ), p1 = CsgPredicate_Project( pr, Y.P[n] );
            if ( !CsgPredicate_ProperCross2D( a, b, p0, p1 ) ) { continue; }
            const math::vec2d_t e{ b.x - a.x, b.y - a.y }, f{ p1.x - p0.x, p1.y - p0.y };
            const f64 t = Cross2( math::vec2d_t{ p0.x - a.x, p0.y - a.y }, f ) / Cross2( e, f );
            const u32 p = GetOrCreate( c, EdgeEdgeKey( X, i, j, Y, m, n ), OntoPlane( math::Vec3d_Lerp( X.P[lo], X.P[hi], t ), Y.P ) );
            RegisterEdge( c, X.op, X.lv[i], X.lv[j], p );
            RegisterEdge( c, Y.op, Y.lv[m], Y.lv[n], p );
            AddPt( c, p );
        }
    }
}

bool Emit( ctx_t *c, u32 tri, u32 p0, u32 p1 ) noexcept
{
    return Vector_PushBack( &c->x->constraints, csg_constraint_t{ tri, p0, p1 } );
}

void EmitPoints( ctx_t *c, const tri_view_t &A, const tri_view_t &B ) noexcept
{
    for ( u32 i = 0u; i < c->cPts && c->st == geometry_status_t::OK; ++i ) {
        if ( !Vector_PushBack( &c->x->trianglePoints, csg_triangle_point_t{ A.global, c->pts[i] } ) ||
             !Vector_PushBack( &c->x->trianglePoints, csg_triangle_point_t{ B.global, c->pts[i] } ) ) {
            c->st = geometry_status_t::ALLOCATION_FAILED;
        }
    }
}

void NonCoplanar( ctx_t *c, const tri_view_t &A, const tri_view_t &B, const i32 *sA, const i32 *sB ) noexcept
{
    VerticesOnPlane( c, A, B, sA );
    VerticesOnPlane( c, B, A, sB );
    EdgesThroughPlane( c, A, B, sA );
    EdgesThroughPlane( c, B, A, sB );
    EdgesInPlane( c, A, B, sA );
    EdgesInPlane( c, B, A, sB );
    if ( c->st != geometry_status_t::OK || c->cPts == 0u ) { return; }
    // Order along the line where the planes meet; the ends span the segment.
    const math::vec3d_t nA = math::Vec3d_Cross( math::Vec3d_Subtract( A.P[1], A.P[0] ), math::Vec3d_Subtract( A.P[2], A.P[0] ) );
    const math::vec3d_t nB = math::Vec3d_Cross( math::Vec3d_Subtract( B.P[1], B.P[0] ), math::Vec3d_Subtract( B.P[2], B.P[0] ) );
    const math::vec3d_t d = math::Vec3d_Cross( nA, nB );
    u32 iMin = 0u, iMax = 0u;
    f64 tMin = 0.0, tMax = 0.0;
    for ( u32 i = 0u; i < c->cPts; ++i ) {
        const f64 t = math::Vec3d_Dot( c->x->positions.pData[c->pts[i]], d );
        if ( i == 0u || t < tMin ) {
            tMin = t;
            iMin = i;
        }
        if ( i == 0u || t > tMax ) {
            tMax = t;
            iMax = i;
        }
    }
    EmitPoints( c, A, B );
    if ( c->st == geometry_status_t::OK && c->pts[iMin] != c->pts[iMax] ) {
        if ( !Emit( c, A.global, c->pts[iMin], c->pts[iMax] ) || !Emit( c, B.global, c->pts[iMin], c->pts[iMax] ) ) {
            c->st = geometry_status_t::ALLOCATION_FAILED;
        }
    }
}

// True when the point's key puts it on X's boundary: one of X's corners,
// or constructed on one of X's edges. Decided by construction, not by
// testing rounded coordinates, which can land a hair outside.
bool OnBoundaryByKey( const csg_intersection_t *x, const tri_view_t &X, u32 p ) noexcept
{
    for ( u32 k = 0u; k < 3u; ++k ) {
        if ( X.gp[k] == p ) { return true; }
    }
    const csg_point_key_t &key = x->keys.pData[p];
    u32 e0 = 0u, e1 = 0u;
    if ( key.kind == csg_point_kind_t::EDGE_EDGE ) {
        e0 = X.op == kCsgOperandA ? key.a : key.c;
        e1 = X.op == kCsgOperandA ? key.b : key.d;
    } else if ( key.kind == csg_point_kind_t::EDGE_FACE && key.a == X.op ) {
        e0 = key.b;
        e1 = key.c;
    } else {
        return false;
    }
    for ( u32 k = 0u; k < 3u; ++k ) {
        const u32 a = std::min( X.lv[k], X.lv[( k + 1u ) % 3u] ), b = std::max( X.lv[k], X.lv[( k + 1u ) % 3u] );
        if ( a == e0 && b == e1 ) { return true; }
    }
    return false;
}

// Constraints inside X from Y's edges clipped to X (coplanar pairs).
void ClipEdgesInto( ctx_t *c, const tri_view_t &X, const tri_view_t &Y, const csg_projection_t &pr ) noexcept
{
    math::vec2d_t x2[3], y2[3];
    for ( u32 k = 0u; k < 3u; ++k ) {
        x2[k] = CsgPredicate_Project( pr, X.P[k] );
        y2[k] = CsgPredicate_Project( pr, Y.P[k] );
    }
    if ( math::Orient2D( x2[0], x2[1], x2[2] ) < 0 ) { std::swap( x2[1], x2[2] ); }
    for ( u32 m = 0u; m < 3u && c->st == geometry_status_t::OK; ++m ) {
        const math::vec2d_t a = y2[m], b = y2[( m + 1u ) % 3u];
        const math::vec2d_t dir{ b.x - a.x, b.y - a.y };
        u32 best0 = CY_U32_MAX, best1 = CY_U32_MAX;
        f64 t0 = 0.0, t1 = 0.0;
        // Every point of this pair lying on Y's edge m and inside X is on the
        // clipped chain; its extremes are the constraint.
        for ( u32 i = 0u; i < c->cPts; ++i ) {
            const u32 p = c->pts[i];
            const math::vec2d_t q = CsgPredicate_Project( pr, c->x->positions.pData[p] );
            const csg_point_key_t &key = c->x->keys.pData[p];
            bool bOnEdge = false;
            const u32 ya = Y.lv[m], yb = Y.lv[( m + 1u ) % 3u];
            if ( key.kind == csg_point_kind_t::EDGE_EDGE ) {
                const u32 e0 = Y.op == kCsgOperandA ? key.a : key.c, e1 = Y.op == kCsgOperandA ? key.b : key.d;
                bOnEdge = e0 == std::min( ya, yb ) && e1 == std::max( ya, yb );
            } else if ( key.kind == csg_point_kind_t::VERTEX ) {
                bOnEdge = p == Y.gp[m] || p == Y.gp[( m + 1u ) % 3u] || CsgPredicate_OnSegment2D( a, b, q );
            }
            if ( !bOnEdge ) { continue; }
            if ( !OnBoundaryByKey( c->x, X, p ) && CsgPredicate_PointInTriangle2D( x2[0], x2[1], x2[2], q ) < 0 ) { continue; }
            const f64 t = ( q.x - a.x ) * dir.x + ( q.y - a.y ) * dir.y;
            if ( best0 == CY_U32_MAX || t < t0 ) {
                t0 = t;
                best0 = p;
            }
            if ( best1 == CY_U32_MAX || t > t1 ) {
                t1 = t;
                best1 = p;
            }
        }
        if ( best0 != CY_U32_MAX && best0 != best1 && !Emit( c, X.global, best0, best1 ) ) { c->st = geometry_status_t::ALLOCATION_FAILED; }
    }
}

void Coplanar( ctx_t *c, const tri_view_t &A, const tri_view_t &B ) noexcept
{
    csg_projection_t pr{};
    if ( !CsgPredicate_TryProjection( A.P[0], A.P[1], A.P[2], &pr ) ) { return; }
    const i32 zero[3] = { 0, 0, 0 };
    VerticesOnPlane( c, A, B, zero );
    VerticesOnPlane( c, B, A, zero );
    EdgesInPlane( c, A, B, zero );
    if ( c->st != geometry_status_t::OK || c->cPts == 0u ) { return; }
    EmitPoints( c, A, B );
    ClipEdgesInto( c, A, B, pr );
    ClipEdgesInto( c, B, A, pr );
}

tri_view_t View( const csg_intersection_t *x, const csg_operand_t *op, u32 iOperand, u32 iTriangle ) noexcept
{
    tri_view_t v{};
    v.op = iOperand;
    v.global = iOperand == kCsgOperandA ? iTriangle : x->cTrianglesA + iTriangle;
    const csg_source_triangle_t &t = op->triangles.pData[iTriangle];
    for ( u32 k = 0u; k < 3u; ++k ) {
        v.lv[k] = t.v[k];
        v.gp[k] = CsgIntersection_VertexPoint( x, iOperand, t.v[k] );
        v.P[k] = x->positions.pData[v.gp[k]];
    }
    return v;
}

} // namespace

geometry_status_t CsgIntersection_Init( csg_intersection_t *pX, const allocator_t *pA ) noexcept
{
    if ( pX == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const bool bOk = Vector_Init( &pX->positions, pA ) && Vector_Init( &pX->keys, pA ) && Vector_Init( &pX->alias, pA ) && HashMap_Init( &pX->lookup, pA ) &&
                     Vector_Init( &pX->constraints, pA ) && Vector_Init( &pX->trianglePoints, pA ) && Vector_Init( &pX->edgePoints, pA );
    if ( !bOk ) {
        CsgIntersection_Shutdown( pX );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void CsgIntersection_Shutdown( csg_intersection_t *pX ) noexcept
{
    if ( pX == nullptr ) { return; }
    Vector_Shutdown( &pX->positions );
    Vector_Shutdown( &pX->keys );
    Vector_Shutdown( &pX->alias );
    HashMap_Shutdown( &pX->lookup );
    Vector_Shutdown( &pX->constraints );
    Vector_Shutdown( &pX->trianglePoints );
    Vector_Shutdown( &pX->edgePoints );
}

u32 CsgIntersection_VertexPoint( const csg_intersection_t *pX, u32 iOperand, u32 iLocal ) noexcept
{
    return pX->alias.pData[iOperand == kCsgOperandA ? iLocal : pX->cVerticesA + iLocal];
}

geometry_status_t CsgIntersection_TryCompute( const csg_operand_t *pA, const csg_operand_t *pB, const vector_t<csg_pair_t> &pairs, csg_intersection_t *pX,
                                              csg_diagnostics_t *pDiag ) noexcept
{
    if ( pA == nullptr || pB == nullptr || pX == nullptr || pX->positions.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pAlloc = pX->positions.pAllocator;
    Vector_Clear( &pX->positions );
    Vector_Clear( &pX->keys );
    Vector_Clear( &pX->constraints );
    Vector_Clear( &pX->trianglePoints );
    Vector_Clear( &pX->edgePoints );
    HashMap_Clear( &pX->lookup );
    pX->cVerticesA = static_cast<u32>( pA->positions.nCount );
    pX->cVerticesB = static_cast<u32>( pB->positions.nCount );
    pX->cTrianglesA = static_cast<u32>( pA->triangles.nCount );
    pX->cTrianglesB = static_cast<u32>( pB->triangles.nCount );
    const usize cV = pX->cVerticesA + pX->cVerticesB;
    if ( cV > kCsgPointsMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    if ( !Vector_Resize( &pX->positions, cV ) || !Vector_Resize( &pX->keys, cV ) || !Vector_Resize( &pX->alias, cV ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 v = 0u; v < cV; ++v ) {
        const bool bA = v < pX->cVerticesA;
        const u32 local = bA ? v : v - pX->cVerticesA;
        pX->positions.pData[v] = bA ? pA->positions.pData[local] : pB->positions.pData[local];
        pX->keys.pData[v] = csg_point_key_t{ csg_point_kind_t::VERTEX, bA ? kCsgOperandA : kCsgOperandB, local, 0u, 0u };
        pX->alias.pData[v] = v;
    }
    // B corners exactly on A corners become A's points.
    {
        vector_t<u32> order{};
        if ( !Vector_Init( &order, pAlloc ) || !Vector_Resize( &order, cV ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 v = 0u; v < cV; ++v ) { order.pData[v] = v; }
        std::sort( order.pData, order.pData + cV, [&]( u32 x, u32 y ) {
            const math::vec3d_t &px = pX->positions.pData[x], &py = pX->positions.pData[y];
            if ( px.x != py.x ) { return px.x < py.x; }
            if ( px.y != py.y ) { return px.y < py.y; }
            if ( px.z != py.z ) { return px.z < py.z; }
            return x < y;
        } );
        for ( usize i = 0u; i < cV; ) {
            usize j = i + 1u;
            const math::vec3d_t &p = pX->positions.pData[order.pData[i]];
            while ( j < cV && math::Vec3d_EqualsExact( pX->positions.pData[order.pData[j]], p ) ) { ++j; }
            // The run is sorted by index, so an A vertex (lower index) leads when present.
            const u32 lead = order.pData[i];
            if ( lead < pX->cVerticesA ) {
                for ( usize k = i + 1u; k < j; ++k ) {
                    if ( order.pData[k] >= pX->cVerticesA ) { pX->alias.pData[order.pData[k]] = lead; }
                }
            }
            i = j;
        }
    }
    ctx_t c{};
    c.x = pX;
    for ( usize i = 0u; i < pairs.nCount && c.st == geometry_status_t::OK; ++i ) {
        const tri_view_t A = View( pX, pA, kCsgOperandA, pairs.pData[i].iA );
        const tri_view_t B = View( pX, pB, kCsgOperandB, pairs.pData[i].iB );
        i32 sA[3], sB[3];
        bool bAllZero = true;
        for ( u32 k = 0u; k < 3u; ++k ) {
            sA[k] = CsgPredicate_Side( B.P[0], B.P[1], B.P[2], A.P[k] );
            sB[k] = CsgPredicate_Side( A.P[0], A.P[1], A.P[2], B.P[k] );
            bAllZero = bAllZero && sA[k] == 0;
        }
        auto oneSide = []( const i32 *s ) noexcept { return ( s[0] > 0 && s[1] > 0 && s[2] > 0 ) || ( s[0] < 0 && s[1] < 0 && s[2] < 0 ); };
        if ( oneSide( sA ) || oneSide( sB ) ) { continue; }
        c.cPts = 0u;
        if ( bAllZero ) {
            Coplanar( &c, A, B );
        } else {
            NonCoplanar( &c, A, B, sA, sB );
        }
        if ( c.st != geometry_status_t::OK && pDiag != nullptr ) {
            pDiag->iWitnessTriangle = A.global;
            pDiag->witness = A.P[0];
        }
    }
    if ( c.st != geometry_status_t::OK ) { return c.st; }

    // Rounding step: an exact coincidence in real numbers (two edges meeting
    // at an irrational point) can come out of input rounding as two
    // distinct constructed points a few ulps apart. Kept apart they only
    // make needle triangles, so constructed points that close are merged
    // into one - an input vertex wins when one is involved, otherwise the
    // lowest index - and every record is remapped. Two input vertices are
    // never merged here (they coincide only when exactly equal, see alias).
    {
        const usize cP = pX->positions.nCount;
        math::vec3d_t lo = pA->lo, hi = pA->hi;
        lo = math::Vec3d_Min( lo, pB->lo );
        hi = math::Vec3d_Max( hi, pB->hi );
        const f64 eps = kCsgMergeRelative * std::fmax( math::Vec3d_MaxAbsComponent( math::Vec3d_Subtract( hi, lo ) ), 1e-300 );
        vector_t<u32> order{}, rep{};
        if ( !Vector_Init( &order, pAlloc ) || !Vector_Init( &rep, pAlloc ) || !Vector_Resize( &rep, cP ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 p = 0u; p < cP; ++p ) {
            rep.pData[p] = p;
            // Only points something uses: resolved vertices and constructed points.
            if ( ( p >= cV || pX->alias.pData[p] == p ) && !Vector_PushBack( &order, p ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
        std::sort( order.pData, order.pData + order.nCount, [&]( u32 a, u32 b ) { return pX->positions.pData[a].x < pX->positions.pData[b].x; } );
        auto find = [&]( u32 v ) noexcept {
            while ( rep.pData[v] != v ) { v = rep.pData[v]; }
            return v;
        };
        usize cMerged = 0u;
        for ( usize i = 0u; i < order.nCount; ++i ) {
            const u32 a = order.pData[i];
            for ( usize j = i + 1u; j < order.nCount && pX->positions.pData[order.pData[j]].x - pX->positions.pData[a].x <= eps; ++j ) {
                const u32 b = order.pData[j];
                if ( a < cV && b < cV ) { continue; }
                if ( math::Vec3d_DistanceSquared( pX->positions.pData[a], pX->positions.pData[b] ) > eps * eps ) { continue; }
                const u32 ra = find( a ), rb = find( b );
                if ( ra == rb ) { continue; }
                // Never merge two input vertices through a chain either.
                if ( ra < cV && rb < cV ) { continue; }
                rep.pData[std::max( ra, rb )] = std::min( ra, rb );
                ++cMerged;
            }
        }
        if ( cMerged > 0u ) {
            for ( usize i = 0u; i < pX->constraints.nCount; ++i ) {
                pX->constraints.pData[i].p0 = find( pX->constraints.pData[i].p0 );
                pX->constraints.pData[i].p1 = find( pX->constraints.pData[i].p1 );
            }
            for ( usize i = 0u; i < pX->trianglePoints.nCount; ++i ) { pX->trianglePoints.pData[i].p = find( pX->trianglePoints.pData[i].p ); }
            usize w = 0u;
            for ( usize i = 0u; i < pX->edgePoints.nCount; ++i ) {
                csg_edge_point_t e = pX->edgePoints.pData[i];
                e.p = find( e.p );
                if ( e.p == CsgIntersection_VertexPoint( pX, e.iOperand, e.v0 ) || e.p == CsgIntersection_VertexPoint( pX, e.iOperand, e.v1 ) ) { continue; }
                pX->edgePoints.pData[w++] = e;
            }
            pX->edgePoints.nCount = w;
            usize wc = 0u;
            for ( usize i = 0u; i < pX->constraints.nCount; ++i ) {
                if ( pX->constraints.pData[i].p0 != pX->constraints.pData[i].p1 ) { pX->constraints.pData[wc++] = pX->constraints.pData[i]; }
            }
            pX->constraints.nCount = wc;
        }
        if ( pDiag != nullptr ) { pDiag->cNearDuplicatePoints = cMerged; }
    }
    // Canonical order and no duplicates, so later stages see sets.
    std::sort( pX->constraints.pData, pX->constraints.pData + pX->constraints.nCount, []( const csg_constraint_t &x, const csg_constraint_t &y ) {
        const u32 x0 = std::min( x.p0, x.p1 ), x1 = std::max( x.p0, x.p1 ), y0 = std::min( y.p0, y.p1 ), y1 = std::max( y.p0, y.p1 );
        return x.iTriangle != y.iTriangle ? x.iTriangle < y.iTriangle : x0 != y0 ? x0 < y0 : x1 < y1;
    } );
    usize w = 0u;
    for ( usize i = 0u; i < pX->constraints.nCount; ++i ) {
        csg_constraint_t k = pX->constraints.pData[i];
        if ( k.p0 > k.p1 ) { std::swap( k.p0, k.p1 ); }
        if ( w > 0u && pX->constraints.pData[w - 1u].iTriangle == k.iTriangle && pX->constraints.pData[w - 1u].p0 == k.p0 &&
             pX->constraints.pData[w - 1u].p1 == k.p1 ) {
            continue;
        }
        pX->constraints.pData[w++] = k;
    }
    pX->constraints.nCount = w;
    std::sort( pX->trianglePoints.pData, pX->trianglePoints.pData + pX->trianglePoints.nCount,
               []( const csg_triangle_point_t &x, const csg_triangle_point_t &y ) { return x.iTriangle != y.iTriangle ? x.iTriangle < y.iTriangle : x.p < y.p; } );
    w = 0u;
    for ( usize i = 0u; i < pX->trianglePoints.nCount; ++i ) {
        if ( w > 0u && pX->trianglePoints.pData[w - 1u].iTriangle == pX->trianglePoints.pData[i].iTriangle &&
             pX->trianglePoints.pData[w - 1u].p == pX->trianglePoints.pData[i].p ) {
            continue;
        }
        pX->trianglePoints.pData[w++] = pX->trianglePoints.pData[i];
    }
    pX->trianglePoints.nCount = w;
    std::sort( pX->edgePoints.pData, pX->edgePoints.pData + pX->edgePoints.nCount, []( const csg_edge_point_t &x, const csg_edge_point_t &y ) {
        return x.iOperand != y.iOperand ? x.iOperand < y.iOperand : x.v0 != y.v0 ? x.v0 < y.v0 : x.v1 != y.v1 ? x.v1 < y.v1 : x.p < y.p;
    } );
    w = 0u;
    for ( usize i = 0u; i < pX->edgePoints.nCount; ++i ) {
        const csg_edge_point_t &e = pX->edgePoints.pData[i];
        if ( w > 0u ) {
            const csg_edge_point_t &l = pX->edgePoints.pData[w - 1u];
            if ( l.iOperand == e.iOperand && l.v0 == e.v0 && l.v1 == e.v1 && l.p == e.p ) { continue; }
        }
        pX->edgePoints.pData[w++] = e;
    }
    pX->edgePoints.nCount = w;
    if ( pDiag != nullptr ) {
        pDiag->cIntersectionPoints = pX->positions.nCount - cV;
        pDiag->cSegments = pX->constraints.nCount;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
