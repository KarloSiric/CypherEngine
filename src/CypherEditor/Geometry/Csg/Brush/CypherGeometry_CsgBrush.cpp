//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgBrush.cpp
//  Purpose: Implements Booleans over sets of convex brushes.
//  Details: Working pieces are bare solids whose side IDs are working IDs:
//           input sides keep their own, sides BrushCSG creates get scratch
//           IDs from a range far above any document ID. One table maps every
//           working side ID to its origin (source brush, source side,
//           operand, surface record), filled from the inputs and from each
//           subtraction's side provenance, so the final pieces can be given
//           records and fresh document IDs in one pass at the end.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgBrush.h"
#include "CypherGeometry_BrushCSG.h"

#include "CypherCommon_HashMap.h"

#include <new>
#include <utility>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct origin_t {
    geometry_source_id_t brushId{};
    geometry_source_id_t sideId{};
    u32 operand{ kCsgOperandA };
    geometry_brush_side_attributes_t record{};
};

struct ctx_t {
    const allocator_t *pA{ nullptr };
    const geometry_policy_t *pPolicy{ nullptr };
    geometry_source_id_allocator_t work{};
    hash_map_t<u64, origin_t> origins{};
    geometry_status_t st{ geometry_status_t::OK };
};

brush_solid_t *NewSolid( ctx_t *c ) noexcept
{
    void *p = Allocator_AllocateZeroed( c->pA, sizeof( brush_solid_t ), alignof( brush_solid_t ) );
    return p != nullptr ? new ( p ) brush_solid_t{} : nullptr;
}

void FreeSolid( ctx_t *c, brush_solid_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    BrushSolid_Shutdown( p );
    p->~brush_solid_t();
    Allocator_Free( c->pA, p, sizeof( brush_solid_t ), alignof( brush_solid_t ) );
}

void FreeAll( ctx_t *c, vector_t<brush_solid_t *> *pList ) noexcept
{
    for ( usize i = 0u; i < pList->nCount; ++i ) { FreeSolid( c, pList->pData[i] ); }
    Vector_Clear( pList );
}

bool Push( ctx_t *c, vector_t<brush_solid_t *> *pList, brush_solid_t *p ) noexcept
{
    if ( pList->nCount >= kCsgBrushPiecesMax ) {
        c->st = geometry_status_t::LIMIT_EXCEEDED;
        FreeSolid( c, p );
        return false;
    }
    if ( !Vector_PushBack( pList, p ) ) {
        c->st = geometry_status_t::ALLOCATION_FAILED;
        FreeSolid( c, p );
        return false;
    }
    return true;
}

brush_solid_t *CopySolid( ctx_t *c, const brush_solid_t &s ) noexcept
{
    brush_solid_t *p = NewSolid( c );
    if ( p == nullptr ) {
        c->st = geometry_status_t::ALLOCATION_FAILED;
        return nullptr;
    }
    geometry_status_t st = BrushSolid_Init( p, c->pA, s.sourceId );
    if ( st == geometry_status_t::OK ) { st = BrushSolid_TryReserve( p, c->pPolicy->limits, s.sides.nCount ); }
    for ( usize k = 0u; st == geometry_status_t::OK && k < s.sides.nCount; ++k ) { st = BrushSolid_TryAddSide( p, c->pPolicy->limits, s.sides.pData[k], nullptr ); }
    if ( st != geometry_status_t::OK ) {
        c->st = st;
        FreeSolid( c, p );
        return nullptr;
    }
    return p;
}

bool RegisterInput( ctx_t *c, const brush_source_t &b, u32 operand ) noexcept
{
    for ( usize k = 0u; k < b.solid.sides.nCount; ++k ) {
        const brush_solid_side_t &side = b.solid.sides.pData[k];
        origin_t o{};
        o.brushId = b.solid.sourceId;
        o.sideId = side.sourceId;
        o.operand = operand;
        if ( BrushSideAttributeStore_TryGet( &b.attributes, side.iAttributeIndex, &o.record ) != geometry_status_t::OK ) {
            o.record = geometry_brush_side_attributes_t{};
        }
        if ( !HashMap_Insert( &c->origins, side.sourceId.value, o ).pValue ) {
            c->st = geometry_status_t::ALLOCATION_FAILED;
            return false;
        }
    }
    return true;
}

