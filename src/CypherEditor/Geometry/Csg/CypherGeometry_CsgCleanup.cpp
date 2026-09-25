//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgCleanup.cpp
//  Purpose: Implements CSG cleanup.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgCleanup.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

u64 Key( u32 a, u32 b ) noexcept { return a < b ? ( static_cast<u64>( a ) << 32u ) | b : ( static_cast<u64>( b ) << 32u ) | a; }

} // namespace

geometry_status_t CsgCleanup_TryRemoveRedundantVertices( csg_mesh_result_t *pR, usize *pcRemoved ) noexcept
{
    if ( pR == nullptr || pR->faceOperand.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pA = pR->faceOperand.pAllocator;
    mesh_source_description_t &d = pR->mesh;
    usize cRemovedTotal = 0u;
    vector_t<u64> edges{};
    vector_t<u32> neighbourCount{}, n0{}, n1{}, remap{};
    vector_t<u8> remove{}, faceSafe{};
    if ( !Vector_Init( &edges, pA ) || !Vector_Init( &neighbourCount, pA ) || !Vector_Init( &n0, pA ) || !Vector_Init( &n1, pA ) || !Vector_Init( &remap, pA ) ||
         !Vector_Init( &remove, pA ) || !Vector_Init( &faceSafe, pA ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 pass = 0u; pass < 64u; ++pass ) {
        const usize cV = d.vertices.nCount;
        Vector_Clear( &edges );
        for ( usize f = 0u; f < d.faces.nCount; ++f ) {
            const mesh_source_face_t &face = d.faces.pData[f];
            for ( u32 k = 0u; k < face.cCorners; ++k ) {
                const u32 a = d.corners.pData[face.iFirstCorner + k].iVertex, b = d.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex;
                if ( !Vector_PushBack( &edges, Key( a, b ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            }
        }
        std::sort( edges.pData, edges.pData + edges.nCount );
        edges.nCount = static_cast<usize>( std::unique( edges.pData, edges.pData + edges.nCount ) - edges.pData );
        if ( !Vector_Resize( &neighbourCount, cV ) || !Vector_Resize( &n0, cV ) || !Vector_Resize( &n1, cV ) || !Vector_Resize( &remove, cV ) ||
             !Vector_Resize( &remap, cV ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        for ( usize v = 0u; v < cV; ++v ) {
            neighbourCount.pData[v] = 0u;
            remove.pData[v] = 0u;
        }
        for ( usize e = 0u; e < edges.nCount; ++e ) {
            const u32 a = static_cast<u32>( edges.pData[e] >> 32u ), b = static_cast<u32>( edges.pData[e] & 0xFFFFFFFFu );
            for ( const u32 v : { a, b } ) {
                const u32 other = v == a ? b : a;
                const u32 c = neighbourCount.pData[v]++;
                if ( c == 0u ) { n0.pData[v] = other; }
                if ( c == 1u ) { n1.pData[v] = other; }
            }
        }
        // Candidates: constructed, two neighbours, on the line between them.
        usize cCandidates = 0u;
        for ( usize v = 0u; v < cV; ++v ) {
            if ( neighbourCount.pData[v] != 2u || GeometrySourceId_IsValid( d.vertices.pData[v].sourceId ) ) { continue; }
            const math::vec3d_t p = d.vertices.pData[v].position;
            const math::vec3d_t u = math::Vec3d_Subtract( d.vertices.pData[n0.pData[v]].position, p );
            const math::vec3d_t w = math::Vec3d_Subtract( d.vertices.pData[n1.pData[v]].position, p );
            const f64 lu = std::sqrt( math::Vec3d_LengthSquared( u ) ), lw = std::sqrt( math::Vec3d_LengthSquared( w ) );
            const f64 cross = std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Cross( u, w ) ) );
            // On the line *between* them (opposite directions), not a spike.
            if ( cross <= kCsgCollinearRelative * lu * lw && math::Vec3d_Dot( u, w ) < 0.0 ) {
                remove.pData[v] = 1u;
                ++cCandidates;
            }
        }
        if ( cCandidates == 0u ) { break; }
        // A face must keep three corners; unmark vertices that would break one.
        for ( usize f = 0u; f < d.faces.nCount; ++f ) {
            const mesh_source_face_t &face = d.faces.pData[f];
            u32 cLeft = 0u;
            for ( u32 k = 0u; k < face.cCorners; ++k ) { cLeft += remove.pData[d.corners.pData[face.iFirstCorner + k].iVertex] ? 0u : 1u; }
            if ( cLeft < 3u ) {
                for ( u32 k = 0u; k < face.cCorners; ++k ) { remove.pData[d.corners.pData[face.iFirstCorner + k].iVertex] = 0u; }
            }
        }
        // Also avoid removing both neighbours' shared chain in one pass
        // (the next pass sees the new neighbours).
        for ( usize v = 0u; v < cV; ++v ) {
            if ( remove.pData[v] && ( remove.pData[n0.pData[v]] || remove.pData[n1.pData[v]] ) && ( n0.pData[v] < v || n1.pData[v] < v ) ) {
                remove.pData[v] = 0u;
            }
        }
        // Rebuild: corners, faces, vertices, keys, edge entries.
        usize cRemoved = 0u;
        for ( usize v = 0u; v < cV; ++v ) {
            if ( remove.pData[v] ) {
                remap.pData[v] = CY_U32_MAX;
                ++cRemoved;
            } else {
                remap.pData[v] = static_cast<u32>( v - cRemoved );
            }
        }
        if ( cRemoved == 0u ) { break; }
        usize wc = 0u;
        for ( usize f = 0u; f < d.faces.nCount; ++f ) {
            mesh_source_face_t &face = d.faces.pData[f];
            const u32 first = face.iFirstCorner, n = face.cCorners;
            face.iFirstCorner = static_cast<u32>( wc );
            u32 kept = 0u;
            for ( u32 k = 0u; k < n; ++k ) {
                const mesh_source_corner_t c = d.corners.pData[first + k];
                if ( remap.pData[c.iVertex] == CY_U32_MAX ) { continue; }
                d.corners.pData[wc] = c;
                d.corners.pData[wc].iVertex = remap.pData[c.iVertex];
                ++wc;
                ++kept;
            }
            face.cCorners = kept;
        }
        d.corners.nCount = wc;
        // Edge entries: an entry through a removed vertex continues past it.
        usize we = 0u;
        for ( usize e = 0u; e < d.edges.nCount; ++e ) {
            mesh_source_edge_t edge = d.edges.pData[e];
            u32 a = edge.iVertexA, b = edge.iVertexB;
            if ( remove.pData[a] ) { a = n0.pData[a] == b ? n1.pData[a] : n0.pData[a]; }
            if ( remove.pData[b] ) { b = n0.pData[b] == edge.iVertexA ? n1.pData[b] : n0.pData[b]; }
            if ( remove.pData[a] || remove.pData[b] ) { continue; }
            a = remap.pData[a];
            b = remap.pData[b];
            if ( a == b ) { continue; }
            edge.iVertexA = std::min( a, b );
            edge.iVertexB = std::max( a, b );
            d.edges.pData[we++] = edge;
        }
        d.edges.nCount = we;
        std::sort( d.edges.pData, d.edges.pData + we, []( const mesh_source_edge_t &x, const mesh_source_edge_t &y ) {
            return x.iVertexA != y.iVertexA ? x.iVertexA < y.iVertexA : x.iVertexB < y.iVertexB;
        } );
        usize wu = 0u;
        for ( usize e = 0u; e < we; ++e ) {
            if ( wu > 0u && d.edges.pData[wu - 1u].iVertexA == d.edges.pData[e].iVertexA && d.edges.pData[wu - 1u].iVertexB == d.edges.pData[e].iVertexB ) {
                d.edges.pData[wu - 1u].attributes.flags = static_cast<u8>( d.edges.pData[wu - 1u].attributes.flags | d.edges.pData[e].attributes.flags );
                d.edges.pData[wu - 1u].creaseWeight = std::max( d.edges.pData[wu - 1u].creaseWeight, d.edges.pData[e].creaseWeight );
                continue;
            }
            d.edges.pData[wu++] = d.edges.pData[e];
        }
        d.edges.nCount = wu;
        usize wv = 0u;
        for ( usize v = 0u; v < cV; ++v ) {
            if ( remove.pData[v] ) { continue; }
            d.vertices.pData[wv] = d.vertices.pData[v];
            pR->vertexKeys.pData[wv] = pR->vertexKeys.pData[v];
            ++wv;
        }
        d.vertices.nCount = wv;
        pR->vertexKeys.nCount = wv;
        cRemovedTotal += cRemoved;
    }
    if ( pcRemoved != nullptr ) { *pcRemoved = cRemovedTotal; }
    return geometry_status_t::OK;
}

geometry_status_t CsgCleanup_TryValidate( const csg_mesh_result_t *pR, bool bExpectClosed, csg_diagnostics_t *pDiag ) noexcept
{
    if ( pR == nullptr || pR->faceOperand.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const mesh_source_description_t &d = pR->mesh;
    vector_t<u64> directed{};
    if ( !Vector_Init( &directed, pR->faceOperand.pAllocator ) || !Vector_Resize( &directed, d.corners.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    usize n = 0u;
    for ( usize f = 0u; f < d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        for ( u32 k = 0u; k < face.cCorners; ++k ) {
            const u64 a = d.corners.pData[face.iFirstCorner + k].iVertex, b = d.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex;
            directed.pData[n++] = ( a << 32u ) | b;
        }
    }
    std::sort( directed.pData, directed.pData + n );
    auto witness = [&]( u64 key, geometry_status_t st ) noexcept {
        if ( pDiag != nullptr ) {
            const u32 a = static_cast<u32>( key >> 32u ), b = static_cast<u32>( key & 0xFFFFFFFFu );
            pDiag->witness = math::Vec3d_Scale( math::Vec3d_Add( d.vertices.pData[a].position, d.vertices.pData[b].position ), 0.5 );
            pDiag->stage = csg_stage_t::RECONSTRUCTION;
        }
        return st;
    };
    for ( usize i = 0u; i < n; ++i ) {
        if ( i > 0u && directed.pData[i] == directed.pData[i - 1u] ) { return witness( directed.pData[i], geometry_status_t::NON_MANIFOLD ); }
        if ( bExpectClosed ) {
            const u64 k = directed.pData[i];
            const u64 rev = ( k << 32u ) | ( k >> 32u );
            if ( !std::binary_search( directed.pData, directed.pData + n, rev ) ) { return witness( k, geometry_status_t::OPEN_VOLUME ); }
        }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
