//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Clip.cpp
//  Purpose: Implements plane clipping used by mesh and block authoring tools.
//  Details: Polygon clipping uses Sutherland-Hodgman and segment clipping uses
//           a parametric interval against convex outward-facing planes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Clip.h"

#include "CypherCommon_Assert.h"

#include <algorithm>
#include <cmath>

namespace cypher::math
{

namespace
{

bool_t AppendVertex(
    vec3_t vertex,
    vec3_t *pOutputVertices,
    usize cOutputVertices,
    usize *pWritten ) noexcept
{
    if ( *pWritten >= cOutputVertices ) {
        return false;
    }
    pOutputVertices[( *pWritten )++] = vertex;
    return true;
}

} // namespace

polygon_clip_result_t Clip_PolygonAgainstPlane(
    const vec3_t *pVertices,
    usize cVertices,
    plane_t outwardPlane,
    f32 insideTolerance,
    vec3_t *pOutputVertices,
    usize cOutputVertices ) noexcept
{
    polygon_clip_result_t result{};
    result.status = polygon_clip_status_t::INVALID_ARGUMENT;
    if ( pVertices == nullptr || cVertices < 3u || pOutputVertices == nullptr ||
         pVertices == pOutputVertices || cOutputVertices == 0u ||
         !Plane_IsFinite( outwardPlane ) || insideTolerance < 0.0f ||
         !Scalar_IsFinite( insideTolerance ) ) {
        return result;
    }

    vec3_t previous = pVertices[cVertices - 1u];
    if ( !Vec3_IsFinite( previous ) ) {
        return result;
    }
    f32 previousDistance = Plane_SignedDistance( outwardPlane, previous );
    bool_t bPreviousInside = previousDistance <= insideTolerance;
    for ( usize i = 0u; i < cVertices; ++i ) {
        const vec3_t current = pVertices[i];
        if ( !Vec3_IsFinite( current ) ) {
            result.cVerticesWritten = 0u;
            return result;
        }
        const f32 currentDistance = Plane_SignedDistance( outwardPlane, current );
        const bool_t bCurrentInside = currentDistance <= insideTolerance;

        if ( bCurrentInside != bPreviousInside ) {
            const f64 denominator =
                static_cast<f64>( currentDistance ) - previousDistance;
            if ( denominator != 0.0 ) {
                const f64 parameter = std::clamp(
                    ( static_cast<f64>( insideTolerance ) - previousDistance ) /
                        denominator,
                    0.0, 1.0 );
                const vec3_t intersection = Vec3_Lerp(
                    previous, current, static_cast<f32>( parameter ) );
                if ( !AppendVertex(
                         intersection, pOutputVertices, cOutputVertices,
                         &result.cVerticesWritten ) ) {
                    result.status = polygon_clip_status_t::INSUFFICIENT_CAPACITY;
                    return result;
                }
            }
        }
        if ( bCurrentInside &&
             !AppendVertex(
                 current, pOutputVertices, cOutputVertices,
                 &result.cVerticesWritten ) ) {
            result.status = polygon_clip_status_t::INSUFFICIENT_CAPACITY;
            return result;
        }

        previous = current;
        previousDistance = currentDistance;
        bPreviousInside = bCurrentInside;
    }

    result.status = result.cVerticesWritten >= 3u
        ? polygon_clip_status_t::OK
        : polygon_clip_status_t::FULLY_CLIPPED;
    return result;
}

bool_t Clip_TrySegmentAgainstConvexPlanes(
    segment_t segment,
    const plane_t *pPlanes,
    usize cPlanes,
    f32 insideTolerance,
    f32 minimumAbsDenominator,
    segment_clip_result_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Clip_TrySegmentAgainstConvexPlanes requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = {};
    if ( !Segment_IsFinite( segment ) || pPlanes == nullptr || cPlanes == 0u ||
         insideTolerance < 0.0f || minimumAbsDenominator < 0.0f ||
         !Scalar_IsFinite( insideTolerance ) ||
         !Scalar_IsFinite( minimumAbsDenominator ) ) {
        return false;
    }

    const vec3_t direction = Segment_Direction( segment );
    f64 enter = 0.0;
    f64 exit = 1.0;
    for ( usize i = 0u; i < cPlanes; ++i ) {
        if ( !Plane_IsFinite( pPlanes[i] ) ) {
            return false;
        }
        const f64 startDistance = Plane_SignedDistance( pPlanes[i], segment.start );
        const f64 denominator = Vec3_Dot( pPlanes[i].normal, direction );
        if ( std::abs( denominator ) <= minimumAbsDenominator ) {
            if ( startDistance > insideTolerance ) {
                return false;
            }
            continue;
        }

        const f64 parameter =
            ( static_cast<f64>( insideTolerance ) - startDistance ) / denominator;
        if ( denominator < 0.0 ) {
            enter = std::max( enter, parameter );
        } else {
            exit = std::min( exit, parameter );
        }
        if ( enter > exit ) {
            return false;
        }
    }

    enter = std::clamp( enter, 0.0, 1.0 );
    exit = std::clamp( exit, 0.0, 1.0 );
    *pResult = {
        Segment_Make(
            Segment_PointAt( segment, static_cast<f32>( enter ) ),
            Segment_PointAt( segment, static_cast<f32>( exit ) ) ),
        static_cast<f32>( enter ),
        static_cast<f32>( exit )
    };
    return true;
}

} // namespace cypher::math
