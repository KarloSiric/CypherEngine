//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PlanarRegion.cpp
//  Purpose: Implements PlanarRegion storage, building, and exact queries.
//  Details: Containment uses the non-zero winding rule with a half-open
//           crossing test: an edge counts only when it straddles the
//           horizontal line through the query point with the rule
//           (a.y <= p.y) != (b.y <= p.y). That rule counts a vertex lying
//           exactly on the scan line once, never twice. The side decision
//           is the exact Orient2D sign, so no epsilon is involved.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PlanarRegion.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Frame tolerance for Init. Frames produced by PlanarFrame_* are accurate
// to a few ulps; 1e-9 rejects hand-built frames that are visibly wrong
// while never rejecting a computed one.
constexpr f64 kFrameUnitTolerance = 1.0e-9;

bool OnSegment( math::vec2d_t a, math::vec2d_t b, math::vec2d_t p ) noexcept
{
    if ( math::Orient2D( a, b, p ) != 0 ) { return false; }
    const f64 minX = a.x < b.x ? a.x : b.x;
    const f64 maxX = a.x < b.x ? b.x : a.x;
    const f64 minY = a.y < b.y ? a.y : b.y;
    const f64 maxY = a.y < b.y ? b.y : a.y;
    return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY;
}

geometry_status_t CheckContourInput(
    span_t<const math::vec2d_t> pts ) noexcept
{
    if ( pts.pData == nullptr || pts.nCount < 3u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pts.nCount > kPlanarRegionContourPointsMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    for ( usize i = 0u; i < pts.nCount; ++i ) {
        if ( !math::Vec2d_IsFinite( pts.pData[i] ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    return geometry_status_t::OK;
}

// Reserves room for one more contour of cPoints (and optionally one more
// polygon). After this succeeds every PushBack below cannot fail, which is
// what makes the builders failure-atomic.
geometry_status_t ReserveFor(
    planar_region_t *pRegion,
    usize cPoints,
    bool bNewPolygon ) noexcept
{
    if ( pRegion->points.nCount + cPoints > kPlanarRegionPointsMax ||
         pRegion->contours.nCount + 1u > kPlanarRegionContoursMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !Vector_Reserve( &pRegion->points, pRegion->points.nCount + cPoints ) ||
         !Vector_Reserve( &pRegion->contours, pRegion->contours.nCount + 1u ) ||
         ( bNewPolygon &&
           !Vector_Reserve( &pRegion->polygons, pRegion->polygons.nCount + 1u ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void AppendContour(
    planar_region_t *pRegion,
    geometry_source_id_t contourId,
    span_t<const math::vec2d_t> pts ) noexcept
{
    planar_region_contour_t contour{};
    contour.iFirstPoint = static_cast<u32>( pRegion->points.nCount );
    contour.cPoints = static_cast<u32>( pts.nCount );
    contour.sourceId = contourId;
    for ( usize i = 0u; i < pts.nCount; ++i ) {
        (void)Vector_PushBack( &pRegion->points, pts.pData[i] );
    }
    (void)Vector_PushBack( &pRegion->contours, contour );
}

} // namespace

// ---------------------------------------------------------------------------
// Free ring helpers
// ---------------------------------------------------------------------------

planar_containment_t Planar_RingContains(
    span_t<const math::vec2d_t> ring,
    math::vec2d_t p ) noexcept
{
    if ( ring.pData == nullptr || ring.nCount < 3u ) {
        return planar_containment_t::OUTSIDE;
    }
    i64 winding = 0;
    for ( usize i = 0u; i < ring.nCount; ++i ) {
        const math::vec2d_t a = ring.pData[i];
        const math::vec2d_t b = ring.pData[( i + 1u ) % ring.nCount];
        if ( OnSegment( a, b, p ) ) {
            return planar_containment_t::BOUNDARY;
        }
        const bool aBelow = a.y <= p.y;
        const bool bBelow = b.y <= p.y;
        if ( aBelow == bBelow ) { continue; }
        const i32 side = math::Orient2D( a, b, p );
        if ( aBelow && side > 0 ) { ++winding; }       // upward edge, p left
        else if ( !aBelow && side < 0 ) { --winding; } // downward edge, p right
    }
    return winding != 0 ? planar_containment_t::INSIDE
                        : planar_containment_t::OUTSIDE;
}

f64 Planar_RingSignedArea( span_t<const math::vec2d_t> ring ) noexcept
{
    if ( ring.pData == nullptr || ring.nCount < 3u ) { return 0.0; }
    // Relative to the first point: the products stay small for rings far
    // from the origin, so cancellation does not swamp small areas.
    const math::vec2d_t o = ring.pData[0];
    f64 twice = 0.0;
    for ( usize i = 1u; i + 1u < ring.nCount; ++i ) {
        const math::vec2d_t a = math::Vec2d_Subtract( ring.pData[i], o );
        const math::vec2d_t b = math::Vec2d_Subtract( ring.pData[i + 1u], o );
        twice += a.x * b.y - a.y * b.x;
    }
    return 0.5 * twice;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool PlanarRegion_IsInitialized( const planar_region_t *pRegion ) noexcept
{
    return pRegion != nullptr && pRegion->points.pAllocator != nullptr;
}

geometry_status_t PlanarRegion_Init(
    planar_region_t *pRegion,
    const allocator_t *pAllocator,
    const planar_frame_t &frame,
    geometry_source_id_t regionId ) noexcept
{
    if ( pRegion == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( PlanarRegion_IsInitialized( pRegion ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( regionId ) ||
         !PlanarFrame_IsValid( frame, kFrameUnitTolerance ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !Vector_Init( &pRegion->points, pAllocator ) ||
         !Vector_Init( &pRegion->contours, pAllocator ) ||
         !Vector_Init( &pRegion->polygons, pAllocator ) ) {
        Vector_Shutdown( &pRegion->points );
        Vector_Shutdown( &pRegion->contours );
        Vector_Shutdown( &pRegion->polygons );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pRegion->frame = frame;
    pRegion->sourceId = regionId;
    return geometry_status_t::OK;
}

void PlanarRegion_Shutdown( planar_region_t *pRegion ) noexcept
{
    if ( pRegion == nullptr ) { return; }
    Vector_Shutdown( &pRegion->points );
    Vector_Shutdown( &pRegion->contours );
    Vector_Shutdown( &pRegion->polygons );
    pRegion->frame = planar_frame_t{};
    pRegion->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------

geometry_status_t PlanarRegion_TryAddPolygon(
    planar_region_t *pRegion,
    geometry_source_id_t polygonId,
    geometry_source_id_t outerContourId,
    span_t<const math::vec2d_t> outer,
    u32 *pPolygonIndexOut ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( polygonId ) ||
         !GeometrySourceId_IsValid( outerContourId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_status_t s = CheckContourInput( outer );
    if ( s != geometry_status_t::OK ) { return s; }
    s = ReserveFor( pRegion, outer.nCount, true );
    if ( s != geometry_status_t::OK ) { return s; }

    planar_region_polygon_t polygon{};
    polygon.iFirstContour = static_cast<u32>( pRegion->contours.nCount );
    polygon.cContours = 1u;
    polygon.sourceId = polygonId;
    AppendContour( pRegion, outerContourId, outer );
    (void)Vector_PushBack( &pRegion->polygons, polygon );

    if ( pPolygonIndexOut != nullptr ) {
        *pPolygonIndexOut = static_cast<u32>( pRegion->polygons.nCount - 1u );
    }
    return geometry_status_t::OK;
}

geometry_status_t PlanarRegion_TryAddHole(
    planar_region_t *pRegion,
    geometry_source_id_t holeContourId,
    span_t<const math::vec2d_t> hole ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pRegion->polygons.nCount == 0u ||
         !GeometrySourceId_IsValid( holeContourId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_status_t s = CheckContourInput( hole );
    if ( s != geometry_status_t::OK ) { return s; }
    s = ReserveFor( pRegion, hole.nCount, false );
    if ( s != geometry_status_t::OK ) { return s; }

    AppendContour( pRegion, holeContourId, hole );
    pRegion->polygons.pData[pRegion->polygons.nCount - 1u].cContours += 1u;
    return geometry_status_t::OK;
}

void PlanarRegion_Clear( planar_region_t *pRegion ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ) { return; }
    Vector_Clear( &pRegion->points );
    Vector_Clear( &pRegion->contours );
    Vector_Clear( &pRegion->polygons );
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

usize PlanarRegion_PolygonCount( const planar_region_t *pRegion ) noexcept
{
    return PlanarRegion_IsInitialized( pRegion ) ? pRegion->polygons.nCount : 0u;
}

usize PlanarRegion_ContourCount( const planar_region_t *pRegion ) noexcept
{
    return PlanarRegion_IsInitialized( pRegion ) ? pRegion->contours.nCount : 0u;
}

usize PlanarRegion_PointCount( const planar_region_t *pRegion ) noexcept
{
    return PlanarRegion_IsInitialized( pRegion ) ? pRegion->points.nCount : 0u;
}

span_t<const math::vec2d_t> PlanarRegion_ContourPoints(
    const planar_region_t *pRegion,
    usize iContour ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ||
         iContour >= pRegion->contours.nCount ) {
        return {};
    }
    const planar_region_contour_t &c = pRegion->contours.pData[iContour];
    return span_t<const math::vec2d_t>{ pRegion->points.pData + c.iFirstPoint,
                                        c.cPoints };
}

f64 PlanarRegion_ContourSignedArea(
    const planar_region_t *pRegion,
    usize iContour ) noexcept
{
    return Planar_RingSignedArea( PlanarRegion_ContourPoints( pRegion, iContour ) );
}

f64 PlanarRegion_PolygonArea(
    const planar_region_t *pRegion,
    usize iPolygon ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ||
         iPolygon >= pRegion->polygons.nCount ) {
        return 0.0;
    }
    const planar_region_polygon_t &poly = pRegion->polygons.pData[iPolygon];
    f64 area = std::fabs( PlanarRegion_ContourSignedArea( pRegion, poly.iFirstContour ) );
    for ( u32 h = 1u; h < poly.cContours; ++h ) {
        area -= std::fabs(
            PlanarRegion_ContourSignedArea( pRegion, poly.iFirstContour + h ) );
    }
    return area;
}

f64 PlanarRegion_Area( const planar_region_t *pRegion ) noexcept
{
    f64 area = 0.0;
    for ( usize i = 0u; i < PlanarRegion_PolygonCount( pRegion ); ++i ) {
        area += PlanarRegion_PolygonArea( pRegion, i );
    }
    return area;
}

planar_containment_t PlanarRegion_ContourContains(
    const planar_region_t *pRegion,
    usize iContour,
    math::vec2d_t point ) noexcept
{
    return Planar_RingContains( PlanarRegion_ContourPoints( pRegion, iContour ),
                                point );
}

planar_containment_t PlanarRegion_PolygonContains(
    const planar_region_t *pRegion,
    usize iPolygon,
    math::vec2d_t point ) noexcept
{
    if ( !PlanarRegion_IsInitialized( pRegion ) ||
         iPolygon >= pRegion->polygons.nCount ) {
        return planar_containment_t::OUTSIDE;
    }
    const planar_region_polygon_t &poly = pRegion->polygons.pData[iPolygon];
    const planar_containment_t outer =
        PlanarRegion_ContourContains( pRegion, poly.iFirstContour, point );
    if ( outer != planar_containment_t::INSIDE ) { return outer; }
    for ( u32 h = 1u; h < poly.cContours; ++h ) {
        const planar_containment_t inHole =
            PlanarRegion_ContourContains( pRegion, poly.iFirstContour + h, point );
        if ( inHole == planar_containment_t::BOUNDARY ) {
            return planar_containment_t::BOUNDARY;
        }
        if ( inHole == planar_containment_t::INSIDE ) {
            return planar_containment_t::OUTSIDE;
        }
    }
    return planar_containment_t::INSIDE;
}

planar_containment_t PlanarRegion_Contains(
    const planar_region_t *pRegion,
    math::vec2d_t point ) noexcept
{
    bool bBoundary = false;
    for ( usize i = 0u; i < PlanarRegion_PolygonCount( pRegion ); ++i ) {
        const planar_containment_t c =
            PlanarRegion_PolygonContains( pRegion, i, point );
        if ( c == planar_containment_t::INSIDE ) { return c; }
        if ( c == planar_containment_t::BOUNDARY ) { bBoundary = true; }
    }
    return bBoundary ? planar_containment_t::BOUNDARY
                     : planar_containment_t::OUTSIDE;
}

bool PlanarRegion_TryBounds(
    const planar_region_t *pRegion,
    math::vec2d_t *pMinOut,
    math::vec2d_t *pMaxOut ) noexcept
{
    if ( pMinOut == nullptr || pMaxOut == nullptr ||
         PlanarRegion_PointCount( pRegion ) == 0u ) {
        return false;
    }
    math::vec2d_t mn = pRegion->points.pData[0];
    math::vec2d_t mx = mn;
    for ( usize i = 1u; i < pRegion->points.nCount; ++i ) {
        const math::vec2d_t p = pRegion->points.pData[i];
        mn.x = p.x < mn.x ? p.x : mn.x;
        mn.y = p.y < mn.y ? p.y : mn.y;
        mx.x = p.x > mx.x ? p.x : mx.x;
        mx.y = p.y > mx.y ? p.y : mx.y;
    }
    *pMinOut = mn;
    *pMaxOut = mx;
    return true;
}

} // namespace cypher::editor::geometry
