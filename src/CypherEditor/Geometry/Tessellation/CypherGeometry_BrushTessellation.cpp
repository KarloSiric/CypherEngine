//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTessellation.cpp
//  Purpose: Implements fan triangulation of convex brush faces.
//  Details: Capacity for the worst case (sum of n - 2 over faces) is
//           reserved before any triangle is written, so the build loop
//           cannot fail midway.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushTessellation.h"

namespace cypher::editor::geometry
{

namespace
{

common::bool_t IsInitialized( const geometry_brush_tessellation_t *pTessellation ) noexcept
{
    return pTessellation != nullptr && pTessellation->triangles.pAllocator != nullptr;
}

} // namespace

geometry_status_t BrushTessellation_Init(
    geometry_brush_tessellation_t *pTessellation,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pTessellation == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( IsInitialized( pTessellation ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return common::Vector_Init( &pTessellation->triangles, pAllocator, 0u )
        ? geometry_status_t::OK
        : geometry_status_t::ALLOCATION_FAILED;
}

void BrushTessellation_Shutdown( geometry_brush_tessellation_t *pTessellation ) noexcept
{
    if ( IsInitialized( pTessellation ) ) {
        common::Vector_Shutdown( &pTessellation->triangles );
    }
}

geometry_status_t BrushTessellation_TryBuild(
    geometry_brush_tessellation_t *pTessellation,
    const brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const geometry_policy_t &policy ) noexcept
{
    if ( !IsInitialized( pTessellation ) || pBrush == nullptr ||
         pBrush->sides.pAllocator == nullptr || pBoundary == nullptr ||
         pBoundary->vertices.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    common::Vector_Clear( &pTessellation->triangles );

    const common::usize cFaces = common::Vector_Count( &pBoundary->faces );
    const common::usize cVertices = common::Vector_Count( &pBoundary->vertices );
    const common::usize cSides = common::Vector_Count( &pBrush->sides );
    const common::usize cIndices = common::Vector_Count( &pBoundary->faceVertexIndices );
    if ( cFaces == 0u ) {
        return geometry_status_t::DEGENERATE;
    }

    // Validate every reference and size the output before writing.
    common::usize cMaxTriangles = 0u;
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        if ( face.iSide >= cSides || face.cVertices < 3u ||
             face.iFirstIndex > cIndices || face.cVertices > cIndices - face.iFirstIndex ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        for ( common::u32 i = 0u; i < face.cVertices; ++i ) {
            if ( pBoundary->faceVertexIndices.pData[face.iFirstIndex + i] >= cVertices ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
        cMaxTriangles += face.cVertices - 2u;
    }
    if ( !common::Vector_Reserve( &pTessellation->triangles, cMaxTriangles ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const math::f64 twiceMinimumArea = 2.0 * policy.numerical.fMinimumFaceArea;
    const math::f64 thresholdSq = twiceMinimumArea * twiceMinimumArea;
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        const common::u32 *pRing = pBoundary->faceVertexIndices.pData + face.iFirstIndex;
        const math::vec3d_t v0 = pBoundary->vertices.pData[pRing[0]];
        for ( common::u32 i = 1u; i + 1u < face.cVertices; ++i ) {
            const math::vec3d_t v1 = pBoundary->vertices.pData[pRing[i]];
            const math::vec3d_t v2 = pBoundary->vertices.pData[pRing[i + 1u]];
            const math::vec3d_t cross = math::Vec3d_Cross(
                math::Vec3d_Subtract( v1, v0 ), math::Vec3d_Subtract( v2, v0 ) );
            if ( !( math::Vec3d_LengthSquared( cross ) >= thresholdSq ) ) {
                continue;
            }
            const geometry_brush_triangle_t triangle{
                pRing[0], pRing[i], pRing[i + 1u],
                face.iSide, pBrush->sides.pData[face.iSide].sourceId };
            ( void )common::Vector_PushBack( &pTessellation->triangles, triangle );
        }
    }
    return geometry_status_t::OK;
}

common::usize BrushTessellation_TriangleCount(
    const geometry_brush_tessellation_t *pTessellation ) noexcept
{
    return IsInitialized( pTessellation ) ? common::Vector_Count( &pTessellation->triangles ) : 0u;
}

geometry_status_t BrushTessellation_TryGetTriangle(
    const geometry_brush_tessellation_t *pTessellation,
    common::usize iTriangle,
    geometry_brush_triangle_t *pTriangleOut ) noexcept
{
    if ( pTriangleOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pTriangleOut = {};
    if ( !IsInitialized( pTessellation ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iTriangle >= common::Vector_Count( &pTessellation->triangles ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pTriangleOut = pTessellation->triangles.pData[iTriangle];
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
