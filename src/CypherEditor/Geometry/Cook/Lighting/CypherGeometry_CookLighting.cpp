//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookLighting.cpp
//  Purpose: Implements lightmap charts and atlas layout.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookLighting.h"
#include "CypherGeometry_CookBytes.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

void Clear( cook_lighting_t *p ) noexcept
{
    Vector_Clear( &p->charts );
    Vector_Clear( &p->chartTriangles );
    Vector_Clear( &p->cornerUvs );
    p->cPages = 0u;
    p->contentHash = CY_CONTENT_HASH_INVALID;
    p->revision = GEOMETRY_REVISION_INITIAL;
}

struct inc_t {
    u64 key;
    u32 tri;
};

} // namespace

geometry_status_t CookLighting_Init( cook_lighting_t *p, const allocator_t *pA ) noexcept
{
    if ( p == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &p->charts, pA ) || !Vector_Init( &p->chartTriangles, pA ) || !Vector_Init( &p->cornerUvs, pA ) ) {
        CookLighting_Shutdown( p );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    Clear( p );
    return geometry_status_t::OK;
}

void CookLighting_Shutdown( cook_lighting_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    Vector_Shutdown( &p->charts );
    Vector_Shutdown( &p->chartTriangles );
    Vector_Shutdown( &p->cornerUvs );
}

