//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgBroadPhase.cpp
//  Purpose: Implements CSG candidate generation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgBroadPhase.h"

#include <algorithm>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct box_t {
    math::vec3d_t lo{}, hi{};
    u32 index{ 0u };
};

bool FillBoxes( const csg_operand_t *pOp, vector_t<box_t> *pBoxes ) noexcept
{
    if ( !Vector_Resize( pBoxes, pOp->triangles.nCount ) ) { return false; }
    for ( usize t = 0u; t < pOp->triangles.nCount; ++t ) {
        const csg_source_triangle_t &tri = pOp->triangles.pData[t];
        box_t &b = pBoxes->pData[t];
        b.lo = b.hi = pOp->positions.pData[tri.v[0]];
        for ( u32 i = 1u; i < 3u; ++i ) {
            b.lo = math::Vec3d_Min( b.lo, pOp->positions.pData[tri.v[i]] );
            b.hi = math::Vec3d_Max( b.hi, pOp->positions.pData[tri.v[i]] );
        }
        b.index = static_cast<u32>( t );
    }
    std::sort( pBoxes->pData, pBoxes->pData + pBoxes->nCount, []( const box_t &x, const box_t &y ) {
        return x.lo.x != y.lo.x ? x.lo.x < y.lo.x : x.index < y.index;
    } );
    return true;
}

} // namespace

geometry_status_t CsgBroadPhase_TryCollect( const csg_operand_t *pA, const csg_operand_t *pB, usize cPairsMax, vector_t<csg_pair_t> *pPairsOut ) noexcept
{
    if ( pA == nullptr || pB == nullptr || pPairsOut == nullptr || pPairsOut->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    Vector_Clear( pPairsOut );
    const allocator_t *pAlloc = pPairsOut->pAllocator;
    vector_t<box_t> boxesA{}, boxesB{};
    if ( !Vector_Init( &boxesA, pAlloc ) || !Vector_Init( &boxesB, pAlloc ) || !FillBoxes( pA, &boxesA ) || !FillBoxes( pB, &boxesB ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // Sweep over x: walk both box lists in lo.x order, keeping the boxes of
    // each operand that still reach the sweep position; each new box is
    // tested only against the other operand's active boxes.
    vector_t<u32> activeA{}, activeB{};
    if ( !Vector_Init( &activeA, pAlloc ) || !Vector_Init( &activeB, pAlloc ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    auto prune = []( vector_t<u32> *pActive, const vector_t<box_t> &boxes, f64 x ) noexcept {
        for ( usize k = 0u; k < pActive->nCount; ) {
            if ( boxes.pData[pActive->pData[k]].hi.x < x ) {
                pActive->pData[k] = pActive->pData[pActive->nCount - 1u];
                Vector_PopBack( pActive );
            } else {
                ++k;
            }
        }
    };
    usize ia = 0u, ib = 0u;
    while ( ia < boxesA.nCount || ib < boxesB.nCount ) {
        const bool bTakeA = ib >= boxesB.nCount || ( ia < boxesA.nCount && boxesA.pData[ia].lo.x <= boxesB.pData[ib].lo.x );
        const box_t &box = bTakeA ? boxesA.pData[ia] : boxesB.pData[ib];
        prune( &activeA, boxesA, box.lo.x );
        prune( &activeB, boxesB, box.lo.x );
        const vector_t<u32> &others = bTakeA ? activeB : activeA;
        const vector_t<box_t> &otherBoxes = bTakeA ? boxesB : boxesA;
        for ( usize k = 0u; k < others.nCount; ++k ) {
            const box_t &o = otherBoxes.pData[others.pData[k]];
            if ( o.hi.y < box.lo.y || o.lo.y > box.hi.y || o.hi.z < box.lo.z || o.lo.z > box.hi.z ) { continue; }
            if ( pPairsOut->nCount >= cPairsMax ) {
                Vector_Clear( pPairsOut );
                return geometry_status_t::LIMIT_EXCEEDED;
            }
            const csg_pair_t pair = bTakeA ? csg_pair_t{ box.index, o.index } : csg_pair_t{ o.index, box.index };
            if ( !Vector_PushBack( pPairsOut, pair ) ) {
                Vector_Clear( pPairsOut );
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
        if ( !Vector_PushBack( bTakeA ? &activeA : &activeB, static_cast<u32>( bTakeA ? ia : ib ) ) ) {
            Vector_Clear( pPairsOut );
            return geometry_status_t::ALLOCATION_FAILED;
        }
        if ( bTakeA ) {
            ++ia;
        } else {
            ++ib;
        }
    }
    std::sort( pPairsOut->pData, pPairsOut->pData + pPairsOut->nCount,
               []( const csg_pair_t &x, const csg_pair_t &y ) { return x.iA != y.iA ? x.iA < y.iA : x.iB < y.iB; } );
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
