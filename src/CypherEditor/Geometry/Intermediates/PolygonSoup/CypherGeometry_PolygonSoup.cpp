//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PolygonSoup.cpp
//  Purpose: Implements PolygonSoup storage and per-face validation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PolygonSoup.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

bool PolygonSoup_IsInitialized( const polygon_soup_t *pSoup ) noexcept
{
    return pSoup != nullptr && pSoup->positions.pAllocator != nullptr;
}

geometry_status_t PolygonSoup_Init(
    polygon_soup_t *pSoup,
    const allocator_t *pAllocator ) noexcept
{
    if ( pSoup == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( PolygonSoup_IsInitialized( pSoup ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !Vector_Init( &pSoup->positions, pAllocator ) ||
         !Vector_Init( &pSoup->corners, pAllocator ) ||
         !Vector_Init( &pSoup->faces, pAllocator ) ) {
        PolygonSoup_Shutdown( pSoup );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void PolygonSoup_Shutdown( polygon_soup_t *pSoup ) noexcept
{
    if ( pSoup == nullptr ) { return; }
    Vector_Shutdown( &pSoup->positions );
    Vector_Shutdown( &pSoup->corners );
    Vector_Shutdown( &pSoup->faces );
}

void PolygonSoup_Clear( polygon_soup_t *pSoup ) noexcept
{
    if ( !PolygonSoup_IsInitialized( pSoup ) ) { return; }
    Vector_Clear( &pSoup->positions );
    Vector_Clear( &pSoup->corners );
    Vector_Clear( &pSoup->faces );
}

geometry_status_t PolygonSoup_TryAddVertex(
    polygon_soup_t *pSoup,
    math::vec3d_t position,
    u32 *pIndexOut ) noexcept
{
    if ( !PolygonSoup_IsInitialized( pSoup ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pSoup->positions.nCount >= kPolygonSoupVerticesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !Vector_PushBack( &pSoup->positions, position ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( pIndexOut ) { *pIndexOut = static_cast<u32>( pSoup->positions.nCount - 1u ); }
    return geometry_status_t::OK;
}

geometry_status_t PolygonSoup_TryAddFace(
    polygon_soup_t *pSoup,
    span_t<const u32> vertexIndices,
    geometry_source_id_t sourceId,
    u32 iGroup,
    u32 *pFaceIndexOut ) noexcept
{
    if ( !PolygonSoup_IsInitialized( pSoup ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( vertexIndices.pData == nullptr || vertexIndices.nCount == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( vertexIndices.nCount > kPolygonSoupFaceCornersMax ||
         pSoup->faces.nCount >= kPolygonSoupFacesMax ||
         pSoup->corners.nCount + vertexIndices.nCount > kPolygonSoupCornersMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !Vector_Reserve( &pSoup->corners, pSoup->corners.nCount + vertexIndices.nCount ) ||
         !Vector_Reserve( &pSoup->faces, pSoup->faces.nCount + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    polygon_soup_face_t face{};
    face.iFirstCorner = static_cast<u32>( pSoup->corners.nCount );
    face.cCorners = static_cast<u32>( vertexIndices.nCount );
    face.sourceId = sourceId;
    face.iGroup = iGroup;
    for ( usize i = 0u; i < vertexIndices.nCount; ++i ) {
        (void)Vector_PushBack( &pSoup->corners, vertexIndices.pData[i] );
    }
    (void)Vector_PushBack( &pSoup->faces, face );
    if ( pFaceIndexOut ) { *pFaceIndexOut = static_cast<u32>( pSoup->faces.nCount - 1u ); }
    return geometry_status_t::OK;
}

usize PolygonSoup_VertexCount( const polygon_soup_t *pSoup ) noexcept
{
    return PolygonSoup_IsInitialized( pSoup ) ? pSoup->positions.nCount : 0u;
}

usize PolygonSoup_FaceCount( const polygon_soup_t *pSoup ) noexcept
{
    return PolygonSoup_IsInitialized( pSoup ) ? pSoup->faces.nCount : 0u;
}

span_t<const u32> PolygonSoup_FaceCorners(
    const polygon_soup_t *pSoup,
    usize iFace ) noexcept
{
    if ( !PolygonSoup_IsInitialized( pSoup ) || iFace >= pSoup->faces.nCount ) {
        return {};
    }
    const polygon_soup_face_t &f = pSoup->faces.pData[iFace];
    return span_t<const u32>{ pSoup->corners.pData + f.iFirstCorner, f.cCorners };
}

math::vec3d_t PolygonSoup_FaceVectorArea(
    const polygon_soup_t *pSoup,
    usize iFace ) noexcept
{
    const span_t<const u32> c = PolygonSoup_FaceCorners( pSoup, iFace );
    math::vec3d_t n = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    const usize cV = PolygonSoup_VertexCount( pSoup );
    for ( usize i = 0u; i < c.nCount; ++i ) {
        const u32 ia = c.pData[i];
        const u32 ib = c.pData[( i + 1u ) % c.nCount];
        if ( ia >= cV || ib >= cV ) { return math::Vec3d_Make( 0.0, 0.0, 0.0 ); }
        const math::vec3d_t a = pSoup->positions.pData[ia];
        const math::vec3d_t b = pSoup->positions.pData[ib];
        n.x += ( a.y - b.y ) * ( a.z + b.z );
        n.y += ( a.z - b.z ) * ( a.x + b.x );
        n.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return n;
}

const char *PolygonSoupFault_Name( polygon_soup_fault_t fault ) noexcept
{
    switch ( fault ) {
        case polygon_soup_fault_t::NONE: return "none";
        case polygon_soup_fault_t::NON_FINITE: return "non_finite";
        case polygon_soup_fault_t::INDEX_OUT_OF_RANGE: return "index_out_of_range";
        case polygon_soup_fault_t::TOO_FEW_CORNERS: return "too_few_corners";
        case polygon_soup_fault_t::REPEATED_CORNER: return "repeated_corner";
        case polygon_soup_fault_t::DEGENERATE_AREA: return "degenerate_area";
    }
    return "unknown";
}

polygon_soup_validation_t PolygonSoup_Validate(
    const polygon_soup_t *pSoup,
    f64 fMinimumFaceArea ) noexcept
{
    polygon_soup_validation_t r{};
    if ( !PolygonSoup_IsInitialized( pSoup ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    auto record = [&]( polygon_soup_fault_t f, geometry_status_t s,
                       u32 iFace, u32 iCorner, u32 iVertex ) noexcept {
        if ( r.fault != polygon_soup_fault_t::NONE ) { return; }
        r.fault = f;
        r.status = s;
        r.iFace = iFace;
        r.iCorner = iCorner;
        r.iVertex = iVertex;
    };

    const usize cV = pSoup->positions.nCount;
    for ( usize v = 0u; v < cV; ++v ) {
        if ( !math::Vec3d_IsFinite( pSoup->positions.pData[v] ) ) {
            ++r.cNonFiniteVertices;
            record( polygon_soup_fault_t::NON_FINITE, geometry_status_t::NUMERIC_FAILURE,
                    CY_INVALID_INDEX, CY_INVALID_INDEX, static_cast<u32>( v ) );
        }
    }

    for ( usize fi = 0u; fi < pSoup->faces.nCount; ++fi ) {
        const span_t<const u32> c = PolygonSoup_FaceCorners( pSoup, fi );
        const u32 iFace = static_cast<u32>( fi );
        bool faulty = false;
        if ( c.nCount < 3u ) {
            record( polygon_soup_fault_t::TOO_FEW_CORNERS, geometry_status_t::DEGENERATE,
                    iFace, CY_INVALID_INDEX, CY_INVALID_INDEX );
            faulty = true;
        }
        for ( usize k = 0u; k < c.nCount && !faulty; ++k ) {
            if ( c.pData[k] >= cV ) {
                record( polygon_soup_fault_t::INDEX_OUT_OF_RANGE,
                        geometry_status_t::INVALID_ARGUMENT, iFace, static_cast<u32>( k ),
                        c.pData[k] );
                faulty = true;
            }
        }
        // A vertex used twice by one face makes it non-simple (a pinched
        // polygon); O(k^2) with k <= 256 corners.
        for ( usize k = 0u; k < c.nCount && !faulty; ++k ) {
            for ( usize m = k + 1u; m < c.nCount; ++m ) {
                if ( c.pData[k] == c.pData[m] ) {
                    record( polygon_soup_fault_t::REPEATED_CORNER, geometry_status_t::DEGENERATE,
                            iFace, static_cast<u32>( m ), c.pData[m] );
                    faulty = true;
                    break;
                }
            }
        }
        if ( !faulty ) {
            const f64 area2 = std::sqrt(
                math::Vec3d_LengthSquared( PolygonSoup_FaceVectorArea( pSoup, fi ) ) );
            if ( !( 0.5 * area2 > fMinimumFaceArea ) ) {
                record( polygon_soup_fault_t::DEGENERATE_AREA, geometry_status_t::DEGENERATE,
                        iFace, CY_INVALID_INDEX, CY_INVALID_INDEX );
                faulty = true;
            }
        }
        if ( faulty ) { ++r.cFaultyFaces; }
    }
    return r;
}

} // namespace cypher::editor::geometry
