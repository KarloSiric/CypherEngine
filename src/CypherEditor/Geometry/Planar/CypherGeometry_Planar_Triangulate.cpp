//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Triangulate.cpp
//  Purpose: Implements hole bridging and ear clipping for PlanarRegion.
//  Details: The working ring is a list of region point indices. After
//           bridging it is weakly simple: each bridge appears as two
//           coincident, opposite edges and each bridge endpoint appears
//           twice. The ear test therefore ignores other ring entries that
//           are *positionally* equal to the candidate triangle's corners —
//           those are the bridge duplicates, not intruding vertices.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Planar_Triangulate.h"
#include "CypherGeometry_Planar_Segment.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Scratch vector that frees itself; every early return stays leak-free.
template <typename type_t>
struct scratch_vector_t {
    vector_t<type_t> v{};
    ~scratch_vector_t() { Vector_Shutdown( &v ); }
};

bool SamePos( math::vec2d_t a, math::vec2d_t b ) noexcept
{
    return a.x == b.x && a.y == b.y;
}

bool Left( math::vec2d_t a, math::vec2d_t b, math::vec2d_t c ) noexcept
{
    return math::Orient2D( a, b, c ) > 0;
}

bool LeftOn( math::vec2d_t a, math::vec2d_t b, math::vec2d_t c ) noexcept
{
    return math::Orient2D( a, b, c ) >= 0;
}

// O'Rourke's InCone: is direction a->b strictly inside the interior angle
// at a, where the ring runs a0 -> a -> a1 with the interior on the left?
bool InCone( math::vec2d_t a0, math::vec2d_t a, math::vec2d_t a1,
             math::vec2d_t b ) noexcept
{
    if ( LeftOn( a, a1, a0 ) ) {                  // convex corner
        return Left( a, b, a0 ) && Left( b, a, a1 );
    }
    return !( LeftOn( a, b, a1 ) && LeftOn( b, a, a0 ) ); // reflex corner
}

// True when segment p-q crosses or touches any edge of `ring` (point
// indices into pts), except edges incident to the ring positions listed in
// `allow` (the bridge endpoints themselves). Touching a vertex that is not
// an allowed endpoint also counts, because the channel would pass through it.
bool SegmentBlockedByRing(
    const math::vec2d_t *pts,
    const u32 *ring,
    usize cRing,
    math::vec2d_t p,
    math::vec2d_t q ) noexcept
{
    for ( usize i = 0u; i < cRing; ++i ) {
        const math::vec2d_t e0 = pts[ring[i]];
        const math::vec2d_t e1 = pts[ring[( i + 1u ) % cRing]];
        const planar_segment_relation_t rel =
            Planar_ClassifySegments( p, q, e0, e1 );
        if ( rel == planar_segment_relation_t::DISJOINT ) { continue; }
        if ( rel == planar_segment_relation_t::PROPER ||
             rel == planar_segment_relation_t::COLLINEAR_OVERLAP ) {
            return true;
        }
        // TOUCH: acceptable only when the contact is at p or q and that
        // endpoint is an endpoint of this edge (edge incident to it).
        const bool atP = SamePos( e0, p ) || SamePos( e1, p );
        const bool atQ = SamePos( e0, q ) || SamePos( e1, q );
        if ( !atP && !atQ ) { return true; }
        // Edge incident to p or q: the touch must be only at that shared
        // point, i.e. the edge's other endpoint must not lie on p-q.
        const math::vec2d_t other =
            ( SamePos( e0, p ) || SamePos( e0, q ) ) ? e1 : e0;
        if ( !SamePos( other, p ) && !SamePos( other, q ) &&
             Planar_PointOnSegment( p, q, other ) ) {
            return true;
        }
    }
    return false;
}

struct hole_ref_t {
    u32 iContour{ 0u };
    u32 iMaxLocal{ 0u }; // local index of the lexicographically max vertex
};

