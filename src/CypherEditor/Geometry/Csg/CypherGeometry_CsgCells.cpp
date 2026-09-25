//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgCells.cpp
//  Purpose: Implements the CSG cell complex.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgCells.h"

#include <algorithm>

namespace cypher::editor::geometry
{

using namespace cypher::common;

geometry_status_t CsgCells_Init( csg_cell_complex_t *p, const allocator_t *pA ) noexcept
{
    if ( p == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const bool bOk = Vector_Init( &p->triangleCell, pA ) && Vector_Init( &p->cellStart, pA ) && Vector_Init( &p->cellTriangles, pA ) &&
                     Vector_Init( &p->cellOperand, pA ) && Vector_Init( &p->cellLabel, pA );
    if ( !bOk ) {
        CsgCells_Shutdown( p );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void CsgCells_Shutdown( csg_cell_complex_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    Vector_Shutdown( &p->triangleCell );
    Vector_Shutdown( &p->cellStart );
    Vector_Shutdown( &p->cellTriangles );
    Vector_Shutdown( &p->cellOperand );
    Vector_Shutdown( &p->cellLabel );
}

usize CsgCells_Count( const csg_cell_complex_t *p ) noexcept { return p != nullptr ? p->cellOperand.nCount : 0u; }

geometry_status_t CsgCells_TryBuild( const vector_t<csg_refined_triangle_t> &tris, usize cTrianglesA, csg_cell_complex_t *pCells ) noexcept
{
    if ( pCells == nullptr || pCells->triangleCell.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pA = pCells->triangleCell.pAllocator;
    const usize cT = tris.nCount;
    // (edge key, triangle) incidences, sorted by edge.
    struct inc_t {
        u64 key;
        u32 tri;
    };
    vector_t<inc_t> inc{};
    vector_t<u32> parent{};
    if ( !Vector_Init( &inc, pA ) || !Vector_Init( &parent, pA ) || !Vector_Resize( &inc, 3u * cT ) || !Vector_Resize( &parent, cT ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize t = 0u; t < cT; ++t ) {
        parent.pData[t] = static_cast<u32>( t );
        for ( u32 k = 0u; k < 3u; ++k ) {
            const u32 a = tris.pData[t].v[k], b = tris.pData[t].v[( k + 1u ) % 3u];
            const u64 key = a < b ? ( static_cast<u64>( a ) << 32u ) | b : ( static_cast<u64>( b ) << 32u ) | a;
            inc.pData[3u * t + k] = inc_t{ key, static_cast<u32>( t ) };
        }
    }
    std::sort( inc.pData, inc.pData + inc.nCount, []( const inc_t &x, const inc_t &y ) { return x.key != y.key ? x.key < y.key : x.tri < y.tri; } );
    auto find = [&]( u32 v ) noexcept {
        while ( parent.pData[v] != v ) {
            parent.pData[v] = parent.pData[parent.pData[v]];
            v = parent.pData[v];
        }
        return v;
    };
    auto isA = [&]( u32 t ) noexcept { return t < cTrianglesA; };
    for ( usize i = 0u; i < inc.nCount; ) {
        usize j = i + 1u;
        while ( j < inc.nCount && inc.pData[j].key == inc.pData[i].key ) { ++j; }
        u32 cA = 0u, cB = 0u, tA[2] = { 0u, 0u }, tB[2] = { 0u, 0u };
        for ( usize k = i; k < j; ++k ) {
            const u32 t = inc.pData[k].tri;
            if ( isA( t ) ) {
                if ( cA < 2u ) { tA[cA] = t; }
                ++cA;
            } else {
                if ( cB < 2u ) { tB[cB] = t; }
                ++cB;
            }
        }
        if ( cA > 2u || cB > 2u ) { return geometry_status_t::NON_MANIFOLD; }
        // Only an edge one operand has to itself joins cells.
        if ( cB == 0u && cA == 2u ) { parent.pData[find( tA[0] )] = find( tA[1] ); }
        if ( cA == 0u && cB == 2u ) { parent.pData[find( tB[0] )] = find( tB[1] ); }
        i = j;
    }
    // Number the cells in order of their first triangle (deterministic).
    if ( !Vector_Resize( &pCells->triangleCell, cT ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    Vector_Clear( &pCells->cellOperand );
    Vector_Clear( &pCells->cellLabel );
    vector_t<u32> rootCell{};
    if ( !Vector_Init( &rootCell, pA ) || !Vector_Resize( &rootCell, cT ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize t = 0u; t < cT; ++t ) { rootCell.pData[t] = CY_U32_MAX; }
    for ( usize t = 0u; t < cT; ++t ) {
        const u32 r = find( static_cast<u32>( t ) );
        if ( rootCell.pData[r] == CY_U32_MAX ) {
            rootCell.pData[r] = static_cast<u32>( pCells->cellOperand.nCount );
            if ( !Vector_PushBack( &pCells->cellOperand, isA( static_cast<u32>( t ) ) ? kCsgOperandA : kCsgOperandB ) ||
                 !Vector_PushBack( &pCells->cellLabel, csg_label_t::UNKNOWN ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
        pCells->triangleCell.pData[t] = rootCell.pData[r];
    }
    const usize cCells = pCells->cellOperand.nCount;
    if ( !Vector_Resize( &pCells->cellStart, cCells + 1u ) || !Vector_Resize( &pCells->cellTriangles, cT ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize c = 0u; c <= cCells; ++c ) { pCells->cellStart.pData[c] = 0u; }
    for ( usize t = 0u; t < cT; ++t ) { ++pCells->cellStart.pData[pCells->triangleCell.pData[t] + 1u]; }
    for ( usize c = 0u; c < cCells; ++c ) { pCells->cellStart.pData[c + 1u] += pCells->cellStart.pData[c]; }
    for ( usize c = 0u; c < cCells; ++c ) { rootCell.pData[c] = pCells->cellStart.pData[c]; }
    for ( usize t = 0u; t < cT; ++t ) { pCells->cellTriangles.pData[rootCell.pData[pCells->triangleCell.pData[t]]++] = static_cast<u32>( t ); }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
