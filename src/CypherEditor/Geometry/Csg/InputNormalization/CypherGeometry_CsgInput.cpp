//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgInput.cpp
//  Purpose: Implements CSG input preparation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgInput.h"
#include "CypherGeometry_Planar_Triangulate.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Drops the axis the face normal leans on most, keeping the 2D winding
// the same as the 3D winding seen from outside.
math::vec2d_t Project( math::vec3d_t p, u32 axis, bool bFlip ) noexcept
{
    const math::vec2d_t q = axis == 0u ? math::vec2d_t{ p.y, p.z } : axis == 1u ? math::vec2d_t{ p.z, p.x } : math::vec2d_t{ p.x, p.y };
    return bFlip ? math::vec2d_t{ q.y, q.x } : q;
}

bool TriangleFlat( math::vec3d_t a, math::vec3d_t b, math::vec3d_t c ) noexcept
{
    // Collinear in 3D iff collinear in all three axis projections.
    return math::Orient2D( { a.y, a.z }, { b.y, b.z }, { c.y, c.z } ) == 0 && math::Orient2D( { a.z, a.x }, { b.z, b.x }, { c.z, c.x } ) == 0 &&
           math::Orient2D( { a.x, a.y }, { b.x, b.y }, { c.x, c.y } ) == 0;
}

bool LexLess( math::vec3d_t a, math::vec3d_t b ) noexcept { return math::Vec3d_LexicographicLess( a, b ); }

} // namespace

