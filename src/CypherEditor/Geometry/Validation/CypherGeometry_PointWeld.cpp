//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PointWeld.cpp
//  Purpose: Implements deterministic sort-and-sweep point welding.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PointWeld.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename type_t>
struct scratch_t {
    vector_t<type_t> v{};
    ~scratch_t() { Vector_Shutdown( &v ); }
};

u32 Root( u32 *parent, u32 x ) noexcept
{
    while ( parent[x] != x ) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

bool LexLess( math::vec3d_t a, math::vec3d_t b ) noexcept
{
    if ( a.x != b.x ) { return a.x < b.x; }
    if ( a.y != b.y ) { return a.y < b.y; }
    return a.z < b.z;
}

} // namespace

point_weld_result_t PointWeld_BuildRemap(
    span_t<const math::vec3d_t> points,
    f64 fDistance,
    const allocator_t *pScratchAllocator,
    vector_t<u32> *pRemapOut,
    vector_t<u32> *pRepresentativeOut ) noexcept
{
    point_weld_result_t r{};
    if ( ( points.pData == nullptr && points.nCount != 0u ) || !( fDistance >= 0.0 ) ||
         !Allocator_IsValid( pScratchAllocator ) || pRemapOut == nullptr ||
         pRepresentativeOut == nullptr || pRemapOut->pAllocator == nullptr ||
         pRepresentativeOut->pAllocator == nullptr ) {
        return r;
    }
    const usize n = points.nCount;
    scratch_t<u32> order, parent;
    if ( !Vector_Init( &order.v, pScratchAllocator, n ) ||
         !Vector_Init( &parent.v, pScratchAllocator, n ) ||
         !Vector_Resize( &order.v, n ) || !Vector_Resize( &parent.v, n ) ||
         !Vector_Resize( pRemapOut, n ) ) {
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    const math::vec3d_t *p = points.pData;
    for ( usize i = 0u; i < n; ++i ) {
        order.v.pData[i] = static_cast<u32>( i );
        parent.v.pData[i] = static_cast<u32>( i );
    }
    std::sort( order.v.pData, order.v.pData + n, [p]( u32 a, u32 b ) {
        const bool fa = math::Vec3d_IsFinite( p[a] ), fb = math::Vec3d_IsFinite( p[b] );
        if ( fa != fb ) { return fa; } // non-finite points sort last
        if ( fa && ( LexLess( p[a], p[b] ) || LexLess( p[b], p[a] ) ) ) {
            return LexLess( p[a], p[b] );
        }
        return a < b;
    } );

    for ( usize oi = 0u; oi < n; ++oi ) {
        const math::vec3d_t a = p[order.v.pData[oi]];
        if ( !math::Vec3d_IsFinite( a ) ) { break; }
        for ( usize oj = oi + 1u; oj < n; ++oj ) {
            const math::vec3d_t b = p[order.v.pData[oj]];
            if ( !math::Vec3d_IsFinite( b ) || b.x - a.x > fDistance ) { break; }
            if ( std::fabs( b.y - a.y ) > fDistance || std::fabs( b.z - a.z ) > fDistance ) {
                continue;
            }
            const u32 ra = Root( parent.v.pData, order.v.pData[oi] );
            const u32 rb = Root( parent.v.pData, order.v.pData[oj] );
            if ( ra != rb ) {
                // Root = the member earlier in sorted order, i.e. the
                // lexicographically smallest point of the cluster.
                parent.v.pData[rb] = ra;
            }
        }
    }
    // Normalize roots so each root is the cluster's sorted-first member.
    // With the union rule above (later joins earlier) a root may still be a
    // later element when two existing clusters merge; resolve by scanning
    // sorted order and adopting the first-seen member as representative.
    scratch_t<u32> repOfRoot;
    if ( !Vector_Init( &repOfRoot.v, pScratchAllocator, n ) || !Vector_Resize( &repOfRoot.v, n ) ) {
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    for ( usize i = 0u; i < n; ++i ) { repOfRoot.v.pData[i] = CY_INVALID_INDEX; }
    for ( usize oi = 0u; oi < n; ++oi ) {
        const u32 s = order.v.pData[oi];
        const u32 root = Root( parent.v.pData, s );
        if ( repOfRoot.v.pData[root] == CY_INVALID_INDEX ) { repOfRoot.v.pData[root] = s; }
    }

    // Dense cluster ids in order of first appearance in the input.
    scratch_t<u32> clusterOfRoot;
    if ( !Vector_Init( &clusterOfRoot.v, pScratchAllocator, n ) ||
         !Vector_Resize( &clusterOfRoot.v, n ) ) {
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    for ( usize i = 0u; i < n; ++i ) { clusterOfRoot.v.pData[i] = CY_INVALID_INDEX; }
    Vector_Clear( pRepresentativeOut );
    f64 maxDisp = 0.0;
    for ( usize i = 0u; i < n; ++i ) {
        const u32 root = Root( parent.v.pData, static_cast<u32>( i ) );
        if ( clusterOfRoot.v.pData[root] == CY_INVALID_INDEX ) {
            clusterOfRoot.v.pData[root] = static_cast<u32>( pRepresentativeOut->nCount );
            if ( !Vector_PushBack( pRepresentativeOut, repOfRoot.v.pData[root] ) ) {
                r.status = geometry_status_t::ALLOCATION_FAILED;
                return r;
            }
        }
        pRemapOut->pData[i] = clusterOfRoot.v.pData[root];
        const math::vec3d_t rep = p[repOfRoot.v.pData[root]];
        if ( math::Vec3d_IsFinite( p[i] ) ) {
            const f64 d = std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract( p[i], rep ) ) );
            if ( d > maxDisp ) { maxDisp = d; }
        }
    }
    r.cUnique = static_cast<u32>( pRepresentativeOut->nCount );
    r.cMerged = static_cast<u32>( n ) - r.cUnique;
    r.fMaxDisplacement = maxDisp;
    r.status = geometry_status_t::OK;
    return r;
}

} // namespace cypher::editor::geometry
