//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Brush.cpp
//  Purpose: Implements allocation-free convex brush construction helpers.
//  Details: Triple-plane intersections use double intermediates; accepted points
//           are tested against every half-space and merged by spatial tolerance.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Brush.h"
#include "CypherMath_Intersection.h"
#include "CypherMath_Scalar.h"

#include "CypherCommon_Assert.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cypher::math
{

namespace
{

struct vec3d_t {
    f64 x;
    f64 y;
    f64 z;
};

bool_t BrushPlanesAreNormalized(
    const plane_t *pPlanes,
    usize cPlanes ) noexcept
{
    if ( pPlanes == nullptr ) {
        return false;
    }
    for ( usize i = 0u; i < cPlanes; ++i ) {
        if ( !Plane_IsNormalized(
                 pPlanes[i], CY_PLANE_UNIT_TOLERANCE ) ) {
            return false;
        }
    }
    return true;
}

bool_t BrushVerticesAreFinite(
    const vec3_t *pVertices,
    usize cVertices ) noexcept
{
    if ( pVertices == nullptr ) {
        return false;
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        if ( !Vec3_IsFinite( pVertices[i] ) ) {
            return false;
        }
    }
    return true;
}

bool_t BrushContainsPointUnchecked(
    const plane_t *pPlanes,
    usize cPlanes,
    vec3_t point,
    f32 insideTolerance ) noexcept
{
    for ( usize i = 0u; i < cPlanes; ++i ) {
        if ( Plane_SignedDistance( pPlanes[i], point ) > insideTolerance ) {
            return false;
        }
    }
    return true;
}

vec3d_t CrossDouble( vec3_t a, vec3_t b ) noexcept
{
    return Vec3d_Cross(
        Vec3d_Make(
            static_cast<f64>( a.x ),
            static_cast<f64>( a.y ),
            static_cast<f64>( a.z ) ),
        Vec3d_Make(
            static_cast<f64>( b.x ),
            static_cast<f64>( b.y ),
            static_cast<f64>( b.z ) ) );
}

f64 DotDouble( vec3_t a, vec3d_t b ) noexcept
{
    return Vec3d_Dot(
        Vec3d_Make(
            static_cast<f64>( a.x ),
            static_cast<f64>( a.y ),
            static_cast<f64>( a.z ) ),
        b );
}

bool_t BrushTryIntersectPlanesUnchecked(
    plane_t a,
    plane_t b,
    plane_t c,
    f64 minimumAbsDeterminant,
    vec3_t *pPoint ) noexcept
{
    const vec3d_t crossBC = CrossDouble( b.normal, c.normal );
    const vec3d_t crossCA = CrossDouble( c.normal, a.normal );
    const vec3d_t crossAB = CrossDouble( a.normal, b.normal );

    // Cramer's rule solves the three plane equations. A small determinant means
    // the planes do not define a numerically stable unique point.
    const f64 determinant = DotDouble( a.normal, crossBC );
    if ( std::abs( determinant ) <= minimumAbsDeterminant ) {
        return false;
    }

    const f64 inverseDeterminant = 1.0 / determinant;
    const f64 x = ( -static_cast<f64>( a.d ) * crossBC.x -
                    static_cast<f64>( b.d ) * crossCA.x -
                    static_cast<f64>( c.d ) * crossAB.x ) * inverseDeterminant;
    const f64 y = ( -static_cast<f64>( a.d ) * crossBC.y -
                    static_cast<f64>( b.d ) * crossCA.y -
                    static_cast<f64>( c.d ) * crossAB.y ) * inverseDeterminant;
    const f64 z = ( -static_cast<f64>( a.d ) * crossBC.z -
                    static_cast<f64>( b.d ) * crossCA.z -
                    static_cast<f64>( c.d ) * crossAB.z ) * inverseDeterminant;
    const vec3_t result = Vec3_Make(
        static_cast<f32>( x ), static_cast<f32>( y ), static_cast<f32>( z ) );
    if ( !Vec3_IsFinite( result ) ) {
        return false;
    }
    *pPoint = result;
    return true;
}

bool_t BrushVertexExists(
    const vec3_t *pVertices,
    usize cVertices,
    vec3_t candidate,
    f32 mergeToleranceSquared ) noexcept
{
    for ( usize i = 0u; i < cVertices; ++i ) {
        if ( Vec3_DistanceSquared( pVertices[i], candidate ) <= mergeToleranceSquared ) {
            return true;
        }
    }
    return false;
}

bool_t BrushdVertexExists(
    const vec3d_t *pVertices,
    usize cVertices,
    vec3d_t candidate,
    f64 mergeToleranceSquared ) noexcept
{
    for ( usize i = 0u; i < cVertices; ++i ) {
        if ( Vec3d_DistanceSquared( pVertices[i], candidate ) <= mergeToleranceSquared ) {
            return true;
        }
    }
    return false;
}

} // namespace

usize Brush_MaximumVertexCandidates( usize cPlanes ) noexcept
{
    if ( cPlanes < 3u ) {
        return 0u;
    }
    // Every unique triple of planes can contribute at most one vertex: C(n, 3).
    // Perform each product with an overflow guard before dividing by six.
    const usize maximum = std::numeric_limits<usize>::max();
    if ( cPlanes > maximum / ( cPlanes - 1u ) ) {
        return maximum;
    }
    const usize firstProduct = cPlanes * ( cPlanes - 1u );
    if ( firstProduct > maximum / ( cPlanes - 2u ) ) {
        return maximum;
    }
    return firstProduct * ( cPlanes - 2u ) / 6u;
}

bool_t Brush_ContainsPoint(
    const plane_t *pPlanes,
    usize cPlanes,
    vec3_t point,
    f32 insideTolerance ) noexcept
{
    if ( pPlanes == nullptr || cPlanes < 4u ||
         !Scalar_IsFinite( insideTolerance ) || insideTolerance < 0.0f ||
         !Vec3_IsFinite( point ) ) {
        return false;
    }
    return BrushPlanesAreNormalized( pPlanes, cPlanes ) &&
           BrushContainsPointUnchecked(
               pPlanes, cPlanes, point, insideTolerance );
}

bool_t Brush_TryIntersectPlanes(
    plane_t a,
    plane_t b,
    plane_t c,
    f64 minimumAbsDeterminant,
    vec3_t *pPoint ) noexcept
{
    const bool_t bValidOutput = pPoint != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Brush_TryIntersectPlanes requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pPoint = CY_VEC3_ZERO;
    if ( !Scalar_IsFinite( minimumAbsDeterminant ) ||
         minimumAbsDeterminant < 0.0 ||
         !Plane_IsNormalized( a, CY_PLANE_UNIT_TOLERANCE ) ||
         !Plane_IsNormalized( b, CY_PLANE_UNIT_TOLERANCE ) ||
         !Plane_IsNormalized( c, CY_PLANE_UNIT_TOLERANCE ) ) {
        return false;
    }

    return BrushTryIntersectPlanesUnchecked(
        a, b, c, minimumAbsDeterminant, pPoint );
}

brush_vertex_result_t Brush_BuildVertices(
    const plane_t *pPlanes,
    usize cPlanes,
    f64 minimumAbsDeterminant,
    f32 insideTolerance,
    f32 mergeTolerance,
    vec3_t *pOutputVertices,
    usize cOutputVertices ) noexcept
{
    brush_vertex_result_t result{};
    result.status = brush_build_status_t::INVALID_ARGUMENT;
    if ( cPlanes < 4u || pOutputVertices == nullptr || cOutputVertices == 0u ||
         !Scalar_IsFinite( minimumAbsDeterminant ) ||
         minimumAbsDeterminant < 0.0 || !Scalar_IsFinite( insideTolerance ) ||
         insideTolerance < 0.0f || !Scalar_IsFinite( mergeTolerance ) ||
         mergeTolerance < 0.0f ||
         !BrushPlanesAreNormalized( pPlanes, cPlanes ) ) {
        return result;
    }
    
    const f32 mergeToleranceSquared = mergeTolerance * mergeTolerance;

    // Enumerate plane triples, then keep only intersections inside every
    // outward-facing half-space. Spatial merging collapses shared triples.
    for ( usize i = 0u; i + 2u < cPlanes; ++i ) {
        for ( usize j = i + 1u; j + 1u < cPlanes; ++j ) {
            for ( usize k = j + 1u; k < cPlanes; ++k ) {
                vec3_t candidate{};
                if ( !BrushTryIntersectPlanesUnchecked(
                         pPlanes[i], pPlanes[j], pPlanes[k],
                         minimumAbsDeterminant, &candidate ) ||
                     !BrushContainsPointUnchecked(
                         pPlanes, cPlanes, candidate, insideTolerance ) ||
                     BrushVertexExists(
                         pOutputVertices, result.cVerticesWritten,
                         candidate, mergeToleranceSquared ) ) {
                    continue;
                }
                if ( result.cVerticesWritten >= cOutputVertices ) {
                    result.status = brush_build_status_t::INSUFFICIENT_CAPACITY;
                    return result;
                }
                pOutputVertices[result.cVerticesWritten++] = candidate;
            }
        }
    }

    result.status = result.cVerticesWritten >= 4u
        ? brush_build_status_t::OK
        : brush_build_status_t::DEGENERATE;
    return result;
}

brush_vertex_result_t Brush_BuildFacePolygon(
    plane_t outwardFacePlane,
    const vec3_t *pBrushVertices,
    usize cBrushVertices,
    f32 faceDistanceTolerance,
    f32 minimumNormalLength,
    vec3_t *pOutputVertices,
    usize cOutputVertices ) noexcept
{
    brush_vertex_result_t result{};
    result.status = brush_build_status_t::INVALID_ARGUMENT;
    if ( cBrushVertices < 4u || pOutputVertices == nullptr ||
         cOutputVertices == 0u || !Scalar_IsFinite( faceDistanceTolerance ) ||
         faceDistanceTolerance < 0.0f ||
         !Scalar_IsFinite( minimumNormalLength ) ||
         minimumNormalLength < 0.0f ||
         !Plane_IsValid( outwardFacePlane, 0.0f ) ||
         !BrushVerticesAreFinite( pBrushVertices, cBrushVertices ) ) {
        return result;
    }

    plane_t face{};
    if ( !Plane_TryNormalize( outwardFacePlane, minimumNormalLength, &face ) ) {
        result.status = brush_build_status_t::DEGENERATE;
        return result;
    }
    for ( usize i = 0u; i < cBrushVertices; ++i ) {
        if ( std::abs( Plane_SignedDistance( face, pBrushVertices[i] ) ) <=
             faceDistanceTolerance ) {
            if ( result.cVerticesWritten >= cOutputVertices ) {
                result.status = brush_build_status_t::INSUFFICIENT_CAPACITY;
                return result;
            }
            pOutputVertices[result.cVerticesWritten++] = pBrushVertices[i];
        }
    }
    if ( result.cVerticesWritten < 3u ) {
        result.status = brush_build_status_t::DEGENERATE;
        return result;
    }

    vec3_t centroid = CY_VEC3_ZERO;
    for ( usize i = 0u; i < result.cVerticesWritten; ++i ) {
        centroid = Vec3_Add( centroid, pOutputVertices[i] );
    }
    
    centroid = Vec3_Scale(
        centroid, 1.0f / static_cast<f32>( result.cVerticesWritten ) );

    vec3_t tangent{};
    vec3_t bitangent{};
    Vec3_BuildOrthonormalBasis( face.normal, &tangent, &bitangent );

    // Sort face vertices by polar angle around their centroid. The resulting
    // winding follows the outward face basis and is ready for triangulation.
    for ( usize i = 1u; i < result.cVerticesWritten; ++i ) {
        const vec3_t value = pOutputVertices[i];
        const vec3_t relative = Vec3_Subtract( value, centroid );
        const f32 angle = std::atan2(
            Vec3_Dot( relative, bitangent ),
            Vec3_Dot( relative, tangent ) );
        usize j = i;
        while ( j > 0u ) {
            const vec3_t previousRelative = Vec3_Subtract(
                pOutputVertices[j - 1u], centroid );
            const f32 previousAngle = std::atan2(
                Vec3_Dot( previousRelative, bitangent ),
                Vec3_Dot( previousRelative, tangent ) );
            if ( previousAngle <= angle ) {
                break;
            }
            pOutputVertices[j] = pOutputVertices[j - 1u];
            --j;
        }
        pOutputVertices[j] = value;
    }

    result.status = brush_build_status_t::OK;
    return result;
}

bool_t Brush_TryBounds(
    const vec3_t *pVertices,
    usize cVertices,
    aabb_t *pBounds ) noexcept
{
    const bool_t bValidOutput = pBounds != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Brush_TryBounds requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pBounds = CY_AABB_EMPTY;
    if ( pVertices == nullptr || cVertices == 0u ) {
        return false;
    }
    aabb_t bounds = CY_AABB_EMPTY;
    for ( usize i = 0u; i < cVertices; ++i ) {
        if ( !Vec3_IsFinite( pVertices[i] ) ) {
            return false;
        }
        bounds = Aabb_ExpandPoint( bounds, pVertices[i] );
    }
    *pBounds = bounds;
    return true;
}

//==========================================================================
// Binary64 authoring brush
//==========================================================================

bool_t Brushd_ContainsPoint(
    const planed_t *pPlanes,
    usize cPlanes,
    vec3d_t point,
    f64 insideTolerance ) noexcept
{
    if ( pPlanes == nullptr || cPlanes < 4u || insideTolerance < 0.0 ||
         !Vec3d_IsFinite( point ) ) {
        return false;
    }
    for ( usize i = 0u; i < cPlanes; ++i ) {
        if ( !Planed_IsFinite( pPlanes[i] ) ||
             Planed_SignedDistance( pPlanes[i], point ) > insideTolerance ) {
            return false;
        }
    }
    return true;
}

brush_vertex_result_t Brushd_BuildVertices(
    const planed_t *pPlanes,
    usize cPlanes,
    f64 minimumAbsDeterminant,
    f64 insideTolerance,
    f64 mergeTolerance,
    vec3d_t *pOutputVertices,
    usize cOutputVertices ) noexcept
{
    brush_vertex_result_t result{};
    result.status = brush_build_status_t::INVALID_ARGUMENT;
    if ( pPlanes == nullptr || cPlanes < 4u || pOutputVertices == nullptr ||
         cOutputVertices == 0u || minimumAbsDeterminant < 0.0 ||
         insideTolerance < 0.0 || mergeTolerance < 0.0 ) {
        return result;
    }

    const f64 mergeToleranceSquared = mergeTolerance * mergeTolerance;

    // Enumerate plane triples, then keep only intersections inside every
    // outward-facing half-space. Spatial merging collapses shared triples.
    for ( usize i = 0u; i + 2u < cPlanes; ++i ) {
        for ( usize j = i + 1u; j + 1u < cPlanes; ++j ) {
            for ( usize k = j + 1u; k < cPlanes; ++k ) {
                vec3d_t candidate{};
                if ( !Intersection_TryThreePlanesD(
                         pPlanes[i], pPlanes[j], pPlanes[k],
                         minimumAbsDeterminant, &candidate, nullptr ) ||
                     !Brushd_ContainsPoint(
                         pPlanes, cPlanes, candidate, insideTolerance ) ||
                     BrushdVertexExists(
                         pOutputVertices, result.cVerticesWritten,
                         candidate, mergeToleranceSquared ) ) {
                    continue;
                }
                if ( result.cVerticesWritten >= cOutputVertices ) {
                    result.status = brush_build_status_t::INSUFFICIENT_CAPACITY;
                    return result;
                }
                pOutputVertices[result.cVerticesWritten++] = candidate;
            }
        }
    }

    result.status = result.cVerticesWritten >= 4u
        ? brush_build_status_t::OK
        : brush_build_status_t::DEGENERATE;
    return result;
}

brush_vertex_result_t Brushd_BuildFacePolygon(
    planed_t outwardFacePlane,
    const vec3d_t *pBrushVertices,
    usize cBrushVertices,
    f64 faceDistanceTolerance,
    f64 minimumNormalLength,
    vec3d_t *pOutputVertices,
    usize cOutputVertices ) noexcept
{
    brush_vertex_result_t result{};
    result.status = brush_build_status_t::INVALID_ARGUMENT;
    if ( pBrushVertices == nullptr || cBrushVertices < 4u ||
         pOutputVertices == nullptr || cOutputVertices == 0u ||
         faceDistanceTolerance < 0.0 || minimumNormalLength < 0.0 ) {
        return result;
    }

    planed_t face{};
    if ( !Planed_TryNormalize( outwardFacePlane, minimumNormalLength, &face ) ) {
        result.status = brush_build_status_t::DEGENERATE;
        return result;
    }
    for ( usize i = 0u; i < cBrushVertices; ++i ) {
        if ( std::abs( Planed_SignedDistance( face, pBrushVertices[i] ) ) <=
             faceDistanceTolerance ) {
            if ( result.cVerticesWritten >= cOutputVertices ) {
                result.status = brush_build_status_t::INSUFFICIENT_CAPACITY;
                return result;
            }
            pOutputVertices[result.cVerticesWritten++] = pBrushVertices[i];
        }
    }
    if ( result.cVerticesWritten < 3u ) {
        result.status = brush_build_status_t::DEGENERATE;
        return result;
    }

    vec3d_t centroid = CY_VEC3D_ZERO;
    for ( usize i = 0u; i < result.cVerticesWritten; ++i ) {
        centroid = Vec3d_Add( centroid, pOutputVertices[i] );
    }

    centroid = Vec3d_Scale(
        centroid, 1.0 / static_cast<f64>( result.cVerticesWritten ) );

    vec3d_t tangent{};
    vec3d_t bitangent{};
    Vec3d_BuildOrthonormalBasis( face.normal, &tangent, &bitangent );

    // Sort face vertices by polar angle around their centroid. The resulting
    // winding follows the outward face basis and is ready for triangulation.
    for ( usize i = 1u; i < result.cVerticesWritten; ++i ) {
        const vec3d_t value = pOutputVertices[i];
        const vec3d_t relative = Vec3d_Subtract( value, centroid );
        const f64 angle = std::atan2(
            Vec3d_Dot( relative, bitangent ),
            Vec3d_Dot( relative, tangent ) );
        usize j = i;
        while ( j > 0u ) {
            const vec3d_t previousRelative = Vec3d_Subtract(
                pOutputVertices[j - 1u], centroid );
            const f64 previousAngle = std::atan2(
                Vec3d_Dot( previousRelative, bitangent ),
                Vec3d_Dot( previousRelative, tangent ) );
            if ( previousAngle <= angle ) {
                break;
            }
            pOutputVertices[j] = pOutputVertices[j - 1u];
            --j;
        }
        pOutputVertices[j] = value;
    }

    result.status = brush_build_status_t::OK;
    return result;
}

bool_t Brushd_TryBounds(
    const vec3d_t *pVertices,
    usize cVertices,
    aabbd_t *pBounds ) noexcept
{
    const bool_t bValidOutput = pBounds != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Brushd_TryBounds requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pBounds = CY_AABBD_EMPTY;
    if ( pVertices == nullptr || cVertices == 0u ) {
        return false;
    }
    aabbd_t bounds = CY_AABBD_EMPTY;
    for ( usize i = 0u; i < cVertices; ++i ) {
        if ( !Vec3d_IsFinite( pVertices[i] ) ) {
            return false;
        }
        bounds = Aabbd_ExpandPoint( bounds, pVertices[i] );
    }
    *pBounds = bounds;
    return true;
}

} // namespace cypher::math
