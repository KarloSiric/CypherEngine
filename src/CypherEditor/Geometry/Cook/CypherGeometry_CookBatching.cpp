//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookBatching.cpp
//  Purpose: Implements static batch proposals.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookBatching.h"
#include "CypherGeometry_CookBytes.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct entry_t {
    u64 material;
    i32 c[3];
    u64 object;
    u32 tri;
};

bool Less( const entry_t &a, const entry_t &b ) noexcept
{
    if ( a.material != b.material ) { return a.material < b.material; }
    for ( int k = 2; k >= 0; --k ) {
        if ( a.c[k] != b.c[k] ) { return a.c[k] < b.c[k]; }
    }
    if ( a.object != b.object ) { return a.object < b.object; }
    return a.tri < b.tri;
}

void Clear( cook_batching_t *p ) noexcept
{
    Vector_Clear( &p->batches );
    Vector_Clear( &p->ranges );
    Vector_Clear( &p->triangles );
    p->contentHash = CY_CONTENT_HASH_INVALID;
    p->revision = GEOMETRY_REVISION_INITIAL;
}

} // namespace

geometry_status_t CookBatching_Init( cook_batching_t *p, const allocator_t *pA ) noexcept
{
    if ( p == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &p->batches, pA ) || !Vector_Init( &p->ranges, pA ) || !Vector_Init( &p->triangles, pA ) ) {
        CookBatching_Shutdown( p );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    Clear( p );
    return geometry_status_t::OK;
}

void CookBatching_Shutdown( cook_batching_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    Vector_Shutdown( &p->batches );
    Vector_Shutdown( &p->ranges );
    Vector_Shutdown( &p->triangles );
}

geometry_status_t CookBatching_TryBuild( const cook_surface_soup_t *pSoup, const cook_batching_options_t &o, cook_batching_t *pOut ) noexcept
{
    if ( pSoup == nullptr || pOut == nullptr || pOut->batches.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !std::isfinite( o.chunkSize ) || !( o.chunkSize > 0.0 ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    Clear( pOut );
    const allocator_t *pA = pOut->batches.pAllocator;
    vector_t<entry_t> e{};
    if ( !Vector_Init( &e, pA ) || !Vector_Resize( &e, pSoup->triangles.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize t = 0u; t < pSoup->triangles.nCount; ++t ) {
        const cook_soup_triangle_t &tri = pSoup->triangles.pData[t];
        const math::vec3d_t c = math::Vec3d_Scale(
            math::Vec3d_Add( math::Vec3d_Add( pSoup->positions.pData[tri.v[0]], pSoup->positions.pData[tri.v[1]] ), pSoup->positions.pData[tri.v[2]] ),
            1.0 / 3.0 );
        entry_t &x = e.pData[t];
        x.material = tri.material.value;
        const f64 cc[3] = { c.x, c.y, c.z };
        for ( int k = 0; k < 3; ++k ) {
            const f64 cell = std::floor( cc[k] / o.chunkSize );
            if ( !std::isfinite( cell ) || cell < -2147483648.0 || cell > 2147483647.0 ) {
                Clear( pOut );
                return geometry_status_t::LIMIT_EXCEEDED;
            }
            x.c[k] = static_cast<i32>( cell );
        }
        x.object = pSoup->objects.pData[tri.iObject].objectId.value;
        x.tri = static_cast<u32>( t );
    }
    std::sort( e.pData, e.pData + e.nCount, Less );
    if ( !Vector_Resize( &pOut->triangles, e.nCount ) ) {
        Clear( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    bool bOk = true;
    for ( usize i = 0u; bOk && i < e.nCount; ++i ) {
        const entry_t &x = e.pData[i];
        pOut->triangles.pData[i] = x.tri;
        const bool bNewBatch = i == 0u || x.material != e.pData[i - 1u].material || x.c[0] != e.pData[i - 1u].c[0] || x.c[1] != e.pData[i - 1u].c[1] ||
                               x.c[2] != e.pData[i - 1u].c[2];
        if ( bNewBatch ) {
            cook_batch_t b{};
            b.material.value = x.material;
            b.chunk[0] = x.c[0];
            b.chunk[1] = x.c[1];
            b.chunk[2] = x.c[2];
            b.iFirstRange = static_cast<u32>( pOut->ranges.nCount );
            b.iFirstTriangle = static_cast<u32>( i );
            for ( int k = 0; k < 3; ++k ) {
                b.lo[k] = 1e300;
                b.hi[k] = -1e300;
            }
            bOk = Vector_PushBack( &pOut->batches, b );
        }
        if ( bOk && ( bNewBatch || x.object != e.pData[i - 1u].object ) ) {
            cook_batch_range_t r{};
            r.objectId.value = x.object;
            r.iFirstTriangle = static_cast<u32>( i );
            bOk = Vector_PushBack( &pOut->ranges, r );
            if ( bOk ) { ++pOut->batches.pData[pOut->batches.nCount - 1u].cRanges; }
        }
        if ( bOk ) {
            cook_batch_t &b = pOut->batches.pData[pOut->batches.nCount - 1u];
            ++b.cTriangles;
            ++pOut->ranges.pData[pOut->ranges.nCount - 1u].cTriangles;
            const cook_soup_triangle_t &tri = pSoup->triangles.pData[x.tri];
            for ( u32 k = 0u; k < 3u; ++k ) {
                const math::vec3d_t p = pSoup->positions.pData[tri.v[k]];
                const f64 pp[3] = { p.x, p.y, p.z };
                for ( int a = 0; a < 3; ++a ) {
                    b.lo[a] = std::fmin( b.lo[a], pp[a] );
                    b.hi[a] = std::fmax( b.hi[a], pp[a] );
                }
            }
        }
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
    CookBytes_U32( &w, kCookBatchingVersion );
    CookBytes_F64( &w, o.chunkSize );
    for ( usize b = 0u; b < pOut->batches.nCount; ++b ) {
        const cook_batch_t &x = pOut->batches.pData[b];
        CookBytes_U64( &w, x.material.value );
        for ( int k = 0; k < 3; ++k ) { CookBytes_U32( &w, static_cast<u32>( x.chunk[k] ) ); }
        CookBytes_U32( &w, x.cRanges );
        CookBytes_U32( &w, x.cTriangles );
    }
    for ( usize r = 0u; r < pOut->ranges.nCount; ++r ) {
        CookBytes_U64( &w, pOut->ranges.pData[r].objectId.value );
        CookBytes_U32( &w, pOut->ranges.pData[r].cTriangles );
    }
    for ( usize t = 0u; t < pOut->triangles.nCount; ++t ) { CookBytes_U64( &w, pSoup->triangles.pData[pOut->triangles.pData[t]].elementId.value ); }
    pOut->contentHash = CookBytes_Hash( &w );
    CookBytes_Shutdown( &w );
    pOut->revision = pSoup->revision;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