// Merges one hole into `ring` (in place, growing it). Returns false when no
// valid bridge exists.
bool BridgeHole(
    const planar_region_t *pRegion,
    const hole_ref_t &hole,
    const hole_ref_t *pRemaining,
    usize cRemaining,
    vector_t<u32> *pRing,
    vector_t<u32> *pScratchRing ) noexcept
{
    const math::vec2d_t *pts = pRegion->points.pData;
    const planar_region_contour_t &hc = pRegion->contours.pData[hole.iContour];
    const u32 iM = hc.iFirstPoint + hole.iMaxLocal;
    const math::vec2d_t M = pts[iM];
    const u32 iHolePrev = hc.iFirstPoint + ( hole.iMaxLocal + hc.cPoints - 1u ) % hc.cPoints;
    const u32 iHoleNext = hc.iFirstPoint + ( hole.iMaxLocal + 1u ) % hc.cPoints;

    // Touching hole: a hole vertex that already lies on the current ring is
    // a zero-width connection, and a searched bridge might not exist there
    // (the contact blocks every channel through it). Splice at the contact:
    //   ring[..pos], [contact on ring if mid-edge], hole after k .. k, ring[pos+1..]
    // The first contact in hole order is used; any further contacts of the
    // same hole stay as pinch points, which the ear test tolerates.
    {
        const usize cRingNow = pRing->nCount;
        for ( u32 k = 0u; k < hc.cPoints; ++k ) {
            const u32 iHk = hc.iFirstPoint + k;
            const math::vec2d_t Hk = pts[iHk];
            usize pos = static_cast<usize>( -1 );
            bool bMidEdge = false;
            for ( usize r = 0u; r < cRingNow && pos == static_cast<usize>( -1 ); ++r ) {
                const math::vec2d_t R0 = pts[pRing->pData[r]];
                const math::vec2d_t R1 = pts[pRing->pData[( r + 1u ) % cRingNow]];
                if ( SamePos( R0, Hk ) ) { pos = r; }
                else if ( !SamePos( R1, Hk ) && Planar_PointOnSegment( R0, R1, Hk ) ) {
                    pos = r;
                    bMidEdge = true;
                }
            }
            if ( pos == static_cast<usize>( -1 ) ) { continue; }

            Vector_Clear( pScratchRing );
            for ( usize i = 0u; i <= pos; ++i ) {
                (void)Vector_PushBack( pScratchRing, pRing->pData[i] );
            }
            if ( bMidEdge ) { (void)Vector_PushBack( pScratchRing, iHk ); }
            for ( u32 s = 1u; s <= hc.cPoints; ++s ) {
                (void)Vector_PushBack( pScratchRing,
                                       hc.iFirstPoint + ( k + s ) % hc.cPoints );
            }
            for ( usize i = pos + 1u; i < cRingNow; ++i ) {
                (void)Vector_PushBack( pScratchRing, pRing->pData[i] );
            }
            Vector_Clear( pRing );
            for ( usize i = 0u; i < pScratchRing->nCount; ++i ) {
                (void)Vector_PushBack( pRing, pScratchRing->pData[i] );
            }
            return true;
        }
    }

    // Contour index lists for the hole itself and not-yet-merged holes, so
    // the channel is checked against every boundary that still exists.
    usize bestPos = static_cast<usize>( -1 );
    f64 bestD2 = 0.0;
    const usize cRing = pRing->nCount;
    for ( usize pos = 0u; pos < cRing; ++pos ) {
        const u32 iV = pRing->pData[pos];
        const math::vec2d_t V = pts[iV];
        if ( SamePos( V, M ) ) { continue; }
        const math::vec2d_t Vprev = pts[pRing->pData[( pos + cRing - 1u ) % cRing]];
        const math::vec2d_t Vnext = pts[pRing->pData[( pos + 1u ) % cRing]];
        if ( !InCone( Vprev, V, Vnext, M ) ) { continue; }
        if ( !InCone( pts[iHolePrev], M, pts[iHoleNext], V ) ) { continue; }
        if ( SegmentBlockedByRing( pts, pRing->pData, cRing, M, V ) ) { continue; }

        // The hole's own ring and every remaining hole.
        bool blocked = false;
        auto checkContour = [&]( u32 iContour ) noexcept {
            const planar_region_contour_t &c = pRegion->contours.pData[iContour];
            for ( u32 k = 0u; k < c.cPoints && !blocked; ++k ) {
                const math::vec2d_t e0 = pts[c.iFirstPoint + k];
                const math::vec2d_t e1 = pts[c.iFirstPoint + ( k + 1u ) % c.cPoints];
                const planar_segment_relation_t rel =
                    Planar_ClassifySegments( M, V, e0, e1 );
                if ( rel == planar_segment_relation_t::DISJOINT ) { continue; }
                if ( rel != planar_segment_relation_t::TOUCH_ENDPOINT ) {
                    blocked = true;
                    break;
                }
                const bool incidentM = SamePos( e0, M ) || SamePos( e1, M );
                if ( !incidentM ) { blocked = true; break; }
                const math::vec2d_t other = SamePos( e0, M ) ? e1 : e0;
                if ( Planar_PointOnSegment( M, V, other ) ) { blocked = true; }
            }
        };
        checkContour( hole.iContour );
        for ( usize r = 0u; r < cRemaining && !blocked; ++r ) {
            checkContour( pRemaining[r].iContour );
        }
        if ( blocked ) { continue; }

        const math::vec2d_t d = math::Vec2d_Subtract( V, M );
        const f64 d2 = d.x * d.x + d.y * d.y;
        // Nearest valid target, ties to the lowest ring position: purely a
        // quality/determinism choice, validity was already decided exactly.
        if ( bestPos == static_cast<usize>( -1 ) || d2 < bestD2 ) {
            bestPos = pos;
            bestD2 = d2;
        }
    }
    if ( bestPos == static_cast<usize>( -1 ) ) { return false; }

    // ring' = ring[0..best] , M, hole (from M's successor round to M), V , ring[best+1..]
    Vector_Clear( pScratchRing );
    for ( usize i = 0u; i <= bestPos; ++i ) {
        (void)Vector_PushBack( pScratchRing, pRing->pData[i] );
    }
    for ( u32 k = 0u; k <= hc.cPoints; ++k ) {
        (void)Vector_PushBack( pScratchRing,
                               hc.iFirstPoint + ( hole.iMaxLocal + k ) % hc.cPoints );
    }
    (void)Vector_PushBack( pScratchRing, pRing->pData[bestPos] );
    for ( usize i = bestPos + 1u; i < cRing; ++i ) {
        (void)Vector_PushBack( pScratchRing, pRing->pData[i] );
    }
    Vector_Clear( pRing );
    for ( usize i = 0u; i < pScratchRing->nCount; ++i ) {
        (void)Vector_PushBack( pRing, pScratchRing->pData[i] );
    }
    return true;
}

