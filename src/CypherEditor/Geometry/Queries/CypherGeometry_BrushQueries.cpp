//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushQueries.cpp
//  Purpose: Implements brush measurement, containment, and ray queries.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushQueries.h"

namespace cypher::editor::geometry
{

namespace
{

using math::f64;
using math::vec3d_t;

common::bool_t IsBoundaryReady( const brush_boundary_t *pBoundary ) noexcept
{
    return pBoundary != nullptr && pBoundary->vertices.pAllocator != nullptr;
}

common::bool_t IsFaceValid( const brush_boundary_t *pBoundary, common::usize iFace ) noexcept
{
    const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
    const common::usize cIndices = common::Vector_Count( &pBoundary->faceVertexIndices );
    const common::usize cVertices = common::Vector_Count( &pBoundary->vertices );
    if ( face.cVertices < 3u || face.iFirstIndex > cIndices ||
         face.cVertices > cIndices - face.iFirstIndex ) {
        return false;
    }
    for ( common::u32 i = 0u; i < face.cVertices; ++i ) {
        if ( pBoundary->faceVertexIndices.pData[face.iFirstIndex + i] >= cVertices ) {
            return false;
        }
    }
    return true;
}

vec3d_t FaceNewell( const brush_boundary_t *pBoundary, common::usize iFace ) noexcept
{
    const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
    const common::u32 *pRing = pBoundary->faceVertexIndices.pData + face.iFirstIndex;
    vec3d_t n = math::CY_VEC3D_ZERO;
    for ( common::u32 i = 0u; i < face.cVertices; ++i ) {
        const vec3d_t a = pBoundary->vertices.pData[pRing[i]];
        const vec3d_t b = pBoundary->vertices.pData[pRing[( i + 1u ) % face.cVertices]];
        n.x += ( a.y - b.y ) * ( a.z + b.z );
        n.y += ( a.z - b.z ) * ( a.x + b.x );
        n.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return n;
}

f64 Length( vec3d_t v ) noexcept
{
    f64 length = 0.0;
    return math::Vec3d_TryLength( v, &length ) ? length : 0.0;
}

} // namespace

geometry_status_t BrushQuery_TryFaceArea(
    const brush_boundary_t *pBoundary,
    common::usize iFace,
    f64 *pAreaOut ) noexcept
{
    if ( pAreaOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pAreaOut = 0.0;
    if ( !IsBoundaryReady( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iFace >= common::Vector_Count( &pBoundary->faces ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !IsFaceValid( pBoundary, iFace ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    *pAreaOut = 0.5 * Length( FaceNewell( pBoundary, iFace ) );
    return geometry_status_t::OK;
}

geometry_status_t BrushQuery_TrySurfaceArea(
    const brush_boundary_t *pBoundary,
    f64 *pAreaOut ) noexcept
{
    if ( pAreaOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pAreaOut = 0.0;
    if ( !IsBoundaryReady( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::usize cFaces = common::Vector_Count( &pBoundary->faces );
    if ( cFaces == 0u ) {
        return geometry_status_t::DEGENERATE;
    }
    f64 total = 0.0;
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        if ( !IsFaceValid( pBoundary, iFace ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        total += 0.5 * Length( FaceNewell( pBoundary, iFace ) );
    }
    *pAreaOut = total;
    return geometry_status_t::OK;
}

geometry_status_t BrushQuery_TryVolumeCentroid(
    const brush_boundary_t *pBoundary,
    f64 *pVolumeOut,
    vec3d_t *pCentroidOut ) noexcept
{
    if ( pVolumeOut == nullptr || pCentroidOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pVolumeOut = 0.0;
    *pCentroidOut = math::CY_VEC3D_ZERO;
    if ( !IsBoundaryReady( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::usize cVertices = common::Vector_Count( &pBoundary->vertices );
    const common::usize cFaces = common::Vector_Count( &pBoundary->faces );
    if ( cVertices < 4u || cFaces < 4u ) {
        return geometry_status_t::DEGENERATE;
    }

    vec3d_t reference = math::CY_VEC3D_ZERO;
    for ( common::usize i = 0u; i < cVertices; ++i ) {
        reference = math::Vec3d_Add( reference, pBoundary->vertices.pData[i] );
    }
    reference = math::Vec3d_Scale( reference, 1.0 / static_cast<f64>( cVertices ) );

    f64 sixVolume = 0.0;
    vec3d_t weighted = math::CY_VEC3D_ZERO;
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        if ( !IsFaceValid( pBoundary, iFace ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        const common::u32 *pRing = pBoundary->faceVertexIndices.pData + face.iFirstIndex;
        const vec3d_t a = math::Vec3d_Subtract( pBoundary->vertices.pData[pRing[0]], reference );
        for ( common::u32 i = 1u; i + 1u < face.cVertices; ++i ) {
            const vec3d_t b = math::Vec3d_Subtract( pBoundary->vertices.pData[pRing[i]], reference );
            const vec3d_t c =
                math::Vec3d_Subtract( pBoundary->vertices.pData[pRing[i + 1u]], reference );
            const f64 tetra = math::Vec3d_Dot( a, math::Vec3d_Cross( b, c ) );
            sixVolume += tetra;
            // Tetra centroid relative to reference is (a + b + c) / 4.
            weighted = math::Vec3d_Add(
                weighted, math::Vec3d_Scale( math::Vec3d_Add( math::Vec3d_Add( a, b ), c ), tetra ) );
        }
    }
    if ( !( sixVolume > 0.0 ) ) {
        return geometry_status_t::DEGENERATE;
    }
    *pVolumeOut = sixVolume / 6.0;
    *pCentroidOut = math::Vec3d_Add(
        reference, math::Vec3d_Scale( weighted, 1.0 / ( 4.0 * sixVolume ) ) );
    return geometry_status_t::OK;
}

geometry_status_t BrushQuery_TryClassifyPoint(
    const brush_solid_t *pBrush,
    const geometry_numerical_policy_t &policy,
    vec3d_t point,
    geometry_containment_t *pContainmentOut ) noexcept
{
    if ( pContainmentOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pContainmentOut = geometry_containment_t::OUTSIDE;
    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::usize cSides = common::Vector_Count( &pBrush->sides );
    if ( cSides == 0u ) {
        return geometry_status_t::DEGENERATE;
    }
    common::bool_t bOnBoundary = false;
    for ( common::usize i = 0u; i < cSides; ++i ) {
        const geometry_classify_result_t result =
            Kernel_ClassifyPoint( policy, pBrush->sides.pData[i].plane, point );
        if ( result.status != geometry_status_t::OK ) {
            return result.status;
        }
        if ( result.orientation == geometry_orientation_t::POSITIVE ) {
            *pContainmentOut = geometry_containment_t::OUTSIDE;
            return geometry_status_t::OK;
        }
        if ( result.orientation == geometry_orientation_t::ON_PLANE ) {
            bOnBoundary = true;
        }
    }
    *pContainmentOut = bOnBoundary ? geometry_containment_t::ON_BOUNDARY
                                   : geometry_containment_t::INSIDE;
    return geometry_status_t::OK;
}

geometry_status_t BrushQuery_TryRaycast(
    const brush_solid_t *pBrush,
    const geometry_numerical_policy_t &policy,
    vec3d_t origin,
    vec3d_t direction,
    f64 maxT,
    geometry_brush_ray_hit_t *pHitOut ) noexcept
{
    if ( pHitOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pHitOut = {};
    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Vec3d_IsFinite( origin ) || !math::Vec3d_IsFinite( direction ) ||
         !math::Scalar_IsFinite( maxT ) || maxT < 0.0 ||
         math::Vec3d_LengthSquared( direction ) == 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const common::usize cSides = common::Vector_Count( &pBrush->sides );
    if ( cSides == 0u ) {
        return geometry_status_t::DEGENERATE;
    }

    const f64 tolerance = policy.fCoplanarDistanceTolerance;
    f64 tEnter = 0.0;
    f64 tExit = maxT;
    common::u32 iEnterSide = BRUSH_QUERY_SIDE_NONE;
    for ( common::usize i = 0u; i < cSides; ++i ) {
        const math::planed_t plane = pBrush->sides.pData[i].plane;
        if ( !math::Planed_IsFinite( plane ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        const f64 distance = math::Planed_SignedDistance( plane, origin );
        const f64 denominator = math::Vec3d_Dot( plane.normal, direction );
        if ( denominator == 0.0 ) {
            // Parallel: the ray stays on whichever side the origin is on.
            if ( distance > tolerance ) {
                return geometry_status_t::OK;
            }
            continue;
        }
        const f64 t = -distance / denominator;
        if ( denominator < 0.0 ) {
            // Entering this half-space. Only an origin outside the side
            // beyond tolerance produces a real entry face.
            // Strict comparison keeps the lowest side index on exact ties.
            if ( distance > tolerance && t > tEnter ) {
                tEnter = t;
                iEnterSide = static_cast<common::u32>( i );
            }
        } else if ( t < tExit ) {
            tExit = t;
        }
        if ( tEnter > tExit ) {
            return geometry_status_t::OK;
        }
    }

    pHitOut->bHit = true;
    pHitOut->t = tEnter;
    pHitOut->point = math::Vec3d_Add( origin, math::Vec3d_Scale( direction, tEnter ) );
    if ( iEnterSide == BRUSH_QUERY_SIDE_NONE ) {
        pHitOut->bStartsInside = true;
    } else {
        pHitOut->iSide = iEnterSide;
        pHitOut->normal = pBrush->sides.pData[iEnterSide].plane.normal;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