geometry_status_t CsgOperand_Init( csg_operand_t *pOp, const allocator_t *pA ) noexcept
{
    if ( pOp == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const bool bOk = Vector_Init( &pOp->positions, pA ) && Vector_Init( &pOp->vertexIds, pA ) && Vector_Init( &pOp->triangles, pA ) &&
                     Vector_Init( &pOp->faceAttributes, pA ) && Vector_Init( &pOp->faceIds, pA ) && Vector_Init( &pOp->edges, pA );
    if ( !bOk ) {
        CsgOperand_Shutdown( pOp );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void CsgOperand_Shutdown( csg_operand_t *pOp ) noexcept
{
    if ( pOp == nullptr ) { return; }
    Vector_Shutdown( &pOp->positions );
    Vector_Shutdown( &pOp->vertexIds );
    Vector_Shutdown( &pOp->triangles );
    Vector_Shutdown( &pOp->faceAttributes );
    Vector_Shutdown( &pOp->faceIds );
    Vector_Shutdown( &pOp->edges );
}

geometry_status_t CsgInput_TryFromMeshSource( const mesh_source_t *pSource, csg_operand_t *pOp ) noexcept
{
    if ( pSource == nullptr || pOp == nullptr || pOp->positions.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pA = pOp->positions.pAllocator;
    mesh_source_description_t d{};
    geometry_status_t st = MeshSourceDescription_Init( &d, pA, GEOMETRY_SOURCE_ID_INVALID );
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( pSource, &d ); }
    vector_t<math::vec2d_t> ring{};
    vector_t<planar_ring_triangle_t> tris{};
    if ( st == geometry_status_t::OK && ( !Vector_Init( &ring, pA ) || !Vector_Init( &tris, pA ) ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
    const usize cV = d.vertices.nCount, cF = d.faces.nCount;
    if ( st == geometry_status_t::OK && ( !Vector_Resize( &pOp->positions, cV ) || !Vector_Resize( &pOp->vertexIds, cV ) ||
                                          !Vector_Resize( &pOp->faceAttributes, cF ) || !Vector_Resize( &pOp->faceIds, cF ) ||
                                          !Vector_Resize( &pOp->edges, d.edges.nCount ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st == geometry_status_t::OK ) {
        Vector_Clear( &pOp->triangles );
        for ( usize v = 0u; v < cV; ++v ) {
            pOp->positions.pData[v] = d.vertices.pData[v].position;
            pOp->vertexIds.pData[v] = d.vertices.pData[v].sourceId;
            if ( v == 0u ) { pOp->lo = pOp->hi = d.vertices.pData[v].position; }
            pOp->lo = math::Vec3d_Min( pOp->lo, d.vertices.pData[v].position );
            pOp->hi = math::Vec3d_Max( pOp->hi, d.vertices.pData[v].position );
        }
        for ( usize e = 0u; e < d.edges.nCount; ++e ) { pOp->edges.pData[e] = d.edges.pData[e]; }
        pOp->rootId = d.sourceId;
    }
    for ( usize f = 0u; st == geometry_status_t::OK && f < cF; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        pOp->faceAttributes.pData[f] = face.attributes;
        pOp->faceIds.pData[f] = face.sourceId;
        // Newell normal picks the projection that keeps the face widest.
        math::vec3d_t n{};
        for ( u32 k = 0u; k < face.cCorners; ++k ) {
            const math::vec3d_t a = d.vertices.pData[d.corners.pData[face.iFirstCorner + k].iVertex].position;
            const math::vec3d_t b = d.vertices.pData[d.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex].position;
            n.x += ( a.y - b.y ) * ( a.z + b.z );
            n.y += ( a.z - b.z ) * ( a.x + b.x );
            n.z += ( a.x - b.x ) * ( a.y + b.y );
        }
        const f64 ax = std::fabs( n.x ), ay = std::fabs( n.y ), az = std::fabs( n.z );
        const u32 axis = ax >= ay && ax >= az ? 0u : ay >= az ? 1u : 2u;
        const bool bFlip = math::Vec3d_Component( n, axis ) < 0.0;
        if ( !Vector_Resize( &ring, face.cCorners ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        for ( u32 k = 0u; k < face.cCorners; ++k ) {
            ring.pData[k] = Project( d.vertices.pData[d.corners.pData[face.iFirstCorner + k].iVertex].position, axis, bFlip );
        }
        Vector_Clear( &tris );
        st = Planar_TryTriangulateRing( Vector_Span( static_cast<const vector_t<math::vec2d_t> *>( &ring ) ), pA, &tris );
        if ( st != geometry_status_t::OK ) { break; }
        if ( pOp->triangles.nCount + tris.nCount > kCsgTrianglesMax ) {
            st = geometry_status_t::LIMIT_EXCEEDED;
            break;
        }
        for ( usize t = 0u; t < tris.nCount; ++t ) {
            csg_source_triangle_t tri{};
            const u32 k[3] = { tris.pData[t].a, tris.pData[t].b, tris.pData[t].c };
            for ( u32 i = 0u; i < 3u; ++i ) {
                const mesh_source_corner_t &corner = d.corners.pData[face.iFirstCorner + k[i]];
                tri.v[i] = corner.iVertex;
                tri.corners[i] = corner.attributes;
            }
            tri.iFace = static_cast<u32>( f );
            // Ear clipping can emit a zero-area triangle on collinear corners;
            // it carries no surface and would only feed degenerate cases on.
            if ( TriangleFlat( pOp->positions.pData[tri.v[0]], pOp->positions.pData[tri.v[1]], pOp->positions.pData[tri.v[2]] ) ) { continue; }
            if ( !Vector_PushBack( &pOp->triangles, tri ) ) {
                st = geometry_status_t::ALLOCATION_FAILED;
                break;
            }
        }
    }
    // Closed: every directed edge has its reverse, and none repeats.
    if ( st == geometry_status_t::OK ) {
        vector_t<u64> directed{};
        if ( !Vector_Init( &directed, pA ) || !Vector_Resize( &directed, 3u * pOp->triangles.nCount ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
        } else {
            for ( usize t = 0u; t < pOp->triangles.nCount; ++t ) {
                for ( u32 i = 0u; i < 3u; ++i ) {
                    directed.pData[3u * t + i] = ( static_cast<u64>( pOp->triangles.pData[t].v[i] ) << 32u ) | pOp->triangles.pData[t].v[( i + 1u ) % 3u];
                }
            }
            std::sort( directed.pData, directed.pData + directed.nCount );
            bool bClosed = true;
            for ( usize i = 0u; bClosed && i < directed.nCount; ++i ) {
                const u64 k = directed.pData[i];
                const u64 rev = ( k << 32u ) | ( k >> 32u );
                bClosed = std::binary_search( directed.pData, directed.pData + directed.nCount, rev ) &&
                          ( i + 1u >= directed.nCount || directed.pData[i + 1u] != k );
            }
            pOp->bClosed = bClosed;
        }
    }
    MeshSourceDescription_Shutdown( &d );
    return st;
}

geometry_status_t CsgInput_TryQuantize( csg_operand_t *pOp, f64 step ) noexcept
{
    if ( pOp == nullptr || !std::isfinite( step ) || !( step > 0.0 ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    auto snap = [&]( math::vec3d_t p ) noexcept {
        return math::Vec3d_Make( std::round( p.x / step ) * step, std::round( p.y / step ) * step, std::round( p.z / step ) * step );
    };
    for ( usize t = 0u; t < pOp->triangles.nCount; ++t ) {
        const csg_source_triangle_t &tri = pOp->triangles.pData[t];
        if ( TriangleFlat( snap( pOp->positions.pData[tri.v[0]] ), snap( pOp->positions.pData[tri.v[1]] ), snap( pOp->positions.pData[tri.v[2]] ) ) ) {
            return geometry_status_t::DEGENERATE;
        }
    }
    for ( usize v = 0u; v < pOp->positions.nCount; ++v ) { pOp->positions.pData[v] = snap( pOp->positions.pData[v] ); }
    pOp->lo = snap( pOp->lo );
    pOp->hi = snap( pOp->hi );
    return geometry_status_t::OK;
}

void CsgInput_Canonicalize( csg_operand_t *pOp ) noexcept
{
    if ( pOp == nullptr ) { return; }
    const math::vec3d_t *pos = pOp->positions.pData;
    // Rotate each triangle to start at its smallest corner (winding kept).
    for ( usize t = 0u; t < pOp->triangles.nCount; ++t ) {
        csg_source_triangle_t &tri = pOp->triangles.pData[t];
        u32 first = 0u;
        for ( u32 i = 1u; i < 3u; ++i ) {
            if ( LexLess( pos[tri.v[i]], pos[tri.v[first]] ) ) { first = i; }
        }
        const csg_source_triangle_t copy = tri;
        for ( u32 i = 0u; i < 3u; ++i ) {
            tri.v[i] = copy.v[( first + i ) % 3u];
            tri.corners[i] = copy.corners[( first + i ) % 3u];
        }
    }
    std::sort( pOp->triangles.pData, pOp->triangles.pData + pOp->triangles.nCount, [&]( const csg_source_triangle_t &x, const csg_source_triangle_t &y ) {
        for ( u32 i = 0u; i < 3u; ++i ) {
            if ( LexLess( pos[x.v[i]], pos[y.v[i]] ) ) { return true; }
            if ( LexLess( pos[y.v[i]], pos[x.v[i]] ) ) { return false; }
        }
        return x.iFace < y.iFace;
    } );
}

} // namespace cypher::editor::geometry
