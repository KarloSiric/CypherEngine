//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgBoundary.cpp
//  Purpose: Implements CSG boundary extraction.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgBoundary.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

geometry_status_t CsgBoundary_TryExtract( const vector_t<csg_refined_triangle_t> &tris, const csg_cell_complex_t *pCells, const vector_t<math::vec3d_t> &points,
                                          csg_operator_t op, vector_t<csg_boundary_triangle_t> *pOut, usize *pcDroppedOut ) noexcept
{
    if ( pCells == nullptr || pOut == nullptr || pOut->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    Vector_Clear( pOut );
    if ( pcDroppedOut != nullptr ) { *pcDroppedOut = 0u; }
    for ( usize t = 0u; t < tris.nCount; ++t ) {
        const u32 cell = pCells->triangleCell.pData[t];
        const csg_decision_t d = CsgExpression_Decide( op, pCells->cellOperand.pData[cell], pCells->cellLabel.pData[cell] );
        if ( d == csg_decision_t::DROP ) { continue; }
        csg_boundary_triangle_t b{};
        const csg_refined_triangle_t &r = tris.pData[t];
        b.v[0] = r.v[0];
        b.v[1] = d == csg_decision_t::FLIP ? r.v[2] : r.v[1];
        b.v[2] = d == csg_decision_t::FLIP ? r.v[1] : r.v[2];
        b.iSource = r.iSource;
        b.bFlipped = d == csg_decision_t::FLIP;
        if ( !Vector_PushBack( pOut, b ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    if ( !CsgExpression_ResultClosed( op ) || pOut->nCount == 0u ) { return geometry_status_t::OK; }

    // Components over shared edges; drop the ones that enclose nothing.
    const allocator_t *pA = pOut->pAllocator;
    const usize cT = pOut->nCount;
    vector_t<u32> parent{};
    struct inc_t {
        u64 key;
        u32 tri;
    };
    vector_t<inc_t> inc{};
    if ( !Vector_Init( &parent, pA ) || !Vector_Init( &inc, pA ) || !Vector_Resize( &parent, cT ) ||
         !Vector_Resize( &inc, 3u * cT ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize t = 0u; t < cT; ++t ) {
        parent.pData[t] = static_cast<u32>( t );
        for ( u32 k = 0u; k < 3u; ++k ) {
            const u32 a = pOut->pData[t].v[k], b = pOut->pData[t].v[( k + 1u ) % 3u];
            inc.pData[3u * t + k] = inc_t{ a < b ? ( static_cast<u64>( a ) << 32u ) | b : ( static_cast<u64>( b ) << 32u ) | a, static_cast<u32>( t ) };
        }
    }
    std::sort( inc.pData, inc.pData + inc.nCount, []( const inc_t &x, const inc_t &y ) { return x.key < y.key; } );
    auto find = [&]( u32 v ) noexcept {
        while ( parent.pData[v] != v ) {
            parent.pData[v] = parent.pData[parent.pData[v]];
            v = parent.pData[v];
        }
        return v;
    };
    for ( usize i = 1u; i < inc.nCount; ++i ) {
        if ( inc.pData[i].key == inc.pData[i - 1u].key ) { parent.pData[find( inc.pData[i].tri )] = find( inc.pData[i - 1u].tri ); }
    }
    vector_t<f64> volume{}, extent{};
    vector_t<math::vec3d_t> origin{};
    if ( !Vector_Init( &volume, pA ) || !Vector_Init( &extent, pA ) || !Vector_Init( &origin, pA ) || !Vector_Resize( &volume, cT ) ||
         !Vector_Resize( &extent, cT ) || !Vector_Resize( &origin, cT ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize t = 0u; t < cT; ++t ) {
        volume.pData[t] = 0.0;
        extent.pData[t] = 0.0;
    }
    // Volumes relative to a point of the component keep the sum well
    // conditioned far from the world origin.
    for ( usize t = 0u; t < cT; ++t ) { origin.pData[find( static_cast<u32>( t ) )] = points.pData[pOut->pData[t].v[0]]; }
    for ( usize t = 0u; t < cT; ++t ) {
        const u32 r = find( static_cast<u32>( t ) );
        const math::vec3d_t o = origin.pData[r];
        const math::vec3d_t p0 = math::Vec3d_Subtract( points.pData[pOut->pData[t].v[0]], o );
        const math::vec3d_t p1 = math::Vec3d_Subtract( points.pData[pOut->pData[t].v[1]], o );
        const math::vec3d_t p2 = math::Vec3d_Subtract( points.pData[pOut->pData[t].v[2]], o );
        volume.pData[r] += math::Vec3d_Dot( p0, math::Vec3d_Cross( p1, p2 ) ) / 6.0;
        extent.pData[r] = std::fmax( extent.pData[r], std::fmax( math::Vec3d_MaxAbsComponent( p1 ), math::Vec3d_MaxAbsComponent( p2 ) ) );
    }
    usize w = 0u, cDropped = 0u;
    for ( usize t = 0u; t < cT; ++t ) {
        const u32 r = find( static_cast<u32>( t ) );
        const f64 e = extent.pData[r];
        if ( std::fabs( volume.pData[r] ) <= 1e-12 * e * e * e ) {
            ++cDropped;
            continue;
        }
        pOut->pData[w++] = pOut->pData[t];
    }
    pOut->nCount = w;
    if ( pcDroppedOut != nullptr ) { *pcDroppedOut = cDropped; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
