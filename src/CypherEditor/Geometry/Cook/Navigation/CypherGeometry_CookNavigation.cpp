//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookNavigation.cpp
//  Purpose: Implements the navigation cook input.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookNavigation.h"
#include "CypherGeometry_CookBytes.h"

#include <cfloat>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool ToFloat( math::vec3d_t p, cook_float3_t *pOut ) noexcept
{
    const f64 m = static_cast<f64>( FLT_MAX );
    if ( !math::Vec3d_IsFinite( p ) || std::fabs( p.x ) > m || std::fabs( p.y ) > m || std::fabs( p.z ) > m ) { return false; }
    *pOut = cook_float3_t{ static_cast<f32>( p.x ), static_cast<f32>( p.y ), static_cast<f32>( p.z ) };
    return true;
}

void Clear( cook_navigation_t *p ) noexcept
{
    Vector_Clear( &p->positions );
    Vector_Clear( &p->indices );
    Vector_Clear( &p->triangleObject );
    Vector_Clear( &p->triangleElement );
    Vector_Clear( &p->triangleSlope );
    p->cSkippedFloatRange = 0u;
    p->contentHash = CY_CONTENT_HASH_INVALID;
    p->revision = GEOMETRY_REVISION_INITIAL;
}

} // namespace

geometry_status_t CookNavigation_Init( cook_navigation_t *p, const allocator_t *pA ) noexcept
{
    if ( p == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &p->positions, pA ) || !Vector_Init( &p->indices, pA ) || !Vector_Init( &p->triangleObject, pA ) ||
         !Vector_Init( &p->triangleElement, pA ) || !Vector_Init( &p->triangleSlope, pA ) ) {
        CookNavigation_Shutdown( p );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    Clear( p );
    return geometry_status_t::OK;
}

void CookNavigation_Shutdown( cook_navigation_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    Vector_Shutdown( &p->positions );
    Vector_Shutdown( &p->indices );
    Vector_Shutdown( &p->triangleObject );
    Vector_Shutdown( &p->triangleElement );
    Vector_Shutdown( &p->triangleSlope );
}

geometry_status_t CookNavigation_TryBuild( const cook_surface_soup_t *pSoup, const cook_navigation_options_t &o, cook_navigation_t *pOut ) noexcept
{
    if ( pSoup == nullptr || pOut == nullptr || pOut->positions.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const f64 upLen = std::sqrt( math::Vec3d_LengthSquared( o.up ) );
    if ( !std::isfinite( upLen ) || std::fabs( upLen - 1.0 ) > 1e-9 || !std::isfinite( o.maxSlopeRadians ) || o.maxSlopeRadians < 0.0 ||
         o.maxSlopeRadians > 0.5 * math::CY_PI_D || !std::isfinite( o.minArea ) || o.minArea < 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    Clear( pOut );
    const allocator_t *pA = pOut->positions.pAllocator;
    // Soup vertex -> output vertex. Soup vertices belong to one object
    // each, so candidates of different objects never share a vertex.
    vector_t<u32> remap{};
    if ( !Vector_Init( &remap, pA ) || !Vector_Resize( &remap, pSoup->positions.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize v = 0u; v < remap.nCount; ++v ) { remap.pData[v] = CY_U32_MAX; }
    const f64 cosLimit = std::cos( o.maxSlopeRadians );
    bool bOk = true;
    for ( usize t = 0u; bOk && t < pSoup->triangles.nCount; ++t ) {
        const cook_soup_triangle_t &tri = pSoup->triangles.pData[t];
        const f64 up = math::Vec3d_Dot( tri.normal, o.up );
        if ( tri.area < o.minArea || up < cosLimit - 1e-12 ) { continue; }
        cook_float3_t f[3];
        bool bFits = true;
        for ( u32 k = 0u; k < 3u && bFits; ++k ) { bFits = ToFloat( pSoup->positions.pData[tri.v[k]], &f[k] ); }
        if ( !bFits ) {
            ++pOut->cSkippedFloatRange;
            continue;
        }
        for ( u32 k = 0u; bOk && k < 3u; ++k ) {
            u32 &slot = remap.pData[tri.v[k]];
            if ( slot == CY_U32_MAX ) {
                slot = static_cast<u32>( pOut->positions.nCount );
                bOk = Vector_PushBack( &pOut->positions, f[k] );
            }
            bOk = bOk && Vector_PushBack( &pOut->indices, slot );
        }
        const f64 slope = std::acos( up > 1.0 ? 1.0 : up );
        bOk = bOk && Vector_PushBack( &pOut->triangleObject, pSoup->objects.pData[tri.iObject].objectId ) &&
              Vector_PushBack( &pOut->triangleElement, tri.elementId ) && Vector_PushBack( &pOut->triangleSlope, static_cast<f32>( slope ) );
    }
    if ( !bOk ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    cook_byte_writer_t w{};
    if ( !CookBytes_Init( &w, pA ) ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    CookBytes_U32( &w, kCookNavigationVersion );
    for ( usize v = 0u; v < pOut->positions.nCount; ++v ) {
        CookBytes_F32( &w, pOut->positions.pData[v].x );
        CookBytes_F32( &w, pOut->positions.pData[v].y );
        CookBytes_F32( &w, pOut->positions.pData[v].z );
    }
    for ( usize i = 0u; i < pOut->indices.nCount; ++i ) { CookBytes_U32( &w, pOut->indices.pData[i] ); }
    for ( usize t = 0u; t < pOut->triangleElement.nCount; ++t ) {
        CookBytes_U64( &w, pOut->triangleObject.pData[t].value );
        CookBytes_U64( &w, pOut->triangleElement.pData[t].value );
    }
    pOut->contentHash = CookBytes_Hash( &w );
    CookBytes_Shutdown( &w );
    pOut->revision = pSoup->revision;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
