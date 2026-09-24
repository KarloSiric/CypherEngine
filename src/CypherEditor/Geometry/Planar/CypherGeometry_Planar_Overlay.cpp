//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Overlay.cpp
//  Purpose: Implements the arrangement-based 2D Boolean overlay.
//  Details: See the header for the pipeline. Implementation notes:
//
//           - Half-edge 2e runs min(u,v) -> max(u,v) of undirected edge e,
//             2e+1 is its twin. That makes twin(h) = h ^ 1.
//           - Outgoing half-edges at each vertex are sorted CCW by an
//             exact comparator: half-plane test on exact coordinate
//             comparisons, then Orient2D. next(h) is the outgoing edge
//             immediately *clockwise* of twin(h), which traces every face
//             with its interior on the left.
//           - aSide/bSide of a half-edge: +k when k input edges of that
//             operand run in the half-edge's direction, -k when opposite.
//             Summing means two coincident opposite A edges cancel — the
//             correct answer, since A's interior is then on both sides.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Planar_Overlay.h"
#include "CypherGeometry_Planar_Segment.h"
#include "CypherGeometry_PlanarRegionValidation.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// ---------------------------------------------------------------------------
// Scratch buffer: a vector_t that frees itself and records push failures,
// so the long pipeline can check allocation once per stage instead of
// after every push.
// ---------------------------------------------------------------------------
template <typename type_t>
struct buf_t {
    vector_t<type_t> v{};
    bool ok{ true };

    bool Init( const allocator_t *pAlloc, usize cap ) noexcept {
        ok = Vector_Init( &v, pAlloc, cap );
        return ok;
    }
    ~buf_t() { Vector_Shutdown( &v ); }
    void Push( const type_t &x ) noexcept {
        if ( ok && !Vector_PushBack( &v, x ) ) { ok = false; }
    }
    bool Resize( usize n ) noexcept {
        if ( ok && !Vector_Resize( &v, n ) ) { ok = false; }
        return ok;
    }
    usize Size() const noexcept { return v.nCount; }
    type_t &operator[]( usize i ) noexcept { return v.pData[i]; }
    const type_t &operator[]( usize i ) const noexcept { return v.pData[i]; }
    type_t *Data() noexcept { return v.pData; }
};

struct in_edge_t {
    math::vec2d_t p0{};
    math::vec2d_t p1{};
    u8 owner{ 0u }; // 0 = A, 1 = B
};

struct split_t {
    u32 iEdge{ 0u };
    f64 t{ 0.0 };
    math::vec2d_t p{};
    u8 exact{ 1u };
    u32 iVertex{ 0u };
};

struct sub_edge_t {
    u32 lo{ 0u };
    u32 hi{ 0u };
    i32 dirA{ 0 }; // +1 lo->hi, -1 hi->lo, for owner A
    i32 dirB{ 0 };
};

struct edge_t {
    u32 lo{ 0u };
    u32 hi{ 0u };
    i32 aSide{ 0 }; // relative to lo -> hi
    i32 bSide{ 0 };
};

struct loop_t {
    u32 iFirst{ 0u };  // into loop point buffer
    u32 cPoints{ 0u };
    f64 area{ 0.0 };
    i32 iOwner{ -1 };  // for holes: outer loop index
};

f64 ParamOnEdge( const in_edge_t &e, math::vec2d_t p ) noexcept
{
    const math::vec2d_t d = math::Vec2d_Subtract( e.p1, e.p0 );
    const f64 dd = d.x * d.x + d.y * d.y;
    if ( dd == 0.0 ) { return 0.0; }
    const math::vec2d_t r = math::Vec2d_Subtract( p, e.p0 );
    return ( r.x * d.x + r.y * d.y ) / dd;
}

bool SamePos( math::vec2d_t a, math::vec2d_t b ) noexcept
{
    return a.x == b.x && a.y == b.y;
}

