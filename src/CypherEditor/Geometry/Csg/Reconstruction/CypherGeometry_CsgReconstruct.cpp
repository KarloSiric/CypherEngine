//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgReconstruct.cpp
//  Purpose: Implements CSG reconstruction.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgReconstruct.h"
#include "CypherGeometry_CsgStitch.h"

#include <algorithm>
#include <utility>

namespace cypher::editor::geometry
{

using namespace cypher::common;

geometry_status_t CsgResult_Init( csg_mesh_result_t *p, const allocator_t *pA ) noexcept
{
    if ( p == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_status_t st = MeshSourceDescription_Init( &p->mesh, pA, GEOMETRY_SOURCE_ID_INVALID );
    if ( st == geometry_status_t::OK && ( !Vector_Init( &p->faceOperand, pA ) || !Vector_Init( &p->faceSource, pA ) || !Vector_Init( &p->faceFlipped, pA ) ||
                                          !Vector_Init( &p->vertexKeys, pA ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) { CsgResult_Shutdown( p ); }
    return st;
}

void CsgResult_Shutdown( csg_mesh_result_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    MeshSourceDescription_Shutdown( &p->mesh );
    Vector_Shutdown( &p->faceOperand );
    Vector_Shutdown( &p->faceSource );
    Vector_Shutdown( &p->faceFlipped );
    Vector_Shutdown( &p->vertexKeys );
}

void CsgResult_Clear( csg_mesh_result_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    MeshSourceDescription_Clear( &p->mesh, GEOMETRY_SOURCE_ID_INVALID );
    Vector_Clear( &p->faceOperand );
    Vector_Clear( &p->faceSource );
    Vector_Clear( &p->faceFlipped );
    Vector_Clear( &p->vertexKeys );
}

namespace
{

struct face_group_t {
    u32 op, face;
    bool bFlipped;
};

const csg_source_triangle_t &SourceOf( const csg_intersection_t *pX, const csg_operand_t *pA, const csg_operand_t *pB, u32 iSource, u32 *pOp ) noexcept
{
    *pOp = iSource < pX->cTrianglesA ? kCsgOperandA : kCsgOperandB;
    return *pOp == kCsgOperandA ? pA->triangles.pData[iSource] : pB->triangles.pData[iSource - pX->cTrianglesA];
}

} // namespace

geometry_status_t CsgReconstruct_TryBuild( const vector_t<csg_boundary_triangle_t> &tris, const csg_intersection_t *pX, const csg_operand_t *pA,
                                           const csg_operand_t *pB, const csg_attribute_policy_t &policy, csg_mesh_result_t *pR, csg_diagnostics_t *pDiag ) noexcept
{
    if ( pX == nullptr || pA == nullptr || pB == nullptr || pR == nullptr || pR->faceOperand.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pAlloc = pR->faceOperand.pAllocator;
    CsgResult_Clear( pR );
    vector_t<u32> p2v{}, v2p{};
    if ( !Vector_Init( &p2v, pAlloc ) || !Vector_Init( &v2p, pAlloc ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    usize cNear = 0u;
    geometry_status_t st = CsgStitch_TryCompact( tris, pX->positions, &p2v, &v2p, &cNear );
    if ( st != geometry_status_t::OK ) { return st; }
    if ( pDiag != nullptr ) { pDiag->cNearDuplicatePoints = cNear; }

    // Group triangles by (operand, source face, facing).
    const usize cT = tris.nCount;
    vector_t<u32> order{};
    vector_t<face_group_t> group{};
    if ( !Vector_Init( &order, pAlloc ) || !Vector_Init( &group, pAlloc ) || !Vector_Resize( &order, cT ) || !Vector_Resize( &group, cT ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize t = 0u; t < cT; ++t ) {
        u32 op = 0u;
        const csg_source_triangle_t &src = SourceOf( pX, pA, pB, tris.pData[t].iSource, &op );
        group.pData[t] = face_group_t{ op, src.iFace, tris.pData[t].bFlipped };
        order.pData[t] = static_cast<u32>( t );
    }
    std::stable_sort( order.pData, order.pData + cT, [&]( u32 x, u32 y ) {
        const face_group_t &a = group.pData[x], &b = group.pData[y];
        return a.op != b.op ? a.op < b.op : a.face != b.face ? a.face < b.face : a.bFlipped < b.bFlipped;
    } );

    // Faces, as corner rings of global points with per-corner source triangle.
    struct corner_t {
        u32 p, tri;
    };
    vector_t<corner_t> corners{};
    vector_t<u32> faceStart{};
    vector_t<u64> directed{}, boundary{};
    vector_t<u8> usedFaceId{};
    if ( !Vector_Init( &corners, pAlloc ) || !Vector_Init( &faceStart, pAlloc ) || !Vector_Init( &directed, pAlloc ) || !Vector_Init( &boundary, pAlloc ) ||
         !Vector_Init( &usedFaceId, pAlloc ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // A face starts at `first` in corners (its corners may already be in).
    auto pushFace = [&]( const face_group_t &g, usize first ) noexcept -> bool {
        return Vector_PushBack( &faceStart, static_cast<u32>( first ) ) && Vector_PushBack( &pR->faceOperand, g.op ) &&
               Vector_PushBack( &pR->faceSource, g.face ) && Vector_PushBack( &pR->faceFlipped, static_cast<u8>( g.bFlipped ? 1u : 0u ) );
    };
    for ( usize i = 0u; i < cT; ) {
        usize j = i + 1u;
        const face_group_t g = group.pData[order.pData[i]];
        while ( j < cT && group.pData[order.pData[j]].op == g.op && group.pData[order.pData[j]].face == g.face &&
                group.pData[order.pData[j]].bFlipped == g.bFlipped ) {
            ++j;
        }
        // Boundary of the group: directed edges whose reverse is not in it.
        Vector_Clear( &directed );
        for ( usize k = i; k < j; ++k ) {
            const csg_boundary_triangle_t &t = tris.pData[order.pData[k]];
            for ( u32 e = 0u; e < 3u; ++e ) {
                if ( !Vector_PushBack( &directed, ( static_cast<u64>( t.v[e] ) << 32u ) | t.v[( e + 1u ) % 3u] ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            }
        }
        std::sort( directed.pData, directed.pData + directed.nCount );
        // Kept edges go to their own array: compacting in place would break
        // the binary searches still running over the sorted input.
        Vector_Clear( &boundary );
        for ( usize k = 0u; k < directed.nCount; ++k ) {
            const u64 e = directed.pData[k];
            const u64 rev = ( e << 32u ) | ( e >> 32u );
            if ( !std::binary_search( directed.pData, directed.pData + directed.nCount, rev ) && !Vector_PushBack( &boundary, e ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
        std::swap( directed.pData, boundary.pData );
        std::swap( directed.nCount, boundary.nCount );
        std::swap( directed.nCapacity, boundary.nCapacity );
        const usize w = directed.nCount;
        // One simple loop: every boundary vertex starts exactly one edge.
        bool bLoop = w >= 3u && w <= kMeshSourceCornersPerFaceMax;
        for ( usize k = 1u; bLoop && k < w; ++k ) { bLoop = ( directed.pData[k] >> 32u ) != ( directed.pData[k - 1u] >> 32u ); }
        if ( bLoop ) {
            // Walk it: from the smallest start, repeatedly take the edge
            // leaving the current end.
            usize cWalked = 0u;
            u32 cur = static_cast<u32>( directed.pData[0] >> 32u );
            const u32 start = cur;
            const usize cornerBase = corners.nCount;
            do {
                const u64 probe = static_cast<u64>( cur ) << 32u;
                const u64 *pE = std::lower_bound( directed.pData, directed.pData + w, probe );
                if ( pE == directed.pData + w || ( *pE >> 32u ) != cur ) {
                    bLoop = false;
                    break;
                }
                // The corner's surface data comes from a triangle of the group at that point.
                u32 tri = tris.pData[order.pData[i]].iSource;
                for ( usize k = i; k < j; ++k ) {
                    const csg_boundary_triangle_t &t = tris.pData[order.pData[k]];
                    if ( t.v[0] == cur || t.v[1] == cur || t.v[2] == cur ) {
                        tri = t.iSource;
                        break;
                    }
                }
                if ( !Vector_PushBack( &corners, corner_t{ cur, tri } ) ) { return geometry_status_t::ALLOCATION_FAILED; }
                cur = static_cast<u32>( *pE & 0xFFFFFFFFu );
                ++cWalked;
            } while ( cur != start && cWalked <= w );
            if ( bLoop && cWalked == w && cur == start ) {
                if ( !pushFace( g, cornerBase ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            } else {
                corners.nCount = cornerBase;
                bLoop = false;
            }
        }
        if ( !bLoop ) {
            for ( usize k = i; k < j; ++k ) {
                const csg_boundary_triangle_t &t = tris.pData[order.pData[k]];
                if ( !pushFace( g, corners.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
                for ( u32 e = 0u; e < 3u; ++e ) {
                    if ( !Vector_PushBack( &corners, corner_t{ t.v[e], t.iSource } ) ) { return geometry_status_t::ALLOCATION_FAILED; }
                }
            }
        }
        i = j;
    }
    if ( !Vector_PushBack( &faceStart, static_cast<u32>( corners.nCount ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    const usize cF = faceStart.nCount - 1u;

    // Vertices actually used (a merged face drops points inside it).
    vector_t<u32> used{};
    if ( !Vector_Init( &used, pAlloc ) || !Vector_Resize( &used, pX->positions.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize p = 0u; p < pX->positions.nCount; ++p ) { used.pData[p] = CY_U32_MAX; }
    mesh_source_description_t &d = pR->mesh;
    vector_t<u32> vertexPoint{}; // result vertex -> global point
    if ( !Vector_Init( &vertexPoint, pAlloc ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize c = 0u; c < corners.nCount; ++c ) {
        const u32 p = corners.pData[c].p;
        if ( used.pData[p] != CY_U32_MAX ) { continue; }
        used.pData[p] = static_cast<u32>( d.vertices.nCount );
        const csg_point_key_t &key = pX->keys.pData[p];
        mesh_source_vertex_t v{};
        v.position = pX->positions.pData[p];
        if ( key.kind == csg_point_kind_t::VERTEX ) { v.sourceId = ( key.a == kCsgOperandA ? pA : pB )->vertexIds.pData[key.b]; }
        if ( !Vector_PushBack( &d.vertices, v ) || !Vector_PushBack( &pR->vertexKeys, key ) || !Vector_PushBack( &vertexPoint, p ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    // Faces with attributes; the first face from each source face keeps its ID.
    const usize cFaceIds = pA->faceIds.nCount + pB->faceIds.nCount;
    if ( !Vector_Resize( &usedFaceId, cFaceIds ) || !Vector_Resize( &d.faces, cF ) || !Vector_Resize( &d.corners, corners.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize k = 0u; k < cFaceIds; ++k ) { usedFaceId.pData[k] = 0u; }
    for ( usize f = 0u; f < cF; ++f ) {
        const u32 first = faceStart.pData[f], n = faceStart.pData[f + 1u] - first;
        u32 op = 0u;
        const csg_source_triangle_t &src0 = SourceOf( pX, pA, pB, corners.pData[first].tri, &op );
        const csg_operand_t *pOp = op == kCsgOperandA ? pA : pB;
        mesh_source_face_t &face = d.faces.pData[f];
        face.iFirstCorner = first;
        face.cCorners = n;
        face.attributes = CsgAttributes_Face( pOp, src0, pR->faceFlipped.pData[f] != 0u, policy );
        const usize idSlot = ( op == kCsgOperandA ? 0u : pA->faceIds.nCount ) + src0.iFace;
        face.sourceId = GEOMETRY_SOURCE_ID_INVALID;
        if ( usedFaceId.pData[idSlot] == 0u ) {
            usedFaceId.pData[idSlot] = 1u;
            face.sourceId = pOp->faceIds.pData[src0.iFace];
        }
        for ( u32 k = 0u; k < n; ++k ) {
            const corner_t &c = corners.pData[first + k];
            u32 cop = 0u;
            const csg_source_triangle_t &src = SourceOf( pX, pA, pB, c.tri, &cop );
            mesh_source_corner_t &mc = d.corners.pData[first + k];
            mc.iVertex = used.pData[c.p];
            mc.attributes = CsgAttributes_Interpolate( cop == kCsgOperandA ? pA : pB, src, pX->positions.pData[c.p] );
        }
    }
    // Edges: inherited crease/flags, and hard seams where faces of different
    // origin meet.
    struct edge_face_t {
        u64 key;
        u32 face;
    };
    vector_t<edge_face_t> ef{};
    if ( !Vector_Init( &ef, pAlloc ) || !Vector_Resize( &ef, corners.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize f = 0u; f < cF; ++f ) {
        const u32 first = faceStart.pData[f], n = faceStart.pData[f + 1u] - first;
        for ( u32 k = 0u; k < n; ++k ) {
            const u32 a = d.corners.pData[first + k].iVertex, b = d.corners.pData[first + ( k + 1u ) % n].iVertex;
            ef.pData[first + k] = edge_face_t{ a < b ? ( static_cast<u64>( a ) << 32u ) | b : ( static_cast<u64>( b ) << 32u ) | a, static_cast<u32>( f ) };
        }
    }
    std::sort( ef.pData, ef.pData + ef.nCount, []( const edge_face_t &x, const edge_face_t &y ) { return x.key != y.key ? x.key < y.key : x.face < y.face; } );
    for ( usize i = 0u; i < ef.nCount; ) {
        usize j = i + 1u;
        while ( j < ef.nCount && ef.pData[j].key == ef.pData[i].key ) { ++j; }
        const u32 a = static_cast<u32>( ef.pData[i].key >> 32u ), b = static_cast<u32>( ef.pData[i].key & 0xFFFFFFFFu );
        mesh_source_edge_t edge{};
        mesh_source_edge_t inherited{};
        bool bSpecial = CsgAttributes_SourceEdge( pX, pA, pB, vertexPoint.pData[a], vertexPoint.pData[b], &inherited );
        if ( bSpecial ) { edge = inherited; }
        edge.iVertexA = a;
        edge.iVertexB = b;
        if ( j - i == 2u && policy.bHardSeams ) {
            const u32 f0 = ef.pData[i].face, f1 = ef.pData[i + 1u].face;
            if ( pR->faceOperand.pData[f0] != pR->faceOperand.pData[f1] || pR->faceFlipped.pData[f0] != pR->faceFlipped.pData[f1] ) {
                edge.attributes.flags = static_cast<u8>( edge.attributes.flags | MESH_EDGE_FLAG_HARD );
                bSpecial = true;
            }
        }
        if ( bSpecial && !Vector_PushBack( &d.edges, edge ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        i = j;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
