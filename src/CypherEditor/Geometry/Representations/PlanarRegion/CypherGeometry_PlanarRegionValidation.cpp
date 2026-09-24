//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PlanarRegionValidation.cpp
//  Purpose: Implements PlanarRegion validation.
//  Details: Passes run in dependency order: per-contour sanity first
//           (finite, duplicate, area, simplicity) because later passes
//           (orientation, containment) are meaningless on a
//           self-intersecting ring.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PlanarRegionValidation.h"
#include "CypherGeometry_Planar_Segment.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct box2_t {
    math::vec2d_t mn{};
    math::vec2d_t mx{};
};

box2_t RingBox( span_t<const math::vec2d_t> ring ) noexcept
{
    box2_t b{ ring.pData[0], ring.pData[0] };
    for ( usize i = 1u; i < ring.nCount; ++i ) {
        const math::vec2d_t p = ring.pData[i];
        b.mn.x = p.x < b.mn.x ? p.x : b.mn.x;
        b.mn.y = p.y < b.mn.y ? p.y : b.mn.y;
        b.mx.x = p.x > b.mx.x ? p.x : b.mx.x;
        b.mx.y = p.y > b.mx.y ? p.y : b.mx.y;
    }
    return b;
}

// Closed-box overlap. Touching boxes count, because touching rings are
// themselves a fault the edge test must see.
bool BoxesOverlap( const box2_t &a, const box2_t &b ) noexcept
{
    return a.mn.x <= b.mx.x && b.mn.x <= a.mx.x &&
           a.mn.y <= b.mx.y && b.mn.y <= a.mx.y;
}

planar_region_validation_t Fail(
    planar_region_fault_t fault,
    u32 iPolygon,
    u32 iContour ) noexcept
{
    planar_region_validation_t r{};
    r.fault = fault;
    r.iPolygon = iPolygon;
    r.iContour = iContour;
    switch ( fault ) {
        case planar_region_fault_t::NON_FINITE:
            r.status = geometry_status_t::NUMERIC_FAILURE; break;
        case planar_region_fault_t::COORDINATE_LIMIT:
            r.status = geometry_status_t::LIMIT_EXCEEDED; break;
        case planar_region_fault_t::DUPLICATE_POINT:
        case planar_region_fault_t::ZERO_AREA:
            r.status = geometry_status_t::DEGENERATE; break;
        case planar_region_fault_t::SELF_INTERSECTION:
        case planar_region_fault_t::CONTOURS_INTERSECT:
        case planar_region_fault_t::POLYGONS_INTERSECT:
            r.status = geometry_status_t::SELF_INTERSECTING; break;
        default:
            r.status = geometry_status_t::INVALID_TOPOLOGY; break;
    }
    return r;
}

