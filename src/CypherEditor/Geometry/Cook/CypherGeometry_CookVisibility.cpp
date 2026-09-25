//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookVisibility.cpp
//  Purpose: Implements the visibility cook input.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookVisibility.h"
#include "CypherGeometry_CookBytes.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

void Clear( cook_visibility_t *p ) noexcept
{
    Vector_Clear( &p->hulls );
    Vector_Clear( &p->planes );
    Vector_Clear( &p->shells );
    Vector_Clear( &p->detail );
    for ( int k = 0; k < 3; ++k ) { p->worldLo[k] = p->worldHi[k] = 0.0; }
    p->contentHash = CY_CONTENT_HASH_INVALID;
    p->revision = GEOMETRY_REVISION_INITIAL;
}

} // namespace

geometry_status_t CookVisibility_Init( cook_visibility_t *p, const allocator_t *pA ) noexcept
{
    if ( p == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &p->hulls, pA ) || !Vector_Init( &p->planes, pA ) || !Vector_Init( &p->shells, pA ) || !Vector_Init( &p->detail, pA ) ) {
        CookVisibility_Shutdown( p );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    Clear( p );
    return geometry_status_t::OK;
}

void CookVisibility_Shutdown( cook_visibility_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    Vector_Shutdown( &p->hulls );
    Vector_Shutdown( &p->planes );
    Vector_Shutdown( &p->shells );
    Vector_Shutdown( &p->detail );
}

geometry_status_t CookVisibility_TryBuild( const geometry_snapshot_t *pSnap, const cook_surface_soup_t *pSoup, cook_visibility_t *pOut ) noexcept
{
    if ( !GeometrySnapshot_IsInitialized( pSnap ) || pSoup == nullptr || pOut == nullptr || pOut->hulls.pAllocator == nullptr ||
         pSoup->revision != GeometrySnapshot_GetRevision( pSnap ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    Clear( pOut );
    bool bOk = true;
    for ( usize o = 0u; bOk && o < pSoup->objects.nCount; ++o ) {
        const cook_soup_object_t &obj = pSoup->objects.pData[o];
        if ( obj.kind == cook_source_kind_t::BRUSH ) {
            const brush_solid_t *pBrush = GeometrySnapshot_FindBrush( pSnap, obj.objectId );
            if ( pBrush == nullptr ) { continue; }
            // Only a clean plane set is published.
            bool bClean = pBrush->sides.nCount >= 4u;
            for ( usize k = 0u; bClean && k < pBrush->sides.nCount; ++k ) {
                const math::planed_t &pl = pBrush->sides.pData[k].plane;
                bClean = math::Vec3d_IsFinite( pl.normal ) && std::isfinite( pl.d ) && std::fabs( math::Vec3d_LengthSquared( pl.normal ) - 1.0 ) < 1e-9;
            }
            if ( !bClean ) { continue; }
            cook_visibility_hull_t h{};
            h.brushId = obj.objectId;
            h.iFirstPlane = static_cast<u32>( pOut->planes.nCount );
            h.cPlanes = static_cast<u32>( pBrush->sides.nCount );
            for ( int k = 0; k < 3; ++k ) {
                h.lo[k] = 1e300;
                h.hi[k] = -1e300;
            }
            for ( u32 v = 0u; v < obj.cVertices; ++v ) {
                const math::vec3d_t p = pSoup->positions.pData[obj.iFirstVertex + v];
                const f64 pp[3] = { p.x, p.y, p.z };
                for ( int k = 0; k < 3; ++k ) {
                    h.lo[k] = std::fmin( h.lo[k], pp[k] );
                    h.hi[k] = std::fmax( h.hi[k], pp[k] );
                }
            }
            for ( usize k = 0u; bOk && k < pBrush->sides.nCount; ++k ) {
                bOk = Vector_PushBack( &pOut->planes, cook_visibility_plane_t{ pBrush->sides.pData[k].plane, pBrush->sides.pData[k].sourceId } );
            }
            bOk = bOk && Vector_PushBack( &pOut->hulls, h );
            continue;
        }
        const cook_visibility_range_t r{ obj.objectId, static_cast<u32>( o ), obj.iFirstTriangle, obj.cTriangles };
        bOk = Vector_PushBack( obj.bClosed ? &pOut->shells : &pOut->detail, r );
    }
    if ( !bOk ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const f64 lo[3] = { pSoup->lo.x, pSoup->lo.y, pSoup->lo.z }, hi[3] = { pSoup->hi.x, pSoup->hi.y, pSoup->hi.z };
    for ( int k = 0; k < 3; ++k ) {
        pOut->worldLo[k] = lo[k];
        pOut->worldHi[k] = hi[k];
    }
    cook_byte_writer_t w{};
    if ( !CookBytes_Init( &w, pOut->hulls.pAllocator ) ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    CookBytes_U32( &w, kCookVisibilityVersion );
    for ( usize i = 0u; i < pOut->planes.nCount; ++i ) {
        const cook_visibility_plane_t &p = pOut->planes.pData[i];
        CookBytes_F64( &w, p.plane.normal.x );
        CookBytes_F64( &w, p.plane.normal.y );
        CookBytes_F64( &w, p.plane.normal.z );
        CookBytes_F64( &w, p.plane.d );
        CookBytes_U64( &w, p.sideId.value );
    }
    for ( usize i = 0u; i < pOut->hulls.nCount; ++i ) {
        CookBytes_U64( &w, pOut->hulls.pData[i].brushId.value );
        CookBytes_U32( &w, pOut->hulls.pData[i].cPlanes );
    }
    for ( const auto *pList : { &pOut->shells, &pOut->detail } ) {
        CookBytes_U32( &w, static_cast<u32>( pList->nCount ) );
        for ( usize i = 0u; i < pList->nCount; ++i ) {
            CookBytes_U64( &w, pList->pData[i].objectId.value );
            CookBytes_U32( &w, pList->pData[i].cTriangles );
        }
    }
    pOut->contentHash = CookBytes_Hash( &w );
    CookBytes_Shutdown( &w );
    pOut->revision = pSoup->revision;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
