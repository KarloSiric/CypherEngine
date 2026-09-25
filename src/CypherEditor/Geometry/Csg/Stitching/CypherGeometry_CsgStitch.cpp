//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgStitch.cpp
//  Purpose: Implements CSG stitching.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgStitch.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

geometry_status_t CsgStitch_TryCompact( const vector_t<csg_boundary_triangle_t> &tris, const vector_t<math::vec3d_t> &points, vector_t<u32> *pP2V,
                                        vector_t<u32> *pV2P, usize *pcNear ) noexcept
{
    if ( pP2V == nullptr || pV2P == nullptr || pP2V->pAllocator == nullptr || pV2P->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Resize( pP2V, points.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    Vector_Clear( pV2P );
    for ( usize i = 0u; i < points.nCount; ++i ) { pP2V->pData[i] = CY_U32_MAX; }
    for ( usize t = 0u; t < tris.nCount; ++t ) {
        for ( u32 k = 0u; k < 3u; ++k ) {
            const u32 p = tris.pData[t].v[k];
            if ( pP2V->pData[p] != CY_U32_MAX ) { continue; }
            pP2V->pData[p] = static_cast<u32>( pV2P->nCount );
            if ( !Vector_PushBack( pV2P, p ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
    }
    if ( pcNear == nullptr ) { return geometry_status_t::OK; }
    *pcNear = 0u;
    const usize cV = pV2P->nCount;
    if ( cV < 2u ) { return geometry_status_t::OK; }
    math::vec3d_t lo = points.pData[pV2P->pData[0]], hi = lo;
    for ( usize v = 1u; v < cV; ++v ) {
        lo = math::Vec3d_Min( lo, points.pData[pV2P->pData[v]] );
        hi = math::Vec3d_Max( hi, points.pData[pV2P->pData[v]] );
    }
    const f64 eps = kCsgNearDuplicateRelative * math::Vec3d_MaxAbsComponent( math::Vec3d_Subtract( hi, lo ) );
    vector_t<u32> order{};
    if ( !Vector_Init( &order, pV2P->pAllocator ) || !Vector_Resize( &order, cV ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize v = 0u; v < cV; ++v ) { order.pData[v] = pV2P->pData[v]; }
    std::sort( order.pData, order.pData + cV, [&]( u32 a, u32 b ) { return points.pData[a].x < points.pData[b].x; } );
    for ( usize i = 0u; i < cV; ++i ) {
        for ( usize j = i + 1u; j < cV && points.pData[order.pData[j]].x - points.pData[order.pData[i]].x <= eps; ++j ) {
            if ( math::Vec3d_DistanceSquared( points.pData[order.pData[i]], points.pData[order.pData[j]] ) <= eps * eps ) { ++*pcNear; }
        }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