// Per-contour checks that need no other contour.
planar_region_validation_t ValidateContourAlone(
    span_t<const math::vec2d_t> ring,
    u32 iPolygon,
    u32 iContour,
    const geometry_policy_t &policy ) noexcept
{
    const usize n = ring.nCount;
    const f64 limit = policy.numerical.fCoordinateMagnitudeLimit;
    for ( usize i = 0u; i < n; ++i ) {
        const math::vec2d_t p = ring.pData[i];
        if ( !math::Vec2d_IsFinite( p ) ) {
            planar_region_validation_t r =
                Fail( planar_region_fault_t::NON_FINITE, iPolygon, iContour );
            r.iEdge = static_cast<u32>( i );
            return r;
        }
        if ( std::fabs( p.x ) > limit || std::fabs( p.y ) > limit ) {
            planar_region_validation_t r =
                Fail( planar_region_fault_t::COORDINATE_LIMIT, iPolygon, iContour );
            r.iEdge = static_cast<u32>( i );
            return r;
        }
    }
    for ( usize i = 0u; i < n; ++i ) {
        const math::vec2d_t a = ring.pData[i];
        const math::vec2d_t b = ring.pData[( i + 1u ) % n];
        if ( a.x == b.x && a.y == b.y ) {
            planar_region_validation_t r =
                Fail( planar_region_fault_t::DUPLICATE_POINT, iPolygon, iContour );
            r.iEdge = static_cast<u32>( ( i + 1u ) % n );
            return r;
        }
    }
    if ( !( std::fabs( Planar_RingSignedArea( ring ) ) >
            policy.numerical.fMinimumFaceArea ) ) {
        return Fail( planar_region_fault_t::ZERO_AREA, iPolygon, iContour );
    }

    // Simplicity. Adjacent edges share exactly their common vertex; any
    // other contact (including a collinear fold-back "spike") is a fault.
    for ( usize i = 0u; i < n; ++i ) {
        const math::vec2d_t a0 = ring.pData[i];
        const math::vec2d_t a1 = ring.pData[( i + 1u ) % n];
        for ( usize j = i + 1u; j < n; ++j ) {
            const math::vec2d_t b0 = ring.pData[j];
            const math::vec2d_t b1 = ring.pData[( j + 1u ) % n];
            const planar_segment_relation_t rel =
                Planar_ClassifySegments( a0, a1, b0, b1 );
            const bool adjacent = ( j == i + 1u ) || ( i == 0u && j == n - 1u );
            bool bad = false;
            if ( adjacent ) {
                // Expected contact is TOUCH at the shared vertex. A second
                // contact point is only possible through collinear overlap,
                // or (for a triangle-like fold) the non-shared endpoint
                // lying on the other edge, which also reports TOUCH — so
                // check that explicitly.
                if ( rel == planar_segment_relation_t::COLLINEAR_OVERLAP ||
                     rel == planar_segment_relation_t::PROPER ) {
                    bad = true;
                } else if ( n > 3u ) {
                    const math::vec2d_t freeA = ( j == i + 1u ) ? a0 : a1;
                    const math::vec2d_t freeB = ( j == i + 1u ) ? b1 : b0;
                    if ( Planar_PointOnSegment( b0, b1, freeA ) ||
                         Planar_PointOnSegment( a0, a1, freeB ) ) {
                        bad = true;
                    }
                }
            } else {
                bad = rel != planar_segment_relation_t::DISJOINT;
            }
            if ( bad ) {
                planar_region_validation_t r = Fail(
                    planar_region_fault_t::SELF_INTERSECTION, iPolygon, iContour );
                r.iEdge = static_cast<u32>( i );
                r.iEdgeOther = static_cast<u32>( j );
                return r;
            }
        }
    }
    return planar_region_validation_t{};
}

bool SamePos( math::vec2d_t a, math::vec2d_t b ) noexcept
{
    return a.x == b.x && a.y == b.y;
}

// The interior of `ring` in an infinitesimal neighbourhood of contact point
// c on edge i (ring runs point i -> i+1, interior on the left). At a ring
// vertex the neighbourhood is the wedge prev -> c -> next; in the middle
// of an edge it is the open half-plane left of the edge.
struct local_wedge_t {
    math::vec2d_t prev{};
    math::vec2d_t c{};
    math::vec2d_t next{};
    bool bAtVertex{ false };
};

local_wedge_t WedgeAt(
    span_t<const math::vec2d_t> ring,
    usize iEdge,
    math::vec2d_t c ) noexcept
{
    const usize n = ring.nCount;
    local_wedge_t w{};
    w.c = c;
    usize iv = n;
    if ( SamePos( ring.pData[iEdge], c ) ) { iv = iEdge; }
    else if ( SamePos( ring.pData[( iEdge + 1u ) % n], c ) ) { iv = ( iEdge + 1u ) % n; }
    if ( iv < n ) {
        w.prev = ring.pData[( iv + n - 1u ) % n];
        w.next = ring.pData[( iv + 1u ) % n];
        w.bAtVertex = true;
    } else {
        w.prev = ring.pData[iEdge];
        w.next = ring.pData[( iEdge + 1u ) % n];
    }
    return w;
}

