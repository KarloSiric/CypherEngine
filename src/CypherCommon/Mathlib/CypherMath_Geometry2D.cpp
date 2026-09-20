//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Geometry2D.cpp
//  Purpose: Implements robust planar geometry used by editor tooling.
//  Details: Segment classification handles points and collinear overlap, while
//           ear clipping rejects self-intersecting or degenerate polygons.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Geometry2D.h"

#include "CypherCommon_Assert.h"

#include <algorithm>
#include <cmath>

namespace cypher::math
{

namespace
{

f64 Cross2( vec2_t a, vec2_t b ) noexcept
{
    return static_cast<f64>( a.x ) * static_cast<f64>( b.y ) -
           static_cast<f64>( a.y ) * static_cast<f64>( b.x );
}

f64 Dot2( vec2_t a, vec2_t b ) noexcept
{
    return static_cast<f64>( a.x ) * static_cast<f64>( b.x ) +
           static_cast<f64>( a.y ) * static_cast<f64>( b.y );
}

f64 ClampUnit( f64 value ) noexcept
{
    return std::clamp( value, 0.0, 1.0 );
}

vec2_t SegmentPoint( segment2_t segment, f64 parameter ) noexcept
{
    return Vec2_Make(
        static_cast<f32>( static_cast<f64>( segment.start.x ) +
            ( static_cast<f64>( segment.end.x ) - segment.start.x ) * parameter ),
        static_cast<f32>( static_cast<f64>( segment.start.y ) +
            ( static_cast<f64>( segment.end.y ) - segment.start.y ) * parameter ) );
}

f64 SegmentParameter( segment2_t segment, vec2_t point ) noexcept
{
    const vec2_t direction = Vec2_Subtract( segment.end, segment.start );
    const f64 lengthSquared = Dot2( direction, direction );
    if ( lengthSquared == 0.0 ) {
        return 0.0;
    }
    return Dot2( Vec2_Subtract( point, segment.start ), direction ) / lengthSquared;
}

bool_t PointInTriangleInclusive(
    vec2_t point,
    vec2_t a,
    vec2_t b,
    vec2_t c,
    f64 tolerance,
    f64 winding ) noexcept
{
    const f64 ab = Geometry2D_Orientation( a, b, point ) * winding;
    const f64 bc = Geometry2D_Orientation( b, c, point ) * winding;
    const f64 ca = Geometry2D_Orientation( c, a, point ) * winding;
    return ab >= -tolerance && bc >= -tolerance && ca >= -tolerance;
}

bool_t PolygonArgumentsValid( const vec2_t *pVertices, usize cVertices ) noexcept
{
    return pVertices != nullptr && cVertices >= 3u;
}

f64 ClampUnitD( f64 value ) noexcept
{
    return std::clamp( value, 0.0, 1.0 );
}

vec2d_t SegmentPointD( segment2d_t segment, f64 parameter ) noexcept
{
    return Vec2d_MulAdd(
        segment.start, Vec2d_Subtract( segment.end, segment.start ), parameter );
}

f64 SegmentParameterD( segment2d_t segment, vec2d_t point ) noexcept
{
    const vec2d_t direction = Vec2d_Subtract( segment.end, segment.start );
    const f64 lengthSquared = Vec2d_Dot( direction, direction );
    if ( lengthSquared == 0.0 ) {
        return 0.0;
    }
    return Vec2d_Dot( Vec2d_Subtract( point, segment.start ), direction ) /
           lengthSquared;
}

bool_t PointInTriangleInclusiveD(
    vec2d_t point,
    vec2d_t a,
    vec2d_t b,
    vec2d_t c,
    f64 tolerance,
    f64 winding ) noexcept
{
    const f64 ab = Geometry2D_OrientationD( a, b, point ) * winding;
    const f64 bc = Geometry2D_OrientationD( b, c, point ) * winding;
    const f64 ca = Geometry2D_OrientationD( c, a, point ) * winding;
    return ab >= -tolerance && bc >= -tolerance && ca >= -tolerance;
}

bool_t PolygonArgumentsValidD( const vec2d_t *pVertices, usize cVertices ) noexcept
{
    return pVertices != nullptr && cVertices >= 3u;
}

} // namespace

f64 Geometry2D_Orientation( vec2_t a, vec2_t b, vec2_t c ) noexcept
{
    const f64 abX = static_cast<f64>( b.x ) - a.x;
    const f64 abY = static_cast<f64>( b.y ) - a.y;
    const f64 acX = static_cast<f64>( c.x ) - a.x;
    const f64 acY = static_cast<f64>( c.y ) - a.y;
    return abX * acY - abY * acX;
}

bool_t Geometry2D_PointOnSegment(
    vec2_t point,
    segment2_t segment,
    f32 tolerance ) noexcept
{
    if ( tolerance < 0.0f || !Scalar_IsFinite( tolerance ) ||
         !Vec2_IsFinite( point ) || !Vec2_IsFinite( segment.start ) ||
         !Vec2_IsFinite( segment.end ) ) {
        return false;
    }

    if ( std::abs( Geometry2D_Orientation( segment.start, segment.end, point ) ) >
         static_cast<f64>( tolerance ) ) {
        return false;
    }

    const f64 minX = std::min<f64>( segment.start.x, segment.end.x ) - tolerance;
    const f64 maxX = std::max<f64>( segment.start.x, segment.end.x ) + tolerance;
    const f64 minY = std::min<f64>( segment.start.y, segment.end.y ) - tolerance;
    const f64 maxY = std::max<f64>( segment.start.y, segment.end.y ) + tolerance;
    return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

segment2_intersection_t Geometry2D_IntersectSegments(
    segment2_t a,
    segment2_t b,
    f32 tolerance ) noexcept
{
    segment2_intersection_t result{};
    result.kind = segment2_intersection_kind_t::NONE;
    if ( tolerance < 0.0f || !Scalar_IsFinite( tolerance ) ) {
        return result;
    }

    const vec2_t r = Vec2_Subtract( a.end, a.start );
    const vec2_t s = Vec2_Subtract( b.end, b.start );
    const vec2_t qMinusP = Vec2_Subtract( b.start, a.start );
    const f64 denominator = Cross2( r, s );
    const f64 collinearity = Cross2( qMinusP, r );
    const f64 tolerance64 = tolerance;
    const f64 rLengthSquared = Dot2( r, r );
    const f64 sLengthSquared = Dot2( s, s );

    // A zero-length segment is a point query, not a line-line intersection.
    // Handle all point combinations before using parametric denominators.
    if ( rLengthSquared <= tolerance64 * tolerance64 &&
         sLengthSquared <= tolerance64 * tolerance64 ) {
        if ( Vec2_DistanceSquared( a.start, b.start ) <= tolerance * tolerance ) {
            result.kind = segment2_intersection_kind_t::POINT;
            result.point0 = a.start;
        }
        return result;
    }
    if ( rLengthSquared <= tolerance64 * tolerance64 ) {
        if ( Geometry2D_PointOnSegment( a.start, b, tolerance ) ) {
            result.kind = segment2_intersection_kind_t::POINT;
            result.point0 = a.start;
            result.parameterB0 = static_cast<f32>( ClampUnit( SegmentParameter( b, a.start ) ) );
        }
        return result;
    }
    if ( sLengthSquared <= tolerance64 * tolerance64 ) {
        if ( Geometry2D_PointOnSegment( b.start, a, tolerance ) ) {
            result.kind = segment2_intersection_kind_t::POINT;
            result.point0 = b.start;
            result.parameterA0 = static_cast<f32>( ClampUnit( SegmentParameter( a, b.start ) ) );
        }
        return result;
    }

    if ( std::abs( denominator ) <= tolerance64 ) {
        if ( std::abs( collinearity ) > tolerance64 ) {
            return result;
        }

        // Collinear segments reduce to overlapping parameter intervals on A.
        f64 t0 = SegmentParameter( a, b.start );
        f64 t1 = SegmentParameter( a, b.end );
        if ( t0 > t1 ) {
            std::swap( t0, t1 );
        }
        const f64 overlapStart = std::max( 0.0, t0 );
        const f64 overlapEnd = std::min( 1.0, t1 );
        if ( overlapEnd < overlapStart - tolerance64 ) {
            return result;
        }

        result.parameterA0 = static_cast<f32>( ClampUnit( overlapStart ) );
        result.parameterA1 = static_cast<f32>( ClampUnit( overlapEnd ) );
        result.point0 = SegmentPoint( a, result.parameterA0 );
        result.point1 = SegmentPoint( a, result.parameterA1 );
        result.parameterB0 = static_cast<f32>( ClampUnit( SegmentParameter( b, result.point0 ) ) );
        result.parameterB1 = static_cast<f32>( ClampUnit( SegmentParameter( b, result.point1 ) ) );
        result.kind = Vec2_DistanceSquared( result.point0, result.point1 ) <=
                tolerance * tolerance
            ? segment2_intersection_kind_t::POINT
            : segment2_intersection_kind_t::OVERLAP;
        return result;
    }

    const f64 parameterA = Cross2( qMinusP, s ) / denominator;
    const f64 parameterB = Cross2( qMinusP, r ) / denominator;
    if ( parameterA < -tolerance64 || parameterA > 1.0 + tolerance64 ||
         parameterB < -tolerance64 || parameterB > 1.0 + tolerance64 ) {
        return result;
    }

    result.kind = segment2_intersection_kind_t::POINT;
    result.parameterA0 = static_cast<f32>( ClampUnit( parameterA ) );
    result.parameterB0 = static_cast<f32>( ClampUnit( parameterB ) );
    result.point0 = SegmentPoint( a, result.parameterA0 );
    result.point1 = result.point0;
    return result;
}

f64 Polygon2_SignedArea( const vec2_t *pVertices, usize cVertices ) noexcept
{
    if ( !PolygonArgumentsValid( pVertices, cVertices ) ) {
        return 0.0;
    }

    f64 twiceArea = 0.0;
    for ( usize i = 0u; i < cVertices; ++i ) {
        twiceArea += Cross2( pVertices[i], pVertices[( i + 1u ) % cVertices] );
    }
    return twiceArea * 0.5;
}

bool_t Polygon2_TryCentroid(
    const vec2_t *pVertices,
    usize cVertices,
    f64 minimumAbsArea,
    vec2_t *pCentroid ) noexcept
{
    const bool_t bValidOutput = pCentroid != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Polygon2_TryCentroid requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pCentroid = CY_VEC2_ZERO;
    if ( !PolygonArgumentsValid( pVertices, cVertices ) || minimumAbsArea < 0.0 ) {
        return false;
    }

    f64 twiceArea = 0.0;
    f64 weightedX = 0.0;
    f64 weightedY = 0.0;
    for ( usize i = 0u; i < cVertices; ++i ) {
        const vec2_t a = pVertices[i];
        const vec2_t b = pVertices[( i + 1u ) % cVertices];
        const f64 cross = Cross2( a, b );
        twiceArea += cross;
        weightedX += ( static_cast<f64>( a.x ) + b.x ) * cross;
        weightedY += ( static_cast<f64>( a.y ) + b.y ) * cross;
    }
    if ( std::abs( twiceArea * 0.5 ) <= minimumAbsArea ) {
        return false;
    }

    const f64 divisor = 3.0 * twiceArea;
    *pCentroid = Vec2_Make(
        static_cast<f32>( weightedX / divisor ),
        static_cast<f32>( weightedY / divisor ) );
    return Vec2_IsFinite( *pCentroid );
}

bool_t Polygon2_ContainsPoint(
    const vec2_t *pVertices,
    usize cVertices,
    vec2_t point,
    f32 boundaryTolerance,
    bool_t bIncludeBoundary ) noexcept
{
    if ( !PolygonArgumentsValid( pVertices, cVertices ) ||
         boundaryTolerance < 0.0f || !Vec2_IsFinite( point ) ) {
        return false;
    }

    // Even-odd crossing rule: each edge crossing to the right toggles interior
    // state. Boundary points are resolved first by the caller's policy.
    bool_t bInside = false;
    for ( usize i = 0u, j = cVertices - 1u; i < cVertices; j = i++ ) {
        const segment2_t edge{ pVertices[j], pVertices[i] };
        if ( Geometry2D_PointOnSegment( point, edge, boundaryTolerance ) ) {
            return bIncludeBoundary;
        }

        const bool_t bStraddles = ( pVertices[i].y > point.y ) !=
                                  ( pVertices[j].y > point.y );
        if ( bStraddles ) {
            const f64 xAtY = static_cast<f64>( pVertices[j].x ) +
                ( static_cast<f64>( point.y ) - pVertices[j].y ) *
                ( static_cast<f64>( pVertices[i].x ) - pVertices[j].x ) /
                ( static_cast<f64>( pVertices[i].y ) - pVertices[j].y );
            if ( static_cast<f64>( point.x ) < xAtY ) {
                bInside = !bInside;
            }
        }
    }
    return bInside;
}

bool_t Polygon2_IsSimple(
    const vec2_t *pVertices,
    usize cVertices,
    f32 tolerance ) noexcept
{
    if ( !PolygonArgumentsValid( pVertices, cVertices ) || tolerance < 0.0f ) {
        return false;
    }

    // Non-adjacent edge intersections make the polygon self-intersecting.
    for ( usize i = 0u; i < cVertices; ++i ) {
        const usize iNext = ( i + 1u ) % cVertices;
        if ( Vec2_DistanceSquared( pVertices[i], pVertices[iNext] ) <=
             tolerance * tolerance ) {
            return false;
        }
        const segment2_t a{ pVertices[i], pVertices[iNext] };
        for ( usize j = i + 1u; j < cVertices; ++j ) {
            const usize jNext = ( j + 1u ) % cVertices;
            if ( i == j || iNext == j || jNext == i ) {
                continue;
            }
            const segment2_t b{ pVertices[j], pVertices[jNext] };
            if ( Geometry2D_IntersectSegments( a, b, tolerance ).kind !=
                 segment2_intersection_kind_t::NONE ) {
                return false;
            }
        }
    }
    return true;
}

bool_t Polygon2_IsConvex(
    const vec2_t *pVertices,
    usize cVertices,
    f64 orientationTolerance ) noexcept
{
    if ( !PolygonArgumentsValid( pVertices, cVertices ) ||
         orientationTolerance < 0.0 ) {
        return false;
    }

    f64 expectedSign = 0.0;
    f64 totalTurning = 0.0;
    for ( usize i = 0u; i < cVertices; ++i ) {
        const vec2_t vertex = pVertices[i];
        const vec2_t nextVertex = pVertices[( i + 1u ) % cVertices];
        const vec2_t afterNextVertex = pVertices[( i + 2u ) % cVertices];

        // Accumulate the signed exterior angle at nextVertex. A simple polygon
        // turns through exactly one revolution; a star polygon turns through
        // two or more while still agreeing on sign at every corner, which is
        // why the sign test below cannot detect it alone.
        const vec2_t incoming = Vec2_Subtract( nextVertex, vertex );
        const vec2_t outgoing = Vec2_Subtract( afterNextVertex, nextVertex );
        totalTurning += std::atan2(
            Cross2( incoming, outgoing ), Dot2( incoming, outgoing ) );

        const f64 orientation =
            Geometry2D_Orientation( vertex, nextVertex, afterNextVertex );
        if ( std::abs( orientation ) <= orientationTolerance ) {
            continue;
        }
        const f64 sign = orientation > 0.0 ? 1.0 : -1.0;
        if ( expectedSign == 0.0 ) {
            expectedSign = sign;
        } else if ( sign != expectedSign ) {
            return false;
        }
    }
    if ( expectedSign == 0.0 ) {
        return false;
    }

    // One revolution is 2*pi and the next achievable total is 4*pi, so the
    // midpoint separates them with an enormous margin. Testing against 3*pi
    // avoids inventing an angular tolerance that the caller cannot supply --
    // orientationTolerance is an area, not an angle, and must not be reused here.
    constexpr f64 cRevolutionSeparator = 9.424777960769379; // 3 * pi
    return std::abs( totalTurning ) < cRevolutionSeparator;
}

polygon_triangulation_result_t Polygon2_Triangulate(
    const vec2_t *pVertices,
    usize cVertices,
    f64 distanceTolerance,
    f64 areaTolerance,
    u32 *pScratchIndices,
    usize cScratchIndices,
    u32 *pOutputIndices,
    usize cOutputIndices ) noexcept
{
    polygon_triangulation_result_t result{};
    result.status = polygon_triangulation_status_t::INVALID_ARGUMENT;
    if ( !PolygonArgumentsValid( pVertices, cVertices ) ||
         distanceTolerance < 0.0 || areaTolerance < 0.0 ||
         pOutputIndices == nullptr ) {
        return result;
    }
    if ( pScratchIndices == nullptr || cScratchIndices < cVertices ) {
        result.status = polygon_triangulation_status_t::INSUFFICIENT_SCRATCH;
        return result;
    }

    const usize cRequiredIndices = ( cVertices - 2u ) * 3u;
    if ( cOutputIndices < cRequiredIndices ) {
        result.status = polygon_triangulation_status_t::INSUFFICIENT_OUTPUT;
        return result;
    }
    if ( !Polygon2_IsSimple(
             pVertices, cVertices, static_cast<f32>( distanceTolerance ) ) ) {
        result.status = polygon_triangulation_status_t::NOT_SIMPLE;
        return result;
    }

    const f64 signedArea = Polygon2_SignedArea( pVertices, cVertices );
    if ( std::abs( signedArea ) <= areaTolerance ) {
        result.status = polygon_triangulation_status_t::DEGENERATE;
        return result;
    }
    const f64 winding = signedArea > 0.0 ? 1.0 : -1.0;
    for ( usize i = 0u; i < cVertices; ++i ) {
        pScratchIndices[i] = static_cast<u32>( i );
    }

    usize cRemaining = cVertices;
    usize cWritten = 0u;

    // Ear clipping removes one convex corner at a time when its triangle
    // contains no remaining polygon vertex.
    while ( cRemaining > 3u ) {
        bool_t bClippedEar = false;
        for ( usize i = 0u; i < cRemaining; ++i ) {
            const usize previous = ( i + cRemaining - 1u ) % cRemaining;
            const usize next = ( i + 1u ) % cRemaining;
            const u32 iA = pScratchIndices[previous];
            const u32 iB = pScratchIndices[i];
            const u32 iC = pScratchIndices[next];
            const f64 corner = Geometry2D_Orientation(
                pVertices[iA], pVertices[iB], pVertices[iC] ) * winding;
            if ( corner <= areaTolerance ) {
                continue;
            }

            bool_t bContainsVertex = false;
            for ( usize candidate = 0u; candidate < cRemaining; ++candidate ) {
                if ( candidate == previous || candidate == i || candidate == next ) {
                    continue;
                }
                if ( PointInTriangleInclusive(
                         pVertices[pScratchIndices[candidate]],
                         pVertices[iA], pVertices[iB], pVertices[iC],
                         areaTolerance, winding ) ) {
                    bContainsVertex = true;
                    break;
                }
            }
            if ( bContainsVertex ) {
                continue;
            }

            pOutputIndices[cWritten++] = iA;
            pOutputIndices[cWritten++] = iB;
            pOutputIndices[cWritten++] = iC;
            for ( usize shift = i; shift + 1u < cRemaining; ++shift ) {
                pScratchIndices[shift] = pScratchIndices[shift + 1u];
            }
            --cRemaining;
            bClippedEar = true;
            break;
        }
        if ( !bClippedEar ) {
            result.status = polygon_triangulation_status_t::DEGENERATE;
            result.cIndicesWritten = cWritten;
            result.cTriangles = cWritten / 3u;
            return result;
        }
    }

    pOutputIndices[cWritten++] = pScratchIndices[0];
    pOutputIndices[cWritten++] = pScratchIndices[1];
    pOutputIndices[cWritten++] = pScratchIndices[2];
    result.status = polygon_triangulation_status_t::OK;
    result.cIndicesWritten = cWritten;
    result.cTriangles = cWritten / 3u;
    return result;
}

//==========================================================================
// Binary64 authoring surface
//==========================================================================

f64 Geometry2D_OrientationD( vec2d_t a, vec2d_t b, vec2d_t c ) noexcept
{
    return Vec2d_Cross( Vec2d_Subtract( b, a ), Vec2d_Subtract( c, a ) );
}

bool_t Geometry2D_PointOnSegmentD(
    vec2d_t point,
    segment2d_t segment,
    f64 tolerance ) noexcept
{
    if ( tolerance < 0.0 || !Scalar_IsFinite( tolerance ) ||
         !Vec2d_IsFinite( point ) || !Vec2d_IsFinite( segment.start ) ||
         !Vec2d_IsFinite( segment.end ) ) {
        return false;
    }

    if ( std::abs( Geometry2D_OrientationD( segment.start, segment.end, point ) ) >
         tolerance ) {
        return false;
    }

    const f64 minX = std::min( segment.start.x, segment.end.x ) - tolerance;
    const f64 maxX = std::max( segment.start.x, segment.end.x ) + tolerance;
    const f64 minY = std::min( segment.start.y, segment.end.y ) - tolerance;
    const f64 maxY = std::max( segment.start.y, segment.end.y ) + tolerance;
    return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

segment2d_intersection_t Geometry2D_IntersectSegmentsD(
    segment2d_t a,
    segment2d_t b,
    f64 tolerance ) noexcept
{
    segment2d_intersection_t result{};
    result.kind = segment2_intersection_kind_t::NONE;
    if ( tolerance < 0.0 || !Scalar_IsFinite( tolerance ) ) {
        return result;
    }

    const vec2d_t r = Vec2d_Subtract( a.end, a.start );
    const vec2d_t s = Vec2d_Subtract( b.end, b.start );
    const vec2d_t qMinusP = Vec2d_Subtract( b.start, a.start );
    const f64 denominator = Vec2d_Cross( r, s );
    const f64 collinearity = Vec2d_Cross( qMinusP, r );
    const f64 rLengthSquared = Vec2d_Dot( r, r );
    const f64 sLengthSquared = Vec2d_Dot( s, s );

    // A zero-length segment is a point query, not a line-line intersection.
    // Handle all point combinations before using parametric denominators.
    if ( rLengthSquared <= tolerance * tolerance &&
         sLengthSquared <= tolerance * tolerance ) {
        if ( Vec2d_DistanceSquared( a.start, b.start ) <= tolerance * tolerance ) {
            result.kind = segment2_intersection_kind_t::POINT;
            result.point0 = a.start;
        }
        return result;
    }
    if ( rLengthSquared <= tolerance * tolerance ) {
        if ( Geometry2D_PointOnSegmentD( a.start, b, tolerance ) ) {
            result.kind = segment2_intersection_kind_t::POINT;
            result.point0 = a.start;
            result.parameterB0 = ClampUnitD( SegmentParameterD( b, a.start ) );
        }
        return result;
    }
    if ( sLengthSquared <= tolerance * tolerance ) {
        if ( Geometry2D_PointOnSegmentD( b.start, a, tolerance ) ) {
            result.kind = segment2_intersection_kind_t::POINT;
            result.point0 = b.start;
            result.parameterA0 = ClampUnitD( SegmentParameterD( a, b.start ) );
        }
        return result;
    }

    if ( std::abs( denominator ) <= tolerance ) {
        if ( std::abs( collinearity ) > tolerance ) {
            return result;
        }

        // Collinear segments reduce to overlapping parameter intervals on A.
        f64 t0 = SegmentParameterD( a, b.start );
        f64 t1 = SegmentParameterD( a, b.end );
        if ( t0 > t1 ) {
            std::swap( t0, t1 );
        }
        const f64 overlapStart = std::max( 0.0, t0 );
        const f64 overlapEnd = std::min( 1.0, t1 );
        if ( overlapEnd < overlapStart - tolerance ) {
            return result;
        }

        result.parameterA0 = ClampUnitD( overlapStart );
        result.parameterA1 = ClampUnitD( overlapEnd );
        result.point0 = SegmentPointD( a, result.parameterA0 );
        result.point1 = SegmentPointD( a, result.parameterA1 );
        result.parameterB0 = ClampUnitD( SegmentParameterD( b, result.point0 ) );
        result.parameterB1 = ClampUnitD( SegmentParameterD( b, result.point1 ) );
        result.kind = Vec2d_DistanceSquared( result.point0, result.point1 ) <=
                tolerance * tolerance
            ? segment2_intersection_kind_t::POINT
            : segment2_intersection_kind_t::OVERLAP;
        return result;
    }

    const f64 parameterA = Vec2d_Cross( qMinusP, s ) / denominator;
    const f64 parameterB = Vec2d_Cross( qMinusP, r ) / denominator;
    if ( parameterA < -tolerance || parameterA > 1.0 + tolerance ||
         parameterB < -tolerance || parameterB > 1.0 + tolerance ) {
        return result;
    }

    result.kind = segment2_intersection_kind_t::POINT;
    result.parameterA0 = ClampUnitD( parameterA );
    result.parameterB0 = ClampUnitD( parameterB );
    result.point0 = SegmentPointD( a, result.parameterA0 );
    result.point1 = result.point0;
    return result;
}

f64 Polygon2d_SignedArea( const vec2d_t *pVertices, usize cVertices ) noexcept
{
    if ( !PolygonArgumentsValidD( pVertices, cVertices ) ) {
        return 0.0;
    }

    f64 twiceArea = 0.0;
    for ( usize i = 0u; i < cVertices; ++i ) {
        twiceArea += Vec2d_Cross( pVertices[i], pVertices[( i + 1u ) % cVertices] );
    }
    return twiceArea * 0.5;
}

bool_t Polygon2d_TryCentroid(
    const vec2d_t *pVertices,
    usize cVertices,
    f64 minimumAbsArea,
    vec2d_t *pCentroid ) noexcept
{
    const bool_t bValidOutput = pCentroid != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Polygon2d_TryCentroid requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pCentroid = CY_VEC2D_ZERO;
    if ( !PolygonArgumentsValidD( pVertices, cVertices ) || minimumAbsArea < 0.0 ) {
        return false;
    }

    f64 twiceArea = 0.0;
    f64 weightedX = 0.0;
    f64 weightedY = 0.0;
    for ( usize i = 0u; i < cVertices; ++i ) {
        const vec2d_t a = pVertices[i];
        const vec2d_t b = pVertices[( i + 1u ) % cVertices];
        const f64 cross = Vec2d_Cross( a, b );
        twiceArea += cross;
        weightedX += ( a.x + b.x ) * cross;
        weightedY += ( a.y + b.y ) * cross;
    }
    if ( std::abs( twiceArea * 0.5 ) <= minimumAbsArea ) {
        return false;
    }

    const f64 divisor = 3.0 * twiceArea;
    *pCentroid = Vec2d_Make( weightedX / divisor, weightedY / divisor );
    return Vec2d_IsFinite( *pCentroid );
}

bool_t Polygon2d_ContainsPoint(
    const vec2d_t *pVertices,
    usize cVertices,
    vec2d_t point,
    f64 boundaryTolerance,
    bool_t bIncludeBoundary ) noexcept
{
    if ( !PolygonArgumentsValidD( pVertices, cVertices ) ||
         boundaryTolerance < 0.0 || !Vec2d_IsFinite( point ) ) {
        return false;
    }

    // Even-odd crossing rule: each edge crossing to the right toggles interior
    // state. Boundary points are resolved first by the caller's policy.
    bool_t bInside = false;
    for ( usize i = 0u, j = cVertices - 1u; i < cVertices; j = i++ ) {
        const segment2d_t edge{ pVertices[j], pVertices[i] };
        if ( Geometry2D_PointOnSegmentD( point, edge, boundaryTolerance ) ) {
            return bIncludeBoundary;
        }

        const bool_t bStraddles = ( pVertices[i].y > point.y ) !=
                                  ( pVertices[j].y > point.y );
        if ( bStraddles ) {
            const f64 xAtY = pVertices[j].x +
                ( point.y - pVertices[j].y ) *
                ( pVertices[i].x - pVertices[j].x ) /
                ( pVertices[i].y - pVertices[j].y );
            if ( point.x < xAtY ) {
                bInside = !bInside;
            }
        }
    }
    return bInside;
}

bool_t Polygon2d_IsSimple(
    const vec2d_t *pVertices,
    usize cVertices,
    f64 tolerance ) noexcept
{
    if ( !PolygonArgumentsValidD( pVertices, cVertices ) || tolerance < 0.0 ) {
        return false;
    }

    // Non-adjacent edge intersections make the polygon self-intersecting.
    for ( usize i = 0u; i < cVertices; ++i ) {
        const usize iNext = ( i + 1u ) % cVertices;
        if ( Vec2d_DistanceSquared( pVertices[i], pVertices[iNext] ) <=
             tolerance * tolerance ) {
            return false;
        }
        const segment2d_t a{ pVertices[i], pVertices[iNext] };
        for ( usize j = i + 1u; j < cVertices; ++j ) {
            const usize jNext = ( j + 1u ) % cVertices;
            if ( i == j || iNext == j || jNext == i ) {
                continue;
            }
            const segment2d_t b{ pVertices[j], pVertices[jNext] };
            if ( Geometry2D_IntersectSegmentsD( a, b, tolerance ).kind !=
                 segment2_intersection_kind_t::NONE ) {
                return false;
            }
        }
    }
    return true;
}

bool_t Polygon2d_IsConvex(
    const vec2d_t *pVertices,
    usize cVertices,
    f64 orientationTolerance ) noexcept
{
    if ( !PolygonArgumentsValidD( pVertices, cVertices ) ||
         orientationTolerance < 0.0 ) {
        return false;
    }

    f64 expectedSign = 0.0;
    f64 totalTurning = 0.0;
    for ( usize i = 0u; i < cVertices; ++i ) {
        const vec2d_t vertex = pVertices[i];
        const vec2d_t nextVertex = pVertices[( i + 1u ) % cVertices];
        const vec2d_t afterNextVertex = pVertices[( i + 2u ) % cVertices];

        // Accumulate the signed exterior angle at nextVertex. A simple polygon
        // turns through exactly one revolution; a star polygon turns through
        // two or more while still agreeing on sign at every corner, which is
        // why the sign test below cannot detect it alone.
        const vec2d_t incoming = Vec2d_Subtract( nextVertex, vertex );
        const vec2d_t outgoing = Vec2d_Subtract( afterNextVertex, nextVertex );
        totalTurning += std::atan2(
            Vec2d_Cross( incoming, outgoing ), Vec2d_Dot( incoming, outgoing ) );

        const f64 orientation =
            Geometry2D_OrientationD( vertex, nextVertex, afterNextVertex );
        if ( std::abs( orientation ) <= orientationTolerance ) {
            continue;
        }
        const f64 sign = orientation > 0.0 ? 1.0 : -1.0;
        if ( expectedSign == 0.0 ) {
            expectedSign = sign;
        } else if ( sign != expectedSign ) {
            return false;
        }
    }
    if ( expectedSign == 0.0 ) {
        return false;
    }

    // One revolution is 2*pi and the next achievable total is 4*pi, so the
    // midpoint separates them with an enormous margin. Testing against 3*pi
    // avoids inventing an angular tolerance that the caller cannot supply --
    // orientationTolerance is an area, not an angle, and must not be reused here.
    constexpr f64 cRevolutionSeparator = 9.424777960769379; // 3 * pi
    return std::abs( totalTurning ) < cRevolutionSeparator;
}

polygon_triangulation_result_t Polygon2d_Triangulate(
    const vec2d_t *pVertices,
    usize cVertices,
    f64 distanceTolerance,
    f64 areaTolerance,
    u32 *pScratchIndices,
    usize cScratchIndices,
    u32 *pOutputIndices,
    usize cOutputIndices ) noexcept
{
    polygon_triangulation_result_t result{};
    result.status = polygon_triangulation_status_t::INVALID_ARGUMENT;
    if ( !PolygonArgumentsValidD( pVertices, cVertices ) ||
         distanceTolerance < 0.0 || areaTolerance < 0.0 ||
         pOutputIndices == nullptr ) {
        return result;
    }
    if ( pScratchIndices == nullptr || cScratchIndices < cVertices ) {
        result.status = polygon_triangulation_status_t::INSUFFICIENT_SCRATCH;
        return result;
    }

    const usize cRequiredIndices = ( cVertices - 2u ) * 3u;
    if ( cOutputIndices < cRequiredIndices ) {
        result.status = polygon_triangulation_status_t::INSUFFICIENT_OUTPUT;
        return result;
    }
    if ( !Polygon2d_IsSimple( pVertices, cVertices, distanceTolerance ) ) {
        result.status = polygon_triangulation_status_t::NOT_SIMPLE;
        return result;
    }

    const f64 signedArea = Polygon2d_SignedArea( pVertices, cVertices );
    if ( std::abs( signedArea ) <= areaTolerance ) {
        result.status = polygon_triangulation_status_t::DEGENERATE;
        return result;
    }
    const f64 winding = signedArea > 0.0 ? 1.0 : -1.0;
    for ( usize i = 0u; i < cVertices; ++i ) {
        pScratchIndices[i] = static_cast<u32>( i );
    }

    usize cRemaining = cVertices;
    usize cWritten = 0u;

    // Ear clipping removes one convex corner at a time when its triangle
    // contains no remaining polygon vertex.
    while ( cRemaining > 3u ) {
        bool_t bClippedEar = false;
        for ( usize i = 0u; i < cRemaining; ++i ) {
            const usize previous = ( i + cRemaining - 1u ) % cRemaining;
            const usize next = ( i + 1u ) % cRemaining;
            const u32 iA = pScratchIndices[previous];
            const u32 iB = pScratchIndices[i];
            const u32 iC = pScratchIndices[next];
            const f64 corner = Geometry2D_OrientationD(
                pVertices[iA], pVertices[iB], pVertices[iC] ) * winding;
            if ( corner <= areaTolerance ) {
                continue;
            }

            bool_t bContainsVertex = false;
            for ( usize candidate = 0u; candidate < cRemaining; ++candidate ) {
                if ( candidate == previous || candidate == i || candidate == next ) {
                    continue;
                }
                if ( PointInTriangleInclusiveD(
                         pVertices[pScratchIndices[candidate]],
                         pVertices[iA], pVertices[iB], pVertices[iC],
                         areaTolerance, winding ) ) {
                    bContainsVertex = true;
                    break;
                }
            }
            if ( bContainsVertex ) {
                continue;
            }

            pOutputIndices[cWritten++] = iA;
            pOutputIndices[cWritten++] = iB;
            pOutputIndices[cWritten++] = iC;
            for ( usize shift = i; shift + 1u < cRemaining; ++shift ) {
                pScratchIndices[shift] = pScratchIndices[shift + 1u];
            }
            --cRemaining;
            bClippedEar = true;
            break;
        }
        if ( !bClippedEar ) {
            result.status = polygon_triangulation_status_t::DEGENERATE;
            result.cIndicesWritten = cWritten;
            result.cTriangles = cWritten / 3u;
            return result;
        }
    }

    pOutputIndices[cWritten++] = pScratchIndices[0];
    pOutputIndices[cWritten++] = pScratchIndices[1];
    pOutputIndices[cWritten++] = pScratchIndices[2];
    result.status = polygon_triangulation_status_t::OK;
    result.cIndicesWritten = cWritten;
    result.cTriangles = cWritten / 3u;
    return result;
}

} // namespace cypher::math