// pieces := union over pieces of (piece - cutter).
void Carve( ctx_t *c, vector_t<brush_solid_t *> *pPieces, const brush_solid_t &cutter ) noexcept
{
    vector_t<brush_solid_t *> next{};
    if ( !Vector_Init( &next, c->pA ) ) {
        c->st = geometry_status_t::ALLOCATION_FAILED;
        return;
    }
    for ( usize i = 0u; i < pPieces->nCount && c->st == geometry_status_t::OK; ++i ) {
        brush_csg_subtract_result_t r{};
        const geometry_status_t st = BrushCSG_TrySubtract( pPieces->pData[i], &cutter, c->pA, &c->work, *c->pPolicy, &r );
        if ( st == geometry_status_t::DEGENERATE ) { continue; } // the piece lies inside the cutter
        if ( st != geometry_status_t::OK ) {
            c->st = st;
            BrushCSGSubtractResult_Shutdown( &r );
            break;
        }
        // New sides inherit the origin of the side they come from.
        for ( usize k = 0u; k < r.sideProvenance.nCount && c->st == geometry_status_t::OK; ++k ) {
            const brush_csg_raw_side_provenance_t &pv = r.sideProvenance.pData[k];
            if ( HashMap_Find( &c->origins, pv.sideId.value ) != nullptr ) { continue; }
            const origin_t *pFrom = HashMap_Find( &c->origins, pv.sourceSideId.value );
            origin_t o = pFrom != nullptr ? *pFrom : origin_t{};
            if ( pFrom == nullptr ) {
                o.brushId = pv.sourceBrushId;
                o.operand = pv.operand == brush_csg_operand_t::MINUEND_A ? kCsgOperandA : kCsgOperandB;
            }
            if ( !HashMap_Insert( &c->origins, pv.sideId.value, o ).pValue ) { c->st = geometry_status_t::ALLOCATION_FAILED; }
        }
        for ( usize f = 0u; f < r.cFragments && c->st == geometry_status_t::OK; ++f ) {
            brush_solid_t *p = CopySolid( c, r.fragments[f] );
            if ( p != nullptr ) { (void)Push( c, &next, p ); }
        }
        BrushCSGSubtractResult_Shutdown( &r );
    }
    if ( c->st != geometry_status_t::OK ) {
        FreeAll( c, &next );
        return;
    }
    FreeAll( c, pPieces );
    std::swap( pPieces->pData, next.pData );
    std::swap( pPieces->nCount, next.nCount );
    std::swap( pPieces->nCapacity, next.nCapacity );
}