// Exact: is point q (a neighbour of c on another ring) strictly inside the
// interior wedge? O'Rourke's InCone for vertices, Orient2D for edges.
bool InWedge( const local_wedge_t &w, math::vec2d_t q ) noexcept
{
    if ( !w.bAtVertex ) {
        return math::Orient2D( w.prev, w.next, q ) > 0;
    }
    auto left = []( math::vec2d_t a, math::vec2d_t b, math::vec2d_t p ) noexcept {
        return math::Orient2D( a, b, p ) > 0;
    };
    auto leftOn = []( math::vec2d_t a, math::vec2d_t b, math::vec2d_t p ) noexcept {
        return math::Orient2D( a, b, p ) >= 0;
    };
    if ( leftOn( w.c, w.next, w.prev ) ) { // convex corner
        return left( w.c, q, w.prev ) && left( q, w.c, w.next );
    }
    return !( leftOn( w.c, q, w.next ) && leftOn( q, w.c, w.prev ) );
}

// Two rings may touch at isolated points (a weakly simple region: Boolean
// results such as the XOR of two overlapping squares legitimately touch at
// corners). They may not share an edge segment, cross properly, or cross
// *through* a contact point — the last is the case where one boundary
// passes from outside the other to inside it exactly at a vertex, detected
// by B's two local neighbours falling on different sides of A's wedge.
bool RingsCross(
    span_t<const math::vec2d_t> a,
    span_t<const math::vec2d_t> b,
    u32 *pEdgeA,
    u32 *pEdgeB ) noexcept
{
    if ( !BoxesOverlap( RingBox( a ), RingBox( b ) ) ) { return false; }
    for ( usize i = 0u; i < a.nCount; ++i ) {
        const math::vec2d_t a0 = a.pData[i];
        const math::vec2d_t a1 = a.pData[( i + 1u ) % a.nCount];
        for ( usize j = 0u; j < b.nCount; ++j ) {
            const math::vec2d_t b0 = b.pData[j];
            const math::vec2d_t b1 = b.pData[( j + 1u ) % b.nCount];
            const planar_segment_relation_t rel =
                Planar_ClassifySegments( a0, a1, b0, b1 );
            if ( rel == planar_segment_relation_t::DISJOINT ) { continue; }
            bool bad = rel != planar_segment_relation_t::TOUCH_ENDPOINT;
            if ( !bad ) {
                // Locate the single contact point (an endpoint of one edge).
                math::vec2d_t c{};
                if ( SamePos( a0, b0 ) || SamePos( a0, b1 ) ||
                     Planar_PointOnSegment( b0, b1, a0 ) ) { c = a0; }
                else if ( SamePos( a1, b0 ) || SamePos( a1, b1 ) ||
                          Planar_PointOnSegment( b0, b1, a1 ) ) { c = a1; }
                else if ( Planar_PointOnSegment( a0, a1, b0 ) ) { c = b0; }
                else { c = b1; }
                const local_wedge_t wa = WedgeAt( a, i, c );
                const local_wedge_t wb = WedgeAt( b, j, c );
                // B's local boundary neighbours must be on one side of A's
                // interior, and vice versa.
                bad = InWedge( wa, wb.prev ) != InWedge( wa, wb.next ) ||
                      InWedge( wb, wa.prev ) != InWedge( wb, wa.next );
            }
            if ( bad ) {
                *pEdgeA = static_cast<u32>( i );
                *pEdgeB = static_cast<u32>( j );
                return true;
            }
        }
    }
    return false;
}

// Containment of ring `probe` relative to ring `container`, skipping probe
// vertices that lie exactly on the container (allowed contact points).
// Returns BOUNDARY only if every probe vertex is on the container.
planar_containment_t ProbeRing(
    span_t<const math::vec2d_t> container,
    span_t<const math::vec2d_t> probe ) noexcept
{
    for ( usize k = 0u; k < probe.nCount; ++k ) {
        const planar_containment_t c = Planar_RingContains( container, probe.pData[k] );
        if ( c != planar_containment_t::BOUNDARY ) { return c; }
    }
    // All vertices on the boundary: fall back to an edge midpoint, which
    // for touching-but-not-crossing rings is off the boundary unless the
    // rings coincide (already rejected as collinear overlap).
    for ( usize k = 0u; k < probe.nCount; ++k ) {
        const math::vec2d_t m = math::Vec2d_Scale(
            math::Vec2d_Add( probe.pData[k], probe.pData[( k + 1u ) % probe.nCount] ), 0.5 );
        const planar_containment_t c = Planar_RingContains( container, m );
        if ( c != planar_containment_t::BOUNDARY ) { return c; }
    }
    return planar_containment_t::BOUNDARY;
}

} // namespace