// Ear clipping over a (weakly) simple CCW ring of point indices.
bool ClipEars(
    const math::vec2d_t *pts,
    vector_t<u32> *pRing,
    u32 iPolygon,
    vector_t<planar_triangle_t> *pOut ) noexcept
{
    u32 *ring = pRing->pData;
    usize n = pRing->nCount;
    while ( n > 3u ) {
        bool clipped = false;
        for ( usize k = 0u; k < n; ++k ) {
            const u32 iP = ring[( k + n - 1u ) % n];
            const u32 iT = ring[k];
            const u32 iN = ring[( k + 1u ) % n];
            const math::vec2d_t P = pts[iP];
            const math::vec2d_t T = pts[iT];
            const math::vec2d_t N = pts[iN];
            if ( !Left( P, T, N ) ) { continue; } // reflex or collinear tip

            bool blocked = false;
            for ( usize m = 0u; m < n && !blocked; ++m ) {
                const math::vec2d_t R = pts[ring[m]];
                if ( SamePos( R, P ) || SamePos( R, T ) || SamePos( R, N ) ) {
                    continue;
                }
                if ( LeftOn( P, T, R ) && LeftOn( T, N, R ) && LeftOn( N, P, R ) ) {
                    blocked = true;
                }
            }
            if ( !blocked ) {
                // Weakly simple rings can hide a crossing behind a bridge;
                // reject any diagonal P-N that properly crosses a ring edge.
                for ( usize m = 0u; m < n && !blocked; ++m ) {
                    const math::vec2d_t e0 = pts[ring[m]];
                    const math::vec2d_t e1 = pts[ring[( m + 1u ) % n]];
                    if ( Planar_ClassifySegments( P, N, e0, e1 ) ==
                         planar_segment_relation_t::PROPER ) {
                        blocked = true;
                    }
                }
            }
            if ( blocked ) { continue; }

            (void)Vector_PushBack( pOut, planar_triangle_t{ iP, iT, iN, iPolygon } );
            for ( usize m = k; m + 1u < n; ++m ) { ring[m] = ring[m + 1u]; }
            --n;
            clipped = true;
            break;
        }
        if ( !clipped ) { return false; }
    }
    if ( !Left( pts[ring[0]], pts[ring[1]], pts[ring[2]] ) ) { return false; }
    (void)Vector_PushBack( pOut, planar_triangle_t{ ring[0], ring[1], ring[2], iPolygon } );
    pRing->nCount = n;
    return true;
}

} // namespace