geometry_status_t CookLighting_TryBuild( const cook_surface_soup_t *pSoup, const cook_lighting_options_t &o, cook_lighting_t *pOut ) noexcept
{
    if ( pSoup == nullptr || pOut == nullptr || pOut->charts.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !std::isfinite( o.texelsPerUnit ) || !( o.texelsPerUnit > 0.0 ) || o.pageSize < 16u || o.pageSize > kCookLightingPageMax ||
         4u * o.paddingTexels + 2u > o.pageSize || !std::isfinite( o.chartAngleRadians ) || !( o.chartAngleRadians > 0.0 ) ||
         !( o.chartAngleRadians < 0.5 * math::CY_PI_D ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    Clear( pOut );
    const allocator_t *pA = pOut->charts.pAllocator;
    const usize cT = pSoup->triangles.nCount;
    // Edge adjacency (soup vertices are per object, so edges never join objects).
    vector_t<inc_t> inc{};
    vector_t<u32> chartOf{}, queue{}, order{};
    if ( !Vector_Init( &inc, pA ) || !Vector_Init( &chartOf, pA ) || !Vector_Init( &queue, pA ) || !Vector_Init( &order, pA ) ||
         !Vector_Resize( &inc, 3u * cT ) || !Vector_Resize( &chartOf, cT ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize t = 0u; t < cT; ++t ) {
        chartOf.pData[t] = CY_U32_MAX;
        for ( u32 k = 0u; k < 3u; ++k ) {
            const u32 a = pSoup->triangles.pData[t].v[k], b = pSoup->triangles.pData[t].v[( k + 1u ) % 3u];
            inc.pData[3u * t + k] = inc_t{ a < b ? ( static_cast<u64>( a ) << 32u ) | b : ( static_cast<u64>( b ) << 32u ) | a, static_cast<u32>( t ) };
        }
    }
    std::sort( inc.pData, inc.pData + inc.nCount, []( const inc_t &x, const inc_t &y ) { return x.key != y.key ? x.key < y.key : x.tri < y.tri; } );
    auto neighbours = [&]( u32 t, auto &&visit ) noexcept {
        for ( u32 k = 0u; k < 3u; ++k ) {
            const u32 a = pSoup->triangles.pData[t].v[k], b = pSoup->triangles.pData[t].v[( k + 1u ) % 3u];
            const u64 key = a < b ? ( static_cast<u64>( a ) << 32u ) | b : ( static_cast<u64>( b ) << 32u ) | a;
            const inc_t *p = std::lower_bound( inc.pData, inc.pData + inc.nCount, inc_t{ key, 0u },
                                               []( const inc_t &x, const inc_t &y ) { return x.key != y.key ? x.key < y.key : x.tri < y.tri; } );
            for ( ; p != inc.pData + inc.nCount && p->key == key; ++p ) {
                if ( p->tri != t ) { visit( p->tri ); }
            }
        }
    };
    // Grow charts; degenerate triangles (no normal) join any neighbour's.
    const f64 cosLimit = std::cos( o.chartAngleRadians );
    bool bOk = true;
    for ( usize seed = 0u; bOk && seed < cT; ++seed ) {
        if ( chartOf.pData[seed] != CY_U32_MAX ) { continue; }
        const cook_soup_triangle_t &s = pSoup->triangles.pData[seed];
        cook_light_chart_t chart{};
        chart.objectId = pSoup->objects.pData[s.iObject].objectId;
        chart.normal = math::Vec3d_LengthSquared( s.normal ) > 0.0 ? s.normal : math::Vec3d_Make( 0, 0, 1 );
        Vec3d_BuildOrthonormalBasis( chart.normal, &chart.uAxis, &chart.vAxis );
        const u32 iChart = static_cast<u32>( pOut->charts.nCount );
        chart.iFirstTriangle = static_cast<u32>( pOut->chartTriangles.nCount );
        Vector_Clear( &queue );
        bOk = Vector_PushBack( &queue, static_cast<u32>( seed ) );
        chartOf.pData[seed] = iChart;
        for ( usize q = 0u; bOk && q < queue.nCount; ++q ) {
            const u32 t = queue.pData[q];
            bOk = Vector_PushBack( &pOut->chartTriangles, t );
            neighbours( t, [&]( u32 nb ) noexcept {
                if ( !bOk || chartOf.pData[nb] != CY_U32_MAX ) { return; }
                const math::vec3d_t n = pSoup->triangles.pData[nb].normal;
                const bool bDegenerate = math::Vec3d_LengthSquared( n ) == 0.0;
                if ( !bDegenerate && math::Vec3d_Dot( n, chart.normal ) < cosLimit ) { return; }
                chartOf.pData[nb] = iChart;
                bOk = Vector_PushBack( &queue, nb );
            } );
        }
        chart.cTriangles = static_cast<u32>( pOut->chartTriangles.nCount ) - chart.iFirstTriangle;
        bOk = bOk && Vector_PushBack( &pOut->charts, chart );
    }
    if ( !bOk ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // Chart extents in texels (the projection frame's 2D bounds).
    const usize cCharts = pOut->charts.nCount;
    vector_t<f64> minU{}, minV{};
    if ( !Vector_Init( &minU, pA ) || !Vector_Init( &minV, pA ) || !Vector_Resize( &minU, cCharts ) || !Vector_Resize( &minV, cCharts ) ||
         !Vector_Resize( &order, cCharts ) ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const u32 pad = o.paddingTexels, inner = o.pageSize - 2u * pad;
    for ( usize c = 0u; c < cCharts; ++c ) {
        cook_light_chart_t &ch = pOut->charts.pData[c];
        f64 u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
        for ( u32 i = 0u; i < ch.cTriangles; ++i ) {
            const cook_soup_triangle_t &t = pSoup->triangles.pData[pOut->chartTriangles.pData[ch.iFirstTriangle + i]];
            for ( u32 k = 0u; k < 3u; ++k ) {
                const math::vec3d_t p = pSoup->positions.pData[t.v[k]];
                const f64 u = math::Vec3d_Dot( p, ch.uAxis ), v = math::Vec3d_Dot( p, ch.vAxis );
                u0 = std::fmin( u0, u );
                u1 = std::fmax( u1, u );
                v0 = std::fmin( v0, v );
                v1 = std::fmax( v1, v );
            }
        }
        minU.pData[c] = u0;
        minV.pData[c] = v0;
        const f64 eu = ( u1 - u0 ) * o.texelsPerUnit, ev = ( v1 - v0 ) * o.texelsPerUnit;
        const f64 fit = std::fmin( eu > 0.0 ? inner / eu : 1.0, ev > 0.0 ? inner / ev : 1.0 );
        ch.scale = fit < 1.0 ? fit : 1.0;
        ch.w = std::min( inner, static_cast<u32>( std::ceil( eu * ch.scale ) ) + 1u ) + 2u * pad;
        ch.h = std::min( inner, static_cast<u32>( std::ceil( ev * ch.scale ) ) + 1u ) + 2u * pad;
        order.pData[c] = static_cast<u32>( c );
    }
    // Shelf packing, tallest first.
    std::sort( order.pData, order.pData + cCharts, [&]( u32 a, u32 b ) {
        const cook_light_chart_t &x = pOut->charts.pData[a], &y = pOut->charts.pData[b];
        return x.h != y.h ? x.h > y.h : x.w != y.w ? x.w > y.w : a < b;
    } );
    u32 page = 0u, cx = 0u, cy = 0u, shelfH = 0u;
    for ( usize i = 0u; i < cCharts; ++i ) {
        cook_light_chart_t &ch = pOut->charts.pData[order.pData[i]];
        if ( cx + ch.w > o.pageSize ) { // next shelf
            cy += shelfH;
            cx = 0u;
            shelfH = 0u;
        }
        if ( cy + ch.h > o.pageSize ) { // next page
            ++page;
            cx = cy = shelfH = 0u;
        }
        ch.page = page;
        ch.x = cx;
        ch.y = cy;
        cx += ch.w;
        shelfH = std::max( shelfH, ch.h );
    }
    pOut->cPages = cCharts > 0u ? page + 1u : 0u;
    // Corner UVs, page-normalised.
    if ( !Vector_Resize( &pOut->cornerUvs, 6u * pOut->chartTriangles.nCount ) ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const f64 inv = 1.0 / o.pageSize;
    for ( usize c = 0u; c < cCharts; ++c ) {
        const cook_light_chart_t &ch = pOut->charts.pData[c];
        const f64 k = o.texelsPerUnit * ch.scale;
        for ( u32 i = 0u; i < ch.cTriangles; ++i ) {
            const usize slot = ch.iFirstTriangle + i;
            const cook_soup_triangle_t &t = pSoup->triangles.pData[pOut->chartTriangles.pData[slot]];
            for ( u32 v = 0u; v < 3u; ++v ) {
                const math::vec3d_t p = pSoup->positions.pData[t.v[v]];
                const f64 u = ( math::Vec3d_Dot( p, ch.uAxis ) - minU.pData[c] ) * k + ch.x + pad + 0.5;
                const f64 w = ( math::Vec3d_Dot( p, ch.vAxis ) - minV.pData[c] ) * k + ch.y + pad + 0.5;
                pOut->cornerUvs.pData[6u * slot + 2u * v] = static_cast<f32>( u * inv );
                pOut->cornerUvs.pData[6u * slot + 2u * v + 1u] = static_cast<f32>( w * inv );
            }
        }
    }
    cook_byte_writer_t wr{};
    if ( !CookBytes_Init( &wr, pA ) ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    CookBytes_U32( &wr, kCookLightingVersion );
    CookBytes_U32( &wr, pOut->cPages );
    for ( usize c = 0u; c < cCharts; ++c ) {
        const cook_light_chart_t &ch = pOut->charts.pData[c];
        CookBytes_U64( &wr, ch.objectId.value );
        CookBytes_U32( &wr, ch.page );
        CookBytes_U32( &wr, ch.x );
        CookBytes_U32( &wr, ch.y );
        CookBytes_U32( &wr, ch.w );
        CookBytes_U32( &wr, ch.h );
        CookBytes_U32( &wr, ch.cTriangles );
    }
    for ( usize i = 0u; i < pOut->chartTriangles.nCount; ++i ) { CookBytes_U32( &wr, pOut->chartTriangles.pData[i] ); }
    for ( usize i = 0u; i < pOut->cornerUvs.nCount; ++i ) { CookBytes_F32( &wr, pOut->cornerUvs.pData[i] ); }
    pOut->contentHash = CookBytes_Hash( &wr );
    CookBytes_Shutdown( &wr );
    pOut->revision = pSoup->revision;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