const char *PlanarRegionFault_Name( planar_region_fault_t fault ) noexcept
{
    switch ( fault ) {
        case planar_region_fault_t::NONE: return "none";
        case planar_region_fault_t::NON_FINITE: return "non_finite";
        case planar_region_fault_t::COORDINATE_LIMIT: return "coordinate_limit";
        case planar_region_fault_t::DUPLICATE_POINT: return "duplicate_point";
        case planar_region_fault_t::ZERO_AREA: return "zero_area";
        case planar_region_fault_t::SELF_INTERSECTION: return "self_intersection";
        case planar_region_fault_t::OUTER_NOT_CCW: return "outer_not_ccw";
        case planar_region_fault_t::HOLE_NOT_CW: return "hole_not_cw";
        case planar_region_fault_t::CONTOURS_INTERSECT: return "contours_intersect";
        case planar_region_fault_t::HOLE_OUTSIDE_OUTER: return "hole_outside_outer";
        case planar_region_fault_t::HOLES_NESTED: return "holes_nested";
        case planar_region_fault_t::POLYGONS_INTERSECT: return "polygons_intersect";
        case planar_region_fault_t::POLYGONS_OVERLAP: return "polygons_overlap";
    }
    return "unknown";
}

planar_region_validation_t PlanarRegion_Validate(
    const planar_region_t *pRegion,
    const geometry_policy_t &policy ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ) {
        planar_region_validation_t r{};
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }

    const usize cPolygons = pRegion->polygons.nCount;

    // Pass 1: every contour alone, plus orientation against its role.
    for ( usize p = 0u; p < cPolygons; ++p ) {
        const planar_region_polygon_t &poly = pRegion->polygons.pData[p];
        for ( u32 c = 0u; c < poly.cContours; ++c ) {
            const u32 iContour = poly.iFirstContour + c;
            const span_t<const math::vec2d_t> ring =
                PlanarRegion_ContourPoints( pRegion, iContour );
            planar_region_validation_t r = ValidateContourAlone(
                ring, static_cast<u32>( p ), iContour, policy );
            if ( r.status != geometry_status_t::OK ) { return r; }

            const f64 area = Planar_RingSignedArea( ring );
            if ( c == 0u && !( area > 0.0 ) ) {
                return Fail( planar_region_fault_t::OUTER_NOT_CCW,
                             static_cast<u32>( p ), iContour );
            }
            if ( c > 0u && !( area < 0.0 ) ) {
                return Fail( planar_region_fault_t::HOLE_NOT_CW,
                             static_cast<u32>( p ), iContour );
            }
        }
    }

    // Pass 2: contours of the same polygon.
    for ( usize p = 0u; p < cPolygons; ++p ) {
        const planar_region_polygon_t &poly = pRegion->polygons.pData[p];
        for ( u32 c = 0u; c < poly.cContours; ++c ) {
            for ( u32 k = c + 1u; k < poly.cContours; ++k ) {
                const u32 iA = poly.iFirstContour + c;
                const u32 iB = poly.iFirstContour + k;
                u32 eA = 0u, eB = 0u;
                if ( RingsCross( PlanarRegion_ContourPoints( pRegion, iA ),
                                 PlanarRegion_ContourPoints( pRegion, iB ),
                                 &eA, &eB ) ) {
                    planar_region_validation_t r = Fail(
                        planar_region_fault_t::CONTOURS_INTERSECT,
                        static_cast<u32>( p ), iA );
                    r.iContourOther = iB;
                    r.iEdge = eA;
                    r.iEdgeOther = eB;
                    return r;
                }
            }
        }
        // Boundaries do not cross, so any off-boundary probe decides
        // containment of a whole ring.
        for ( u32 h = 1u; h < poly.cContours; ++h ) {
            const u32 iHole = poly.iFirstContour + h;
            const span_t<const math::vec2d_t> holeRing =
                PlanarRegion_ContourPoints( pRegion, iHole );
            if ( ProbeRing( PlanarRegion_ContourPoints( pRegion, poly.iFirstContour ),
                            holeRing ) != planar_containment_t::INSIDE ) {
                return Fail( planar_region_fault_t::HOLE_OUTSIDE_OUTER,
                             static_cast<u32>( p ), iHole );
            }
            for ( u32 k = 1u; k < poly.cContours; ++k ) {
                if ( k == h ) { continue; }
                const u32 iOther = poly.iFirstContour + k;
                if ( ProbeRing( PlanarRegion_ContourPoints( pRegion, iOther ),
                                holeRing ) == planar_containment_t::INSIDE ) {
                    planar_region_validation_t r = Fail(
                        planar_region_fault_t::HOLES_NESTED,
                        static_cast<u32>( p ), iHole );
                    r.iContourOther = iOther;
                    return r;
                }
            }
        }
    }

    // Pass 3: different polygons.
    for ( usize p = 0u; p < cPolygons; ++p ) {
        const planar_region_polygon_t &pa = pRegion->polygons.pData[p];
        for ( usize q = p + 1u; q < cPolygons; ++q ) {
            const planar_region_polygon_t &pb = pRegion->polygons.pData[q];
            for ( u32 c = 0u; c < pa.cContours; ++c ) {
                for ( u32 k = 0u; k < pb.cContours; ++k ) {
                    const u32 iA = pa.iFirstContour + c;
                    const u32 iB = pb.iFirstContour + k;
                    u32 eA = 0u, eB = 0u;
                    if ( RingsCross( PlanarRegion_ContourPoints( pRegion, iA ),
                                     PlanarRegion_ContourPoints( pRegion, iB ),
                                     &eA, &eB ) ) {
                        planar_region_validation_t r = Fail(
                            planar_region_fault_t::POLYGONS_INTERSECT,
                            static_cast<u32>( p ), iA );
                        r.iPolygonOther = static_cast<u32>( q );
                        r.iContourOther = iB;
                        r.iEdge = eA;
                        r.iEdgeOther = eB;
                        return r;
                    }
                }
            }
            // Disjoint boundaries: overlap iff one outer's vertex lies in the
            // other's filled area. Islands inside holes are legal because
            // PolygonContains reports OUTSIDE for points in a hole.
            // Probe past contact points: the first vertex off the other
            // polygon's boundary decides.
            auto probePolygon = [&]( usize iContainer, const span_t<const math::vec2d_t> ring ) {
                for ( usize k = 0u; k < ring.nCount; ++k ) {
                    const planar_containment_t c =
                        PlanarRegion_PolygonContains( pRegion, iContainer, ring.pData[k] );
                    if ( c != planar_containment_t::BOUNDARY ) { return c; }
                }
                for ( usize k = 0u; k < ring.nCount; ++k ) {
                    const math::vec2d_t m = math::Vec2d_Scale(
                        math::Vec2d_Add( ring.pData[k], ring.pData[( k + 1u ) % ring.nCount] ),
                        0.5 );
                    const planar_containment_t c =
                        PlanarRegion_PolygonContains( pRegion, iContainer, m );
                    if ( c != planar_containment_t::BOUNDARY ) { return c; }
                }
                return planar_containment_t::BOUNDARY;
            };
            if ( probePolygon( q, PlanarRegion_ContourPoints( pRegion, pa.iFirstContour ) ) ==
                     planar_containment_t::INSIDE ||
                 probePolygon( p, PlanarRegion_ContourPoints( pRegion, pb.iFirstContour ) ) ==
                     planar_containment_t::INSIDE ) {
                planar_region_validation_t r = Fail(
                    planar_region_fault_t::POLYGONS_OVERLAP,
                    static_cast<u32>( p ), pa.iFirstContour );
                r.iPolygonOther = static_cast<u32>( q );
                return r;
            }
        }
    }

    return planar_region_validation_t{};
}

} // namespace cypher::editor::geometry