bool FramesIdentical( const planar_frame_t &a, const planar_frame_t &b ) noexcept
{
    auto eq = []( math::vec3d_t x, math::vec3d_t y ) noexcept {
        return x.x == y.x && x.y == y.y && x.z == y.z;
    };
    return eq( a.origin, b.origin ) && eq( a.u, b.u ) && eq( a.v, b.v ) &&
           eq( a.normal, b.normal );
}

u32 FindRoot( u32 *parent, u32 x ) noexcept
{
    while ( parent[x] != x ) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

// Half-plane index of direction (origin -> dest), decided by exact
// comparisons: 0 for angles in [0, pi), 1 for [pi, 2pi).
int HalfPlane( math::vec2d_t o, math::vec2d_t d ) noexcept
{
    if ( d.y > o.y ) { return 0; }
    if ( d.y < o.y ) { return 1; }
    return d.x > o.x ? 0 : 1;
}

bool Selected( planar_boolean_op_t op, i8 inA, i8 inB ) noexcept
{
    const bool a = inA > 0;
    const bool b = inB > 0;
    switch ( op ) {
        case planar_boolean_op_t::UNION: return a || b;
        case planar_boolean_op_t::INTERSECTION: return a && b;
        case planar_boolean_op_t::DIFFERENCE: return a && !b;
        case planar_boolean_op_t::SYMMETRIC_DIFFERENCE: return a != b;
    }
    return false;
}

} // namespace