// Final pieces -> authored brushes with fresh IDs and origin records.
geometry_status_t Publish( ctx_t *c, const vector_t<brush_solid_t *> &pieces, geometry_source_id_allocator_t *pIds, geometry_fragment_t *pOut,
                           vector_t<csg_brush_side_provenance_t> *pProv ) noexcept
{
    const usize cOutBefore = GeometryFragment_ObjectCount( pOut );
    const usize cProvBefore = pProv != nullptr ? pProv->nCount : 0u;
    geometry_source_id_allocator_t ids = *pIds;
    geometry_status_t st = geometry_status_t::OK;
    for ( usize i = 0u; i < pieces.nCount && st == geometry_status_t::OK; ++i ) {
        brush_source_t src{};
        st = BrushSource_TryBuildDefault( pieces.pData[i], c->pA, *c->pPolicy, &src );
        if ( st != geometry_status_t::OK ) { break; }
        const geometry_source_id_result_t bid = GeometrySourceIdAllocator_Allocate( &ids );
        st = bid.status;
        if ( st == geometry_status_t::OK ) { src.solid.sourceId = bid.id; }
        for ( usize k = 0u; st == geometry_status_t::OK && k < src.solid.sides.nCount; ++k ) {
            brush_solid_side_t &side = src.solid.sides.pData[k];
            const origin_t *pO = HashMap_Find( &c->origins, side.sourceId.value );
            if ( pO != nullptr ) { st = BrushSideAttributeStore_TrySet( &src.attributes, c->pPolicy->numerical, side.iAttributeIndex, pO->record ); }
            if ( st != geometry_status_t::OK ) { break; }
            const geometry_source_id_result_t sid = GeometrySourceIdAllocator_Allocate( &ids );
            st = sid.status;
            if ( st != geometry_status_t::OK ) { break; }
            if ( pProv != nullptr ) {
                csg_brush_side_provenance_t pv{};
                pv.destinationBrushId = bid.id;
                pv.destinationSideId = sid.id;
                if ( pO != nullptr ) {
                    pv.sourceBrushId = pO->brushId;
                    pv.sourceSideId = pO->sideId;
                    pv.iOperand = pO->operand;
                }
                if ( !Vector_PushBack( pProv, pv ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
            }
            side.sourceId = sid.id;
        }
        if ( st == geometry_status_t::OK ) {
            st = GeometryFragment_TryAppendCopies( pOut, *c->pPolicy, span_t<const brush_source_t>{ &src, 1u }, {}, {}, {} );
        }
        BrushSource_Shutdown( &src );
    }
    if ( st != geometry_status_t::OK ) {
        // Take back what this call appended.
        while ( pOut->brushes.nCount > cOutBefore ) {
            brush_source_t *p = pOut->brushes.pData[pOut->brushes.nCount - 1u];
            BrushSource_Shutdown( p );
            p->~brush_source_t();
            Allocator_Free( pOut->pAllocator, p, sizeof( brush_source_t ), alignof( brush_source_t ) );
            Vector_PopBack( &pOut->brushes );
        }
        if ( pProv != nullptr ) { pProv->nCount = cProvBefore; }
        return st;
    }
    *pIds = ids;
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t CsgBrush_TryEvaluate( csg_operator_t op, span_t<const brush_source_t *const> brushesA, span_t<const brush_source_t *const> brushesB,
                                        const geometry_policy_t &policy, geometry_source_id_allocator_t *pIdAllocator, geometry_fragment_t *pOut,
                                        vector_t<csg_brush_side_provenance_t> *pProvenance ) noexcept
{
    if ( pIdAllocator == nullptr || !GeometryFragment_IsInitialized( pOut ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( ( brushesA.nCount > 0u && brushesA.pData == nullptr ) || ( brushesB.nCount > 0u && brushesB.pData == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( op != csg_operator_t::UNION && op != csg_operator_t::INTERSECTION && op != csg_operator_t::DIFFERENCE ) { return geometry_status_t::UNSUPPORTED; }
    if ( pProvenance != nullptr && pProvenance->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    ctx_t c{};
    c.pA = pOut->pAllocator;
    c.pPolicy = &policy;
    c.work.next.value = 1ull << 62; // scratch IDs, far above any document's
    if ( !HashMap_Init( &c.origins, c.pA ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < brushesA.nCount && c.st == geometry_status_t::OK; ++i ) {
        if ( brushesA.pData[i] == nullptr ) {
            c.st = geometry_status_t::INVALID_ARGUMENT;
            break;
        }
        (void)RegisterInput( &c, *brushesA.pData[i], kCsgOperandA );
    }
    for ( usize i = 0u; i < brushesB.nCount && c.st == geometry_status_t::OK; ++i ) {
        if ( brushesB.pData[i] == nullptr ) {
            c.st = geometry_status_t::INVALID_ARGUMENT;
            break;
        }
        (void)RegisterInput( &c, *brushesB.pData[i], kCsgOperandB );
    }
    vector_t<brush_solid_t *> result{}, pieces{};
    if ( c.st == geometry_status_t::OK && ( !Vector_Init( &result, c.pA ) || !Vector_Init( &pieces, c.pA ) ) ) { c.st = geometry_status_t::ALLOCATION_FAILED; }

    if ( c.st == geometry_status_t::OK && op == csg_operator_t::DIFFERENCE ) {
        for ( usize i = 0u; i < brushesA.nCount && c.st == geometry_status_t::OK; ++i ) {
            FreeAll( &c, &pieces );
            brush_solid_t *p = CopySolid( &c, brushesA.pData[i]->solid );
            if ( p == nullptr || !Push( &c, &pieces, p ) ) { break; }
            for ( usize j = 0u; j < brushesB.nCount && c.st == geometry_status_t::OK && pieces.nCount > 0u; ++j ) { Carve( &c, &pieces, brushesB.pData[j]->solid ); }
            for ( usize k = 0u; k < pieces.nCount && c.st == geometry_status_t::OK; ++k ) {
                if ( Push( &c, &result, pieces.pData[k] ) ) { pieces.pData[k] = nullptr; }
            }
            pieces.nCount = 0u;
        }
    } else if ( c.st == geometry_status_t::OK && op == csg_operator_t::UNION ) {
        for ( usize i = 0u; i < brushesA.nCount && c.st == geometry_status_t::OK; ++i ) {
            brush_solid_t *p = CopySolid( &c, brushesA.pData[i]->solid );
            if ( p != nullptr ) { (void)Push( &c, &result, p ); }
        }
        for ( usize j = 0u; j < brushesB.nCount && c.st == geometry_status_t::OK; ++j ) {
            FreeAll( &c, &pieces );
            brush_solid_t *p = CopySolid( &c, brushesB.pData[j]->solid );
            if ( p == nullptr || !Push( &c, &pieces, p ) ) { break; }
            for ( usize i = 0u; i < brushesA.nCount && c.st == geometry_status_t::OK && pieces.nCount > 0u; ++i ) { Carve( &c, &pieces, brushesA.pData[i]->solid ); }
            for ( usize k = 0u; k < pieces.nCount && c.st == geometry_status_t::OK; ++k ) {
                if ( Push( &c, &result, pieces.pData[k] ) ) { pieces.pData[k] = nullptr; }
            }
            pieces.nCount = 0u;
        }
    } else if ( c.st == geometry_status_t::OK ) { // INTERSECTION
        for ( usize i = 0u; i < brushesA.nCount && c.st == geometry_status_t::OK; ++i ) {
            for ( usize j = 0u; j < brushesB.nCount && c.st == geometry_status_t::OK; ++j ) {
                brush_solid_t *p = NewSolid( &c );
                if ( p == nullptr ) {
                    c.st = geometry_status_t::ALLOCATION_FAILED;
                    break;
                }
                const geometry_status_t st = BrushCSG_TryIntersect( &brushesA.pData[i]->solid, &brushesB.pData[j]->solid, c.pA, &c.work, policy, p );
                if ( st == geometry_status_t::DEGENERATE ) {
                    FreeSolid( &c, p );
                    continue;
                }
                if ( st != geometry_status_t::OK ) {
                    c.st = st;
                    FreeSolid( &c, p );
                    break;
                }
                FreeAll( &c, &pieces );
                if ( !Push( &c, &pieces, p ) ) { break; }
                // Disjoint from everything already produced.
                for ( usize k = 0u; k < result.nCount && c.st == geometry_status_t::OK && pieces.nCount > 0u; ++k ) { Carve( &c, &pieces, *result.pData[k] ); }
                for ( usize k = 0u; k < pieces.nCount && c.st == geometry_status_t::OK; ++k ) {
                    if ( Push( &c, &result, pieces.pData[k] ) ) { pieces.pData[k] = nullptr; }
                }
                pieces.nCount = 0u;
            }
        }
    }
    if ( c.st == geometry_status_t::OK ) { c.st = Publish( &c, result, pIdAllocator, pOut, pProvenance ); }
    FreeAll( &c, &pieces );
    FreeAll( &c, &result );
    HashMap_Shutdown( &c.origins );
    return c.st;
}

} // namespace cypher::editor::geometry