geometry_status_t Planar_TryTriangulatePolygon(
    const planar_region_t *pRegion,
    usize iPolygon,
    vector_t<planar_triangle_t> *pTrianglesOut ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pTrianglesOut == nullptr || pTrianglesOut->pAllocator == nullptr ||
         iPolygon >= pRegion->polygons.nCount ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const planar_region_polygon_t &poly = pRegion->polygons.pData[iPolygon];
    usize cCorners = 0u;
    for ( u32 c = 0u; c < poly.cContours; ++c ) {
        cCorners += pRegion->contours.pData[poly.iFirstContour + c].cPoints;
    }
    const usize cHoles = poly.cContours - 1u;
    if ( cCorners + 2u * cHoles > kPlanarTriangulateCornersMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // Reserve everything up front; afterwards no push can fail.
    const usize cRingMax = cCorners + 2u * cHoles;
    const usize cTriangles = cCorners + 2u * cHoles - 2u;
    scratch_vector_t<u32> ring, scratch;
    scratch_vector_t<hole_ref_t> holes;
    if ( !Vector_Init( &ring.v, pTrianglesOut->pAllocator, cRingMax ) ||
         !Vector_Init( &scratch.v, pTrianglesOut->pAllocator, cRingMax ) ||
         !Vector_Init( &holes.v, pTrianglesOut->pAllocator, cHoles + 1u ) ||
         !Vector_Reserve( pTrianglesOut, pTrianglesOut->nCount + cTriangles ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const planar_region_contour_t &outer = pRegion->contours.pData[poly.iFirstContour];
    for ( u32 k = 0u; k < outer.cPoints; ++k ) {
        (void)Vector_PushBack( &ring.v, outer.iFirstPoint + k );
    }

    // Holes ordered by their extreme vertex, largest first, ties by contour
    // index: merging rightmost holes first keeps channels short and makes
    // the result independent of hole insertion order for distinct extremes.
    const math::vec2d_t *pts = pRegion->points.pData;
    for ( u32 h = 1u; h < poly.cContours; ++h ) {
        const planar_region_contour_t &hc = pRegion->contours.pData[poly.iFirstContour + h];
        hole_ref_t ref{ poly.iFirstContour + h, 0u };
        for ( u32 k = 1u; k < hc.cPoints; ++k ) {
            if ( Planar_PointLess( pts[hc.iFirstPoint + ref.iMaxLocal],
                                   pts[hc.iFirstPoint + k] ) ) {
                ref.iMaxLocal = k;
            }
        }
        // Insertion sort (hole counts are small).
        usize at = holes.v.nCount;
        (void)Vector_PushBack( &holes.v, ref );
        while ( at > 0u ) {
            const hole_ref_t &prev = holes.v.pData[at - 1u];
            const math::vec2d_t pm = pts[pRegion->contours.pData[prev.iContour].iFirstPoint + prev.iMaxLocal];
            const math::vec2d_t rm = pts[pRegion->contours.pData[ref.iContour].iFirstPoint + ref.iMaxLocal];
            if ( !Planar_PointLess( pm, rm ) ) { break; }
            holes.v.pData[at] = prev;
            holes.v.pData[at - 1u] = ref;
            --at;
        }
    }

    for ( usize h = 0u; h < holes.v.nCount; ++h ) {
        if ( !BridgeHole( pRegion, holes.v.pData[h], holes.v.pData + h + 1u,
                          holes.v.nCount - h - 1u, &ring.v, &scratch.v ) ) {
            return geometry_status_t::DEGENERATE;
        }
    }

    const usize cBefore = pTrianglesOut->nCount;
    if ( !ClipEars( pts, &ring.v, static_cast<u32>( iPolygon ), pTrianglesOut ) ) {
        // Shrinking never allocates, so this cannot fail.
        (void)Vector_Resize( pTrianglesOut, cBefore );
        return geometry_status_t::DEGENERATE;
    }
    return geometry_status_t::OK;
}

geometry_status_t Planar_TryTriangulateRing(
    span_t<const math::vec2d_t> ring,
    const allocator_t *pScratchAllocator,
    vector_t<planar_ring_triangle_t> *pOut ) noexcept
{
    if ( ring.pData == nullptr || ring.nCount < 3u || pOut == nullptr ||
         pOut->pAllocator == nullptr || !Allocator_IsValid( pScratchAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( ring.nCount > kPlanarTriangulateCornersMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const usize n = ring.nCount;
    // Work on a CCW copy so the shared ear clipper's convexity test holds;
    // map indices back so output triangles keep the ring's own winding.
    const bool ccw = Planar_RingSignedArea( ring ) > 0.0;
    scratch_vector_t<math::vec2d_t> pts;
    scratch_vector_t<u32> idx;
    scratch_vector_t<planar_triangle_t> tris;
    if ( !Vector_Init( &pts.v, pScratchAllocator, n ) || !Vector_Init( &idx.v, pScratchAllocator, n ) ||
         !Vector_Init( &tris.v, pScratchAllocator, n ) ||
         !Vector_Reserve( pOut, pOut->nCount + n - 2u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < n; ++i ) {
        const usize src = ccw ? i : n - 1u - i;
        (void)Vector_PushBack( &pts.v, ring.pData[src] );
        (void)Vector_PushBack( &idx.v, static_cast<u32>( i ) );
    }
    if ( !ClipEars( pts.v.pData, &idx.v, 0u, &tris.v ) ) {
        return geometry_status_t::DEGENERATE;
    }
    for ( usize t = 0u; t < tris.v.nCount; ++t ) {
        const planar_triangle_t &tr = tris.v.pData[t];
        auto back = [&]( u32 k ) noexcept { return ccw ? k : static_cast<u32>( n - 1u - k ); };
        // A reversed copy yields triangles CCW in reversed space; swapping
        // two corners restores the input ring's orientation.
        planar_ring_triangle_t out = ccw ? planar_ring_triangle_t{ back( tr.a ), back( tr.b ), back( tr.c ) }
                                         : planar_ring_triangle_t{ back( tr.a ), back( tr.c ), back( tr.b ) };
        (void)Vector_PushBack( pOut, out );
    }
    return geometry_status_t::OK;
}

geometry_status_t Planar_TryTriangulateRegion(
    const planar_region_t *pRegion,
    vector_t<planar_triangle_t> *pTrianglesOut ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pTrianglesOut == nullptr || pTrianglesOut->pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const usize cBefore = pTrianglesOut->nCount;
    for ( usize p = 0u; p < pRegion->polygons.nCount; ++p ) {
        const geometry_status_t s =
            Planar_TryTriangulatePolygon( pRegion, p, pTrianglesOut );
        if ( s != geometry_status_t::OK ) {
            (void)Vector_Resize( pTrianglesOut, cBefore );
            return s;
        }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
