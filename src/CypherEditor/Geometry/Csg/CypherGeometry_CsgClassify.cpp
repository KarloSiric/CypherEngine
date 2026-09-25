//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgClassify.cpp
//  Purpose: Implements CSG classification.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgClassify.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

f64 CsgClassify_WindingNumber( const csg_operand_t *pOp, math::vec3d_t p ) noexcept
{
    // Van Oosterom-Strackee: the solid angle of a triangle seen from p.
    f64 sum = 0.0;
    for ( usize t = 0u; t < pOp->triangles.nCount; ++t ) {
        const csg_source_triangle_t &tri = pOp->triangles.pData[t];
        const math::vec3d_t a = math::Vec3d_Subtract( pOp->positions.pData[tri.v[0]], p );
        const math::vec3d_t b = math::Vec3d_Subtract( pOp->positions.pData[tri.v[1]], p );
        const math::vec3d_t c = math::Vec3d_Subtract( pOp->positions.pData[tri.v[2]], p );
        const f64 la = std::sqrt( math::Vec3d_LengthSquared( a ) ), lb = std::sqrt( math::Vec3d_LengthSquared( b ) ),
                  lc = std::sqrt( math::Vec3d_LengthSquared( c ) );
        const f64 num = math::Vec3d_Dot( a, math::Vec3d_Cross( b, c ) );
        const f64 den = la * lb * lc + math::Vec3d_Dot( a, b ) * lc + math::Vec3d_Dot( a, c ) * lb + math::Vec3d_Dot( b, c ) * la;
        sum += 2.0 * std::atan2( num, den );
    }
    return sum / ( 4.0 * math::CY_PI_D );
}

geometry_status_t CsgClassify_TryLabel( const vector_t<csg_refined_triangle_t> &tris, const vector_t<csg_label_t> &triLabels, const csg_intersection_t *pX,
                                        const csg_operand_t *pA, const csg_operand_t *pB, bool bClassifyA, bool bClassifyB, csg_cell_complex_t *pCells,
                                        csg_diagnostics_t *pDiag ) noexcept
{
    if ( pX == nullptr || pA == nullptr || pB == nullptr || pCells == nullptr || triLabels.nCount != tris.nCount ) { return geometry_status_t::INVALID_ARGUMENT; }
    const usize cCells = CsgCells_Count( pCells );
    vector_t<u32> order{};
    if ( !Vector_Init( &order, pCells->triangleCell.pAllocator ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    auto area = [&]( u32 t ) noexcept {
        const math::vec3d_t p0 = pX->positions.pData[tris.pData[t].v[0]], p1 = pX->positions.pData[tris.pData[t].v[1]],
                            p2 = pX->positions.pData[tris.pData[t].v[2]];
        return math::Vec3d_LengthSquared( math::Vec3d_Cross( math::Vec3d_Subtract( p1, p0 ), math::Vec3d_Subtract( p2, p0 ) ) );
    };
    for ( usize c = 0u; c < cCells; ++c ) {
        const u32 op = pCells->cellOperand.pData[c];
        const u32 first = pCells->cellStart.pData[c], n = pCells->cellStart.pData[c + 1u] - first;
        const csg_label_t shared = triLabels.pData[pCells->cellTriangles.pData[first]];
        if ( shared == csg_label_t::SHARED_SAME || shared == csg_label_t::SHARED_OPPOSITE ) {
            pCells->cellLabel.pData[c] = shared;
            continue;
        }
        if ( ( op == kCsgOperandA && !bClassifyA ) || ( op == kCsgOperandB && !bClassifyB ) ) { continue; }
        const csg_operand_t *pOther = op == kCsgOperandA ? pB : pA;
        // Largest triangles first: their centroids sit farthest into the cell.
        if ( !Vector_Resize( &order, n ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        for ( u32 i = 0u; i < n; ++i ) { order.pData[i] = pCells->cellTriangles.pData[first + i]; }
        const u32 cTry = std::min( n, 8u );
        std::partial_sort( order.pData, order.pData + cTry, order.pData + n, [&]( u32 l, u32 r ) { return area( l ) > area( r ); } );
        csg_label_t label = csg_label_t::UNKNOWN;
        math::vec3d_t centroid{};
        for ( u32 i = 0u; i < cTry && label == csg_label_t::UNKNOWN; ++i ) {
            const csg_refined_triangle_t &t = tris.pData[order.pData[i]];
            centroid = math::Vec3d_Scale(
                math::Vec3d_Add( math::Vec3d_Add( pX->positions.pData[t.v[0]], pX->positions.pData[t.v[1]] ), pX->positions.pData[t.v[2]] ), 1.0 / 3.0 );
            const f64 w = std::fabs( CsgClassify_WindingNumber( pOther, centroid ) );
            if ( w > 0.75 ) {
                label = csg_label_t::INSIDE;
            } else if ( w < 0.25 ) {
                label = csg_label_t::OUTSIDE;
            }
        }
        if ( label == csg_label_t::UNKNOWN ) {
            if ( pDiag != nullptr ) {
                pDiag->witness = centroid;
                pDiag->iWitnessTriangle = order.pData[0];
            }
            return geometry_status_t::NUMERIC_FAILURE;
        }
        pCells->cellLabel.pData[c] = label;
    }
    if ( pDiag != nullptr ) { pDiag->cPatches = cCells; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
