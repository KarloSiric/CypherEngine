//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushVertexClump.cpp
//  Purpose: Implements the all-or-nothing multi-brush vertex move.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushVertexClump.h"

#include "CypherGeometry_Document.h"

#include <cmath>
#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;

geometry_status_t BrushVertexOps_TryMoveVertexClump(
    span_t<brush_solid_t *const> brushes, math::vec3d_t position, math::vec3d_t newPosition, const allocator_t *pAllocator,
    const geometry_policy_t &policy, geometry_source_id_allocator_t *pIdAllocator, u32 *pMovedOut ) noexcept
{
    if ( pMovedOut != nullptr ) { *pMovedOut = 0u; }
    if ( pAllocator == nullptr || pIdAllocator == nullptr || !GeometryPolicy_IsValid( policy ) ||
         ( brushes.nCount > 0u && brushes.pData == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( position ) || !math::Vec3d_IsFinite( newPosition ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    const usize n = brushes.nCount;
    brush_solid_t *pTemp = n > 0u ? Allocator_AllocateArrayStorage<brush_solid_t>( pAllocator, n ) : nullptr;
    if ( n > 0u && pTemp == nullptr ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < n; ++i ) { ::new ( static_cast<void *>( &pTemp[i] ) ) brush_solid_t{}; }
    const geometry_source_id_allocator_t snapshot = *pIdAllocator;
    brush_boundary_t bd{};
    geometry_status_t st = BrushBoundary_Init( &bd, pAllocator );
    const f64 tol2 = policy.numerical.fWeldDistance * policy.numerical.fWeldDistance;
    u32 cMoved = 0u;
    for ( usize i = 0u; st == geometry_status_t::OK && i < n; ++i ) {
        brush_solid_t *pB = brushes.pData[i];
        if ( pB == nullptr ) { continue; }
        st = BrushBoundary_TryReconstruct( &bd, pB, policy );
        if ( st != geometry_status_t::OK ) { break; }
        usize iBest = CY_USIZE_MAX;
        f64 best = tol2;
        for ( usize k = 0u; k < bd.vertices.nCount; ++k ) {
            const f64 d2 = math::Vec3d_LengthSquared( math::Vec3d_Subtract( bd.vertices.pData[k], position ) );
            if ( d2 <= best ) {
                best = d2;
                iBest = k;
            }
        }
        if ( iBest == CY_USIZE_MAX ) { continue; }
        // Move a private copy; the original stays until every brush succeeded.
        st = BrushSolid_DeepCopy( &pTemp[i], pB, pAllocator, policy.limits );
        if ( st == geometry_status_t::OK ) {
            st = BrushVertexOps_TryMoveVertex( &pTemp[i], &bd, pAllocator, policy, pIdAllocator, iBest, newPosition ).status;
        }
        if ( st == geometry_status_t::OK ) { ++cMoved; }
    }
    if ( st == geometry_status_t::OK && cMoved == 0u ) { st = geometry_status_t::INVALID_ARGUMENT; }
    if ( st == geometry_status_t::OK ) {
        for ( usize i = 0u; i < n; ++i ) {
            if ( pTemp[i].sides.pAllocator == nullptr ) { continue; } // not affected
            brush_solid_t *pB = brushes.pData[i];
            BrushSolid_Shutdown( pB );
            pB->sourceId = pTemp[i].sourceId;
            Vector_Move( &pB->sides, &pTemp[i].sides );
            pTemp[i].sourceId = GEOMETRY_SOURCE_ID_INVALID;
        }
        if ( pMovedOut != nullptr ) { *pMovedOut = cMoved; }
    } else {
        *pIdAllocator = snapshot;
    }
    for ( usize i = 0u; i < n; ++i ) {
        BrushSolid_Shutdown( &pTemp[i] );
        pTemp[i].~brush_solid_t();
    }
    if ( pTemp != nullptr ) { Allocator_FreeArrayStorage( pAllocator, pTemp, n ); }
    BrushBoundary_Shutdown( &bd );
    return st;
}

} // namespace cypher::editor::geometry
