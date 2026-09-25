//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgAttributes.cpp
//  Purpose: Implements mesh-CSG attribute transfer.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgAttributes.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

mesh_corner_attributes_t CsgAttributes_Interpolate( const csg_operand_t *pOp, const csg_source_triangle_t &src, math::vec3d_t p ) noexcept
{
    const math::vec3d_t a = pOp->positions.pData[src.v[0]], b = pOp->positions.pData[src.v[1]], c = pOp->positions.pData[src.v[2]];
    const math::vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, a ) );
    const f64 nn = math::Vec3d_LengthSquared( n );
    // Signed sub-areas against the triangle normal: exact barycentrics for
    // points in the plane, a projection for points a rounding error off it.
    f64 w[3] = { 1.0, 0.0, 0.0 };
    if ( nn > 0.0 ) {
        w[0] = math::Vec3d_Dot( n, math::Vec3d_Cross( math::Vec3d_Subtract( b, p ), math::Vec3d_Subtract( c, p ) ) ) / nn;
        w[1] = math::Vec3d_Dot( n, math::Vec3d_Cross( math::Vec3d_Subtract( c, p ), math::Vec3d_Subtract( a, p ) ) ) / nn;
        w[2] = 1.0 - w[0] - w[1];
    }
    mesh_corner_attributes_t out{};
    for ( u32 i = 0u; i < 3u; ++i ) {
        out.uv0 = math::vec2d_t{ out.uv0.x + w[i] * src.corners[i].uv0.x, out.uv0.y + w[i] * src.corners[i].uv0.y };
        out.uv1 = math::vec2d_t{ out.uv1.x + w[i] * src.corners[i].uv1.x, out.uv1.y + w[i] * src.corners[i].uv1.y };
    }
    u32 color = 0u;
    for ( u32 shift = 0u; shift < 32u; shift += 8u ) {
        f64 ch = 0.0;
        for ( u32 i = 0u; i < 3u; ++i ) { ch += w[i] * static_cast<f64>( ( src.corners[i].colorRgba >> shift ) & 0xFFu ); }
        const f64 clamped = ch < 0.0 ? 0.0 : ch > 255.0 ? 255.0 : ch;
        color |= static_cast<u32>( std::lround( clamped ) ) << shift;
    }
    out.colorRgba = color;
    return out;
}

mesh_face_attributes_t CsgAttributes_Face( const csg_operand_t *pOp, const csg_source_triangle_t &src, bool bFlipped, const csg_attribute_policy_t &policy ) noexcept
{
    mesh_face_attributes_t a = pOp->faceAttributes.pData[src.iFace];
    if ( bFlipped && policy.bOverrideCutMaterial ) { a.material = policy.cutMaterial; }
    return a;
}

namespace
{

// The operand edges a point lies on (at most two: an EDGE_EDGE point is on
// one edge of each operand) or the vertex it is.
struct edge_ref_t {
    u32 op, v0, v1;
};

u32 EdgesOf( const csg_intersection_t *pX, u32 p, edge_ref_t *pOut, u32 *pVertexOp, u32 *pVertex ) noexcept
{
    const csg_point_key_t &k = pX->keys.pData[p];
    *pVertexOp = CY_U32_MAX;
    switch ( k.kind ) {
    case csg_point_kind_t::VERTEX:
        *pVertexOp = k.a;
        *pVertex = k.b;
        return 0u;
    case csg_point_kind_t::EDGE_FACE: pOut[0] = edge_ref_t{ k.a, k.b, k.c }; return 1u;
    case csg_point_kind_t::EDGE_EDGE:
        pOut[0] = edge_ref_t{ kCsgOperandA, k.a, k.b };
        pOut[1] = edge_ref_t{ kCsgOperandB, k.c, k.d };
        return 2u;
    }
    return 0u;
}

bool OnEdge( const csg_intersection_t *pX, u32 p, const edge_ref_t &e ) noexcept
{
    edge_ref_t refs[2];
    u32 vop = 0u, v = 0u;
    const u32 n = EdgesOf( pX, p, refs, &vop, &v );
    if ( vop == e.op && ( v == e.v0 || v == e.v1 ) ) { return true; }
    for ( u32 i = 0u; i < n; ++i ) {
        if ( refs[i].op == e.op && refs[i].v0 == e.v0 && refs[i].v1 == e.v1 ) { return true; }
    }
    return false;
}

bool LookupEdge( const csg_operand_t *pOp, u32 v0, u32 v1, mesh_source_edge_t *pOut ) noexcept
{
    for ( usize i = 0u; i < pOp->edges.nCount; ++i ) {
        const mesh_source_edge_t &e = pOp->edges.pData[i];
        if ( e.iVertexA == v0 && e.iVertexB == v1 ) {
            *pOut = e;
            return true;
        }
    }
    return false;
}

} // namespace

bool CsgAttributes_SourceEdge( const csg_intersection_t *pX, const csg_operand_t *pA, const csg_operand_t *pB, u32 p, u32 q, mesh_source_edge_t *pOut ) noexcept
{
    // Candidate operand edges: those p is on, and p-q itself when both are
    // corners of one operand.
    edge_ref_t cand[3];
    u32 cCand = 0u, vopP = 0u, vP = 0u, vopQ = 0u, vQ = 0u;
    edge_ref_t refsQ[2];
    cCand = EdgesOf( pX, p, cand, &vopP, &vP );
    (void)EdgesOf( pX, q, refsQ, &vopQ, &vQ );
    if ( vopP != CY_U32_MAX && vopP == vopQ && vP != vQ ) { cand[cCand++] = edge_ref_t{ vopP, std::min( vP, vQ ), std::max( vP, vQ ) }; }
    if ( vopP != CY_U32_MAX && vopQ == CY_U32_MAX ) {
        // p is a corner, q on an edge: the edge must end at p.
        edge_ref_t refs[2];
        u32 a = 0u, b = 0u;
        const u32 n = EdgesOf( pX, q, refs, &a, &b );
        for ( u32 i = 0u; i < n && cCand < 3u; ++i ) { cand[cCand++] = refs[i]; }
    }
    for ( u32 i = 0u; i < cCand; ++i ) {
        const edge_ref_t &e = cand[i];
        if ( !OnEdge( pX, p, e ) || !OnEdge( pX, q, e ) ) { continue; }
        mesh_source_edge_t src{};
        if ( LookupEdge( e.op == kCsgOperandA ? pA : pB, e.v0, e.v1, &src ) && ( src.creaseWeight > 0.0 || src.attributes.flags != 0u ) ) {
            *pOut = src;
            return true;
        }
    }
    return false;
}

} // namespace cypher::editor::geometry