geometry_status_t Planar_TryOverlay(
    const planar_region_t *pA,
    const planar_region_t *pB,
    planar_boolean_op_t op,
    const geometry_policy_t &policy,
    geometry_source_id_t resultRegionId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    planar_region_t *pOut,
    planar_overlay_stats_t *pStatsOut ) noexcept
{
    if ( pA == nullptr || pB == nullptr || pOut == nullptr ||
         pIdAllocator == nullptr || !Allocator_IsValid( pAllocator ) ||
         !GeometrySourceId_IsValid( resultRegionId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !PlanarRegion_IsInitialized( pA ) || !PlanarRegion_IsInitialized( pB ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( PlanarRegion_IsInitialized( pOut ) ||
         !FramesIdentical( pA->frame, pB->frame ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    planar_overlay_stats_t stats{};

    // ---- 1. Oriented input edges -----------------------------------------
    const usize cEdgesIn = pA->points.nCount + pB->points.nCount;
    if ( cEdgesIn > kPlanarOverlayEdgesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    buf_t<in_edge_t> edges;
    if ( !edges.Init( pAllocator, cEdgesIn ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    auto collect = [&]( const planar_region_t *pR, u8 owner ) noexcept {
        for ( usize c = 0u; c < pR->contours.nCount; ++c ) {
            const span_t<const math::vec2d_t> ring = PlanarRegion_ContourPoints( pR, c );
            for ( usize i = 0u; i < ring.nCount; ++i ) {
                edges.Push( in_edge_t{ ring.pData[i],
                                       ring.pData[( i + 1u ) % ring.nCount], owner } );
            }
        }
    };
    collect( pA, 0u );
    collect( pB, 1u );
    if ( !edges.ok ) { return geometry_status_t::ALLOCATION_FAILED; }
    const usize cE = edges.Size();
    stats.cInputEdges = static_cast<u32>( cE );

    // ---- 2. Split points --------------------------------------------------
    buf_t<split_t> splits;
    if ( !splits.Init( pAllocator, cE * 2u + 16u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cE; ++i ) {
        splits.Push( split_t{ static_cast<u32>( i ), 0.0, edges[i].p0, 1u, 0u } );
        splits.Push( split_t{ static_cast<u32>( i ), 1.0, edges[i].p1, 1u, 0u } );
    }
    for ( usize i = 0u; i < cE; ++i ) {
        const in_edge_t &ei = edges[i];
        const f64 iMinX = std::fmin( ei.p0.x, ei.p1.x ), iMaxX = std::fmax( ei.p0.x, ei.p1.x );
        const f64 iMinY = std::fmin( ei.p0.y, ei.p1.y ), iMaxY = std::fmax( ei.p0.y, ei.p1.y );
        for ( usize j = i + 1u; j < cE; ++j ) {
            const in_edge_t &ej = edges[j];
            if ( std::fmax( ej.p0.x, ej.p1.x ) < iMinX || std::fmin( ej.p0.x, ej.p1.x ) > iMaxX ||
                 std::fmax( ej.p0.y, ej.p1.y ) < iMinY || std::fmin( ej.p0.y, ej.p1.y ) > iMaxY ) {
                continue;
            }
            const planar_segment_relation_t rel =
                Planar_ClassifySegments( ei.p0, ei.p1, ej.p0, ej.p1 );
            if ( rel == planar_segment_relation_t::DISJOINT ) { continue; }
            if ( rel == planar_segment_relation_t::PROPER ) {
                f64 ta = 0.0, tb = 0.0;
                math::vec2d_t p{};
                if ( Planar_TryIntersectProper( ei.p0, ei.p1, ej.p0, ej.p1, &ta, &tb, &p ) ) {
                    splits.Push( split_t{ static_cast<u32>( i ), ta, p, 0u, 0u } );
                    splits.Push( split_t{ static_cast<u32>( j ), tb, p, 0u, 0u } );
                }
                continue;
            }
            // Touch or collinear overlap: every contact point is an input
            // endpoint, so the split is exact.
            const math::vec2d_t jEnds[2] = { ej.p0, ej.p1 };
            const math::vec2d_t iEnds[2] = { ei.p0, ei.p1 };
            for ( const math::vec2d_t &q : jEnds ) {
                if ( !SamePos( q, ei.p0 ) && !SamePos( q, ei.p1 ) &&
                     Planar_PointOnSegment( ei.p0, ei.p1, q ) ) {
                    splits.Push( split_t{ static_cast<u32>( i ), ParamOnEdge( ei, q ), q, 1u, 0u } );
                }
            }
            for ( const math::vec2d_t &q : iEnds ) {
                if ( !SamePos( q, ej.p0 ) && !SamePos( q, ej.p1 ) &&
                     Planar_PointOnSegment( ej.p0, ej.p1, q ) ) {
                    splits.Push( split_t{ static_cast<u32>( j ), ParamOnEdge( ej, q ), q, 1u, 0u } );
                }
            }
        }
    }
    if ( !splits.ok ) { return geometry_status_t::ALLOCATION_FAILED; }
    const usize cS = splits.Size();

    // ---- 3. Vertex merging -------------------------------------------------
    // Union-find over split points. Identical positions always merge; a
    // constructed point also merges with anything within eps. Two distinct
    // exact input points must never merge (that would move authored
    // geometry), so such a cluster aborts the operation.
    const f64 eps = policy.numerical.fAbsoluteDistanceTolerance;
    buf_t<u32> order, parent;
    if ( !order.Init( pAllocator, cS ) || !parent.Init( pAllocator, cS ) ||
         !order.Resize( cS ) || !parent.Resize( cS ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize s = 0u; s < cS; ++s ) {
        order[s] = static_cast<u32>( s );
        parent[s] = static_cast<u32>( s );
    }
    split_t *sp = splits.Data();
    std::sort( order.Data(), order.Data() + cS, [sp]( u32 a, u32 b ) {
        if ( sp[a].p.x != sp[b].p.x ) { return sp[a].p.x < sp[b].p.x; }
        if ( sp[a].p.y != sp[b].p.y ) { return sp[a].p.y < sp[b].p.y; }
        return a < b;
    } );
    for ( usize oi = 0u; oi < cS; ++oi ) {
        const split_t &a = sp[order[oi]];
        for ( usize oj = oi + 1u; oj < cS; ++oj ) {
            const split_t &b = sp[order[oj]];
            if ( b.p.x - a.p.x > eps ) { break; }
            const bool same = SamePos( a.p, b.p );
            if ( !same ) {
                if ( a.exact && b.exact ) { continue; }
                if ( std::fabs( b.p.y - a.p.y ) > eps ) { continue; }
            }
            const u32 ra = FindRoot( parent.Data(), order[oi] );
            const u32 rb = FindRoot( parent.Data(), order[oj] );
            if ( ra != rb ) { parent[rb > ra ? rb : ra] = rb > ra ? ra : rb; }
        }
    }
    // Dense vertex ids in sorted order; representative = first exact member
    // in sorted order, else the first member.
    buf_t<u32> rootToVertex;
    buf_t<math::vec2d_t> vpos;
    buf_t<u8> vexact;
    if ( !rootToVertex.Init( pAllocator, cS ) || !rootToVertex.Resize( cS ) ||
         !vpos.Init( pAllocator, cS ) || !vexact.Init( pAllocator, cS ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize s = 0u; s < cS; ++s ) { rootToVertex[s] = CY_INVALID_INDEX; }
    for ( usize oi = 0u; oi < cS; ++oi ) {
        const u32 s = order[oi];
        const u32 r = FindRoot( parent.Data(), s );
        if ( rootToVertex[r] == CY_INVALID_INDEX ) {
            rootToVertex[r] = static_cast<u32>( vpos.Size() );
            vpos.Push( sp[s].p );
            vexact.Push( sp[s].exact );
        } else {
            const u32 v = rootToVertex[r];
            if ( sp[s].exact ) {
                if ( vexact[v] && !SamePos( vpos[v], sp[s].p ) ) {
                    return geometry_status_t::NUMERIC_FAILURE;
                }
                if ( !vexact[v] ) { vpos[v] = sp[s].p; vexact[v] = 1u; }
            }
        }
        sp[s].iVertex = rootToVertex[r];
    }
    if ( !vpos.ok || !vexact.ok ) { return geometry_status_t::ALLOCATION_FAILED; }
    const usize cV = vpos.Size();
    stats.cArrangementVertices = static_cast<u32>( cV );

    // ---- 4. Sub-edges and unique edges ------------------------------------
    std::sort( sp, sp + cS, []( const split_t &a, const split_t &b ) {
        if ( a.iEdge != b.iEdge ) { return a.iEdge < b.iEdge; }
        return a.t < b.t;
    } );
    buf_t<sub_edge_t> subs;
    if ( !subs.Init( pAllocator, cS ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize s = 0u; s + 1u < cS; ++s ) {
        if ( sp[s].iEdge != sp[s + 1u].iEdge ) { continue; }
        const u32 u = sp[s].iVertex;
        const u32 v = sp[s + 1u].iVertex;
        if ( u == v ) { continue; }
        sub_edge_t se{};
        se.lo = u < v ? u : v;
        se.hi = u < v ? v : u;
        const i32 dir = u < v ? 1 : -1;
        if ( edges[sp[s].iEdge].owner == 0u ) { se.dirA = dir; } else { se.dirB = dir; }
        subs.Push( se );
    }
    if ( !subs.ok ) { return geometry_status_t::ALLOCATION_FAILED; }
    std::sort( subs.Data(), subs.Data() + subs.Size(),
               []( const sub_edge_t &a, const sub_edge_t &b ) {
                   return a.lo != b.lo ? a.lo < b.lo : a.hi < b.hi;
               } );
    buf_t<edge_t> uedges;
    if ( !uedges.Init( pAllocator, subs.Size() + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize s = 0u; s < subs.Size(); ++s ) {
        if ( uedges.Size() > 0u && uedges[uedges.Size() - 1u].lo == subs[s].lo &&
             uedges[uedges.Size() - 1u].hi == subs[s].hi ) {
            uedges[uedges.Size() - 1u].aSide += subs[s].dirA;
            uedges[uedges.Size() - 1u].bSide += subs[s].dirB;
        } else {
            uedges.Push( edge_t{ subs[s].lo, subs[s].hi, subs[s].dirA, subs[s].dirB } );
        }
    }
    if ( !uedges.ok ) { return geometry_status_t::ALLOCATION_FAILED; }
    const usize cUE = uedges.Size();
    const usize cH = cUE * 2u;
    stats.cArrangementEdges = static_cast<u32>( cUE );

    auto heOrigin = [&]( u32 h ) noexcept { return ( h & 1u ) ? uedges[h >> 1].hi : uedges[h >> 1].lo; };
    auto heDest = [&]( u32 h ) noexcept { return ( h & 1u ) ? uedges[h >> 1].lo : uedges[h >> 1].hi; };
    auto heASide = [&]( u32 h ) noexcept { return ( h & 1u ) ? -uedges[h >> 1].aSide : uedges[h >> 1].aSide; };
    auto heBSide = [&]( u32 h ) noexcept { return ( h & 1u ) ? -uedges[h >> 1].bSide : uedges[h >> 1].bSide; };

    // ---- 5. Rotation system -------------------------------------------------
    buf_t<u32> outOrder, outPos, vFirst, heNext, heFace;
    if ( !outOrder.Init( pAllocator, cH ) || !outOrder.Resize( cH ) ||
         !outPos.Init( pAllocator, cH ) || !outPos.Resize( cH ) ||
         !vFirst.Init( pAllocator, cV + 1u ) || !vFirst.Resize( cV + 1u ) ||
         !heNext.Init( pAllocator, cH ) || !heNext.Resize( cH ) ||
         !heFace.Init( pAllocator, cH ) || !heFace.Resize( cH ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize h = 0u; h < cH; ++h ) { outOrder[h] = static_cast<u32>( h ); }
    const math::vec2d_t *vp = vpos.Data();
    std::sort( outOrder.Data(), outOrder.Data() + cH, [&]( u32 a, u32 b ) {
        const u32 oa = heOrigin( a ), ob = heOrigin( b );
        if ( oa != ob ) { return oa < ob; }
        const math::vec2d_t o = vp[oa];
        const math::vec2d_t da = vp[heDest( a )], db = vp[heDest( b )];
        const int ha = HalfPlane( o, da ), hb = HalfPlane( o, db );
        if ( ha != hb ) { return ha < hb; }
        const i32 s = math::Orient2D( o, da, db );
        if ( s != 0 ) { return s > 0; }
        return a < b; // collinear same direction cannot occur after dedupe
    } );
    for ( usize v = 0u; v <= cV; ++v ) { vFirst[v] = static_cast<u32>( cH ); }
    for ( usize k = cH; k-- > 0u; ) {
        vFirst[heOrigin( outOrder[k] )] = static_cast<u32>( k );
        outPos[outOrder[k]] = static_cast<u32>( k );
    }
    // vFirst[v] for vertices with no edges stays cH; fix the end markers so
    // [vFirst[v], vEnd(v)) is each vertex's CCW fan.
    auto vEnd = [&]( u32 v ) noexcept {
        u32 k = vFirst[v];
        while ( k < cH && heOrigin( outOrder[k] ) == v ) { ++k; }
        return k;
    };
    for ( usize h = 0u; h < cH; ++h ) {
        const u32 tw = static_cast<u32>( h ) ^ 1u;
        const u32 v = heOrigin( tw );
        const u32 first = vFirst[v];
        const u32 last = vEnd( v );
        const u32 pos = outPos[tw];
        const u32 prev = pos == first ? last - 1u : pos - 1u;
        heNext[h] = outOrder[prev];
    }

    // ---- 6. Faces -------------------------------------------------------------
    for ( usize h = 0u; h < cH; ++h ) { heFace[h] = CY_INVALID_INDEX; }
    u32 cFaces = 0u;
    buf_t<f64> faceArea;
    if ( !faceArea.Init( pAllocator, cH / 2u + 2u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize h0 = 0u; h0 < cH; ++h0 ) {
        if ( heFace[h0] != CY_INVALID_INDEX ) { continue; }
        f64 twice = 0.0;
        u32 h = static_cast<u32>( h0 );
        usize guard = 0u;
        do {
            heFace[h] = cFaces;
            const math::vec2d_t a = vp[heOrigin( h )];
            const math::vec2d_t b = vp[heDest( h )];
            twice += a.x * b.y - a.y * b.x;
            h = heNext[h];
            if ( ++guard > cH ) { return geometry_status_t::CORRUPT_STATE; }
        } while ( h != h0 );
        faceArea.Push( 0.5 * twice );
        ++cFaces;
    }
    if ( !faceArea.ok ) { return geometry_status_t::ALLOCATION_FAILED; }
    stats.cFaces = cFaces;

    // ---- 7. Classification -----------------------------------------------------
    buf_t<i8> inA, inB;
    if ( !inA.Init( pAllocator, cFaces ) || !inA.Resize( cFaces ) ||
         !inB.Init( pAllocator, cFaces ) || !inB.Resize( cFaces ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 f = 0u; f < cFaces; ++f ) { inA[f] = -1; inB[f] = -1; }
    for ( usize h = 0u; h < cH; ++h ) {
        const i32 a = heASide( static_cast<u32>( h ) );
        const i32 b = heBSide( static_cast<u32>( h ) );
        if ( a != 0 ) { inA[heFace[h]] = a > 0 ? 1 : 0; }
        if ( b != 0 ) { inB[heFace[h]] = b > 0 ? 1 : 0; }
    }
    // Propagate across edges that are not boundaries of the operand.
    for ( bool changed = true; changed; ) {
        changed = false;
        for ( usize h = 0u; h < cH; ++h ) {
            const u32 f = heFace[h], g = heFace[h ^ 1u];
            if ( heASide( static_cast<u32>( h ) ) == 0 && inA[f] >= 0 && inA[g] < 0 ) {
                inA[g] = inA[f]; changed = true;
            }
            if ( heBSide( static_cast<u32>( h ) ) == 0 && inB[f] >= 0 && inB[g] < 0 ) {
                inB[g] = inB[f]; changed = true;
            }
        }
    }
    // Fallback for faces in components that never touch the operand.
    for ( usize h = 0u; h < cH; ++h ) {
        const u32 f = heFace[h];
        const math::vec2d_t p = vp[heOrigin( static_cast<u32>( h ) )];
        if ( inA[f] < 0 ) {
            const planar_containment_t c = PlanarRegion_Contains( pA, p );
            if ( c != planar_containment_t::BOUNDARY ) {
                inA[f] = c == planar_containment_t::INSIDE ? 1 : 0;
            }
        }
        if ( inB[f] < 0 ) {
            const planar_containment_t c = PlanarRegion_Contains( pB, p );
            if ( c != planar_containment_t::BOUNDARY ) {
                inB[f] = c == planar_containment_t::INSIDE ? 1 : 0;
            }
        }
    }

    buf_t<u8> sel;
    if ( !sel.Init( pAllocator, cFaces ) || !sel.Resize( cFaces ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 f = 0u; f < cFaces; ++f ) {
        sel[f] = Selected( op, inA[f], inB[f] ) ? 1u : 0u;
        if ( sel[f] ) { ++stats.cSelectedFaces; }
    }

    // ---- 8. Boundary loops -------------------------------------------------------
    auto isBoundary = [&]( u32 h ) noexcept {
        return sel[heFace[h]] != 0u && sel[heFace[h ^ 1u]] == 0u;
    };
    buf_t<u8> visited;
    buf_t<math::vec2d_t> loopPts;
    buf_t<loop_t> loops;
    if ( !visited.Init( pAllocator, cH ) || !visited.Resize( cH ) ||
         !loopPts.Init( pAllocator, cH + 1u ) || !loops.Init( pAllocator, 16u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize h = 0u; h < cH; ++h ) { visited[h] = 0u; }
    buf_t<math::vec2d_t> raw;
    if ( !raw.Init( pAllocator, 64u ) ) { return geometry_status_t::ALLOCATION_FAILED; }

    for ( usize h0 = 0u; h0 < cH; ++h0 ) {
        if ( visited[h0] || !isBoundary( static_cast<u32>( h0 ) ) ) { continue; }
        Vector_Clear( &raw.v );
        u32 h = static_cast<u32>( h0 );
        usize guard = 0u;
        do {
            visited[h] = 1u;
            raw.Push( vp[heOrigin( h )] );
            // Rotate clockwise around dest until the next boundary half-edge.
            u32 g = heNext[h];
            usize spin = 0u;
            while ( !isBoundary( g ) ) {
                g = heNext[g ^ 1u];
                if ( ++spin > cH ) { return geometry_status_t::CORRUPT_STATE; }
            }
            h = g;
            if ( ++guard > cH ) { return geometry_status_t::CORRUPT_STATE; }
        } while ( h != h0 );
        if ( !raw.ok ) { return geometry_status_t::ALLOCATION_FAILED; }

        // Drop collinear pass-through vertices (both neighbours on one line
        // through it). Repeat until stable; each pass removes at least one
        // vertex or stops, so this terminates in O(n^2) worst case.
        usize n = raw.Size();
        for ( bool removed = true; removed && n > 3u; ) {
            removed = false;
            for ( usize i = 0u; i < n && n > 3u; ++i ) {
                const math::vec2d_t a = raw[( i + n - 1u ) % n];
                const math::vec2d_t b = raw[i];
                const math::vec2d_t c = raw[( i + 1u ) % n];
                if ( math::Orient2D( a, b, c ) == 0 ) {
                    for ( usize k = i; k + 1u < n; ++k ) { raw[k] = raw[k + 1u]; }
                    --n;
                    removed = true;
                }
            }
        }
        if ( n < 3u ) { continue; }

        // Rotate to start at the lexicographically smallest vertex so the
        // output does not depend on half-edge numbering.
        usize start = 0u;
        for ( usize i = 1u; i < n; ++i ) {
            if ( Planar_PointLess( raw[i], raw[start] ) ) { start = i; }
        }
        loop_t loop{};
        loop.iFirst = static_cast<u32>( loopPts.Size() );
        loop.cPoints = static_cast<u32>( n );
        for ( usize i = 0u; i < n; ++i ) { loopPts.Push( raw[( start + i ) % n] ); }
        loop.area = Planar_RingSignedArea(
            span_t<const math::vec2d_t>{ loopPts.Data() + loop.iFirst, n } );
        loops.Push( loop );
    }
    if ( !loopPts.ok || !loops.ok ) { return geometry_status_t::ALLOCATION_FAILED; }

    // ---- 9. Holes -> outers ----------------------------------------------------
    auto ringOf = [&]( const loop_t &l ) noexcept {
        return span_t<const math::vec2d_t>{ loopPts.Data() + l.iFirst, l.cPoints };
    };
    for ( usize i = 0u; i < loops.Size(); ++i ) {
        if ( loops[i].area >= 0.0 ) { continue; }
        i32 best = -1;
        for ( usize j = 0u; j < loops.Size(); ++j ) {
            if ( loops[j].area <= 0.0 ) { continue; }
            // Hole and outer boundaries share no edge, but may share a
            // vertex; probe vertices until one is strictly inside/outside.
            planar_containment_t c = planar_containment_t::BOUNDARY;
            const span_t<const math::vec2d_t> hr = ringOf( loops[i] );
            for ( usize k = 0u; k < hr.nCount && c == planar_containment_t::BOUNDARY; ++k ) {
                c = Planar_RingContains( ringOf( loops[j] ), hr.pData[k] );
            }
            if ( c == planar_containment_t::INSIDE &&
                 ( best < 0 || loops[j].area < loops[static_cast<usize>( best )].area ) ) {
                best = static_cast<i32>( j );
            }
        }
        if ( best < 0 ) { return geometry_status_t::NUMERIC_FAILURE; }
        loops[i].iOwner = best;
    }

    // ---- 10. Publish -------------------------------------------------------------
    // Deterministic polygon order: outers by their first (smallest) vertex.
    buf_t<u32> outerIdx;
    if ( !outerIdx.Init( pAllocator, loops.Size() + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < loops.Size(); ++i ) {
        if ( loops[i].area > 0.0 ) { outerIdx.Push( static_cast<u32>( i ) ); }
    }
    if ( !outerIdx.ok ) { return geometry_status_t::ALLOCATION_FAILED; }
    loop_t *lp = loops.Data();
    math::vec2d_t *pp = loopPts.Data();
    std::sort( outerIdx.Data(), outerIdx.Data() + outerIdx.Size(), [lp, pp]( u32 a, u32 b ) {
        return Planar_PointLess( pp[lp[a].iFirst], pp[lp[b].iFirst] );
    } );

    geometry_source_id_allocator_t ids = *pIdAllocator; // publish on success
    planar_region_t result{};
    geometry_status_t s = PlanarRegion_Init( &result, pAllocator, pA->frame, resultRegionId );
    if ( s != geometry_status_t::OK ) { return s; }

    auto nextId = [&]( geometry_source_id_t *pId ) noexcept {
        const geometry_source_id_result_t r = GeometrySourceIdAllocator_Allocate( &ids );
        *pId = r.id;
        return r.status;
    };
    for ( usize oi = 0u; oi < outerIdx.Size() && s == geometry_status_t::OK; ++oi ) {
        const u32 o = outerIdx[oi];
        geometry_source_id_t polyId{}, contourId{};
        if ( ( s = nextId( &polyId ) ) != geometry_status_t::OK ) { break; }
        if ( ( s = nextId( &contourId ) ) != geometry_status_t::OK ) { break; }
        s = PlanarRegion_TryAddPolygon( &result, polyId, contourId, ringOf( loops[o] ), nullptr );
        ++stats.cOutputPolygons;
        // Holes of this outer, in loop discovery order sorted by first vertex.
        for ( usize pass = 0u; pass < loops.Size() && s == geometry_status_t::OK; ++pass ) {
            // Selection sort over this outer's holes keeps it allocation-free.
            i32 pick = -1;
            for ( usize i = 0u; i < loops.Size(); ++i ) {
                if ( loops[i].iOwner != static_cast<i32>( o ) ) { continue; }
                if ( pick < 0 ||
                     Planar_PointLess( pp[lp[i].iFirst],
                                       pp[lp[static_cast<usize>( pick )].iFirst] ) ) {
                    pick = static_cast<i32>( i );
                }
            }
            if ( pick < 0 ) { break; }
            geometry_source_id_t holeId{};
            if ( ( s = nextId( &holeId ) ) != geometry_status_t::OK ) { break; }
            s = PlanarRegion_TryAddHole( &result, holeId,
                                         ringOf( loops[static_cast<usize>( pick )] ) );
            loops[static_cast<usize>( pick )].iOwner = -2; // consumed
            ++stats.cOutputHoles;
        }
    }
    if ( s != geometry_status_t::OK ) {
        PlanarRegion_Shutdown( &result );
        return s;
    }

    // Snap-rounding guard: never publish an invalid region.
    if ( PlanarRegion_Validate( &result, policy ).status != geometry_status_t::OK ) {
        PlanarRegion_Shutdown( &result );
        return geometry_status_t::NUMERIC_FAILURE;
    }

    // planar_region_t holds vector_t members (non-copyable); move them.
    pOut->frame = result.frame;
    pOut->sourceId = result.sourceId;
    Vector_Move( &pOut->points, &result.points );
    Vector_Move( &pOut->contours, &result.contours );
    Vector_Move( &pOut->polygons, &result.polygons );
    *pIdAllocator = ids;
    if ( pStatsOut != nullptr ) { *pStatsOut = stats; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
