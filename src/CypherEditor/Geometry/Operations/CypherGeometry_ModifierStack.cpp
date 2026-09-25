//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_ModifierStack.cpp
//  Purpose: Implements modifier stacks.
//  Details: Every stage rewrites one working description plus four
//           parallel provenance arrays. Duplicating stages (mirror, arrays)
//           append copies and then weld; deforming stages (bend, taper)
//           move points in place; subdivide hands the description to
//           Procedural/Subdivision and maps its provenance back.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_ModifierStack.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct work_t {
    mesh_source_description_t d{};
    vector_t<u32> vCopy{}, vSrc{}, fCopy{}, fSrc{};
    u32 cCopies{ 1u };

    bool Init( const allocator_t *pA ) noexcept
    {
        return MeshSourceDescription_Init( &d, pA, GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK && Vector_Init( &vCopy, pA ) &&
               Vector_Init( &vSrc, pA ) && Vector_Init( &fCopy, pA ) && Vector_Init( &fSrc, pA );
    }
    void Shutdown() noexcept { MeshSourceDescription_Shutdown( &d ); }
};

f64 Comp( math::vec3d_t v, u32 i ) noexcept { return math::Vec3d_Component( v, i ); }

// Appends `copies` transformed copies of every vertex and face (copy k
// uses xform(p, k)), numbering them after the existing copies.
// bReverse flips each copy's winding (a reflection turns faces inside out).
template <typename xform_t>
geometry_status_t AppendCopies( work_t *pW, u32 copies, bool bReverse, xform_t &&xform ) noexcept
{
    mesh_source_description_t &d = pW->d;
    const usize cV = d.vertices.nCount, cF = d.faces.nCount, cC = d.corners.nCount, cE = d.edges.nCount;
    const usize total = static_cast<usize>( copies ) + 1u;
    if ( !Vector_Resize( &d.vertices, cV * total ) || !Vector_Resize( &d.faces, cF * total ) || !Vector_Resize( &d.corners, cC * total ) ||
         !Vector_Resize( &d.edges, cE * total ) || !Vector_Resize( &pW->vCopy, cV * total ) || !Vector_Resize( &pW->vSrc, cV * total ) ||
         !Vector_Resize( &pW->fCopy, cF * total ) || !Vector_Resize( &pW->fSrc, cF * total ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 k = 1u; k <= copies; ++k ) {
        const usize vBase = cV * k, fBase = cF * k, cBase = cC * k, eBase = cE * k;
        for ( usize v = 0u; v < cV; ++v ) {
            d.vertices.pData[vBase + v].position = xform( d.vertices.pData[v].position, k );
            d.vertices.pData[vBase + v].sourceId = GEOMETRY_SOURCE_ID_INVALID;
            pW->vCopy.pData[vBase + v] = pW->vCopy.pData[v] + k * pW->cCopies;
            pW->vSrc.pData[vBase + v] = pW->vSrc.pData[v];
        }
        for ( usize f = 0u; f < cF; ++f ) {
            const mesh_source_face_t &src = d.faces.pData[f];
            mesh_source_face_t &dst = d.faces.pData[fBase + f];
            dst = src;
            dst.iFirstCorner = static_cast<u32>( cBase + src.iFirstCorner );
            dst.sourceId = GEOMETRY_SOURCE_ID_INVALID;
            for ( u32 c = 0u; c < src.cCorners; ++c ) {
                const u32 from = src.iFirstCorner + ( bReverse ? src.cCorners - 1u - c : c );
                mesh_source_corner_t corner = d.corners.pData[from];
                corner.iVertex = static_cast<u32>( corner.iVertex + vBase );
                d.corners.pData[dst.iFirstCorner + c] = corner;
            }
            pW->fCopy.pData[fBase + f] = pW->fCopy.pData[f] + k * pW->cCopies;
            pW->fSrc.pData[fBase + f] = pW->fSrc.pData[f];
        }
        for ( usize e = 0u; e < cE; ++e ) {
            mesh_source_edge_t edge = d.edges.pData[e];
            edge.iVertexA = static_cast<u32>( edge.iVertexA + vBase );
            edge.iVertexB = static_cast<u32>( edge.iVertexB + vBase );
            d.edges.pData[eBase + e] = edge;
        }
    }
    pW->cCopies *= copies + 1u;
    return geometry_status_t::OK;
}

// Merges vertices of *different* copies closer than eps (the earlier
// vertex survives with its ID), drops the rest, and rewrites corners and
// edge entries. A face left with a repeated corner is DEGENERATE.
geometry_status_t Weld( work_t *pW, f64 eps, const allocator_t *pA ) noexcept
{
    mesh_source_description_t &d = pW->d;
    const usize cV = d.vertices.nCount;
    vector_t<u32> order{}, rep{}, remap{};
    if ( !Vector_Init( &order, pA ) || !Vector_Init( &rep, pA ) || !Vector_Init( &remap, pA ) || !Vector_Resize( &order, cV ) ||
         !Vector_Resize( &rep, cV ) || !Vector_Resize( &remap, cV ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize v = 0u; v < cV; ++v ) {
        order.pData[v] = static_cast<u32>( v );
        rep.pData[v] = static_cast<u32>( v );
    }
    std::sort( order.pData, order.pData + cV, [&]( u32 a, u32 b ) { return d.vertices.pData[a].position.x < d.vertices.pData[b].position.x; } );
    auto find = [&]( u32 v ) noexcept {
        while ( rep.pData[v] != v ) { v = rep.pData[v]; }
        return v;
    };
    const f64 eps2 = eps * eps;
    for ( usize i = 0u; i < cV; ++i ) {
        const u32 a = order.pData[i];
        for ( usize j = i + 1u; j < cV; ++j ) {
            const u32 b = order.pData[j];
            if ( d.vertices.pData[b].position.x - d.vertices.pData[a].position.x > eps ) { break; }
            if ( pW->vCopy.pData[a] == pW->vCopy.pData[b] ) { continue; }
            if ( math::Vec3d_DistanceSquared( d.vertices.pData[a].position, d.vertices.pData[b].position ) > eps2 ) { continue; }
            const u32 ra = find( a ), rb = find( b );
            if ( ra != rb ) { rep.pData[std::max( ra, rb )] = std::min( ra, rb ); }
        }
    }
    // Compact: survivors keep their order.
    usize w = 0u;
    for ( usize v = 0u; v < cV; ++v ) {
        if ( find( static_cast<u32>( v ) ) == v ) {
            remap.pData[v] = static_cast<u32>( w );
            d.vertices.pData[w] = d.vertices.pData[v];
            pW->vCopy.pData[w] = pW->vCopy.pData[v];
            pW->vSrc.pData[w] = pW->vSrc.pData[v];
            ++w;
        }
    }
    for ( usize v = 0u; v < cV; ++v ) { remap.pData[v] = remap.pData[find( static_cast<u32>( v ) )]; }
    d.vertices.nCount = pW->vCopy.nCount = pW->vSrc.nCount = w;
    for ( usize c = 0u; c < d.corners.nCount; ++c ) { d.corners.pData[c].iVertex = remap.pData[d.corners.pData[c].iVertex]; }
    for ( usize f = 0u; f < d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        for ( u32 i = 0u; i < face.cCorners; ++i ) {
            for ( u32 j = i + 1u; j < face.cCorners; ++j ) {
                if ( d.corners.pData[face.iFirstCorner + i].iVertex == d.corners.pData[face.iFirstCorner + j].iVertex ) {
                    return geometry_status_t::DEGENERATE;
                }
            }
        }
    }
    // Edge entries: remap, order ends, drop collapsed ones, merge duplicates
    // (keep the larger crease and both flag sets).
    usize e2 = 0u;
    for ( usize e = 0u; e < d.edges.nCount; ++e ) {
        mesh_source_edge_t edge = d.edges.pData[e];
        u32 a = remap.pData[edge.iVertexA], b = remap.pData[edge.iVertexB];
        if ( a == b ) { continue; }
        if ( a > b ) { std::swap( a, b ); }
        edge.iVertexA = a;
        edge.iVertexB = b;
        d.edges.pData[e2++] = edge;
    }
    d.edges.nCount = e2;
    std::sort( d.edges.pData, d.edges.pData + e2, []( const mesh_source_edge_t &x, const mesh_source_edge_t &y ) {
        return x.iVertexA != y.iVertexA ? x.iVertexA < y.iVertexA : x.iVertexB < y.iVertexB;
    } );
    usize out = 0u;
    for ( usize e = 0u; e < e2; ++e ) {
        if ( out > 0u && d.edges.pData[out - 1u].iVertexA == d.edges.pData[e].iVertexA && d.edges.pData[out - 1u].iVertexB == d.edges.pData[e].iVertexB ) {
            mesh_source_edge_t &k = d.edges.pData[out - 1u];
            k.creaseWeight = std::max( k.creaseWeight, d.edges.pData[e].creaseWeight );
            k.attributes.flags = static_cast<u8>( k.attributes.flags | d.edges.pData[e].attributes.flags );
        } else {
            d.edges.pData[out++] = d.edges.pData[e];
        }
    }
    d.edges.nCount = out;
    return geometry_status_t::OK;
}

// Each undirected edge on at most two faces, each directed edge on one.
geometry_status_t CheckManifold( const mesh_source_description_t &d, const allocator_t *pA ) noexcept
{
    vector_t<u64> directed{};
    if ( !Vector_Init( &directed, pA ) || !Vector_Resize( &directed, d.corners.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    usize n = 0u;
    for ( usize f = 0u; f < d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        for ( u32 k = 0u; k < face.cCorners; ++k ) {
            const u64 a = d.corners.pData[face.iFirstCorner + k].iVertex;
            const u64 b = d.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex;
            directed.pData[n++] = ( a << 32u ) | b;
        }
    }
    std::sort( directed.pData, directed.pData + n );
    for ( usize i = 1u; i < n; ++i ) {
        if ( directed.pData[i] == directed.pData[i - 1u] ) { return geometry_status_t::NON_MANIFOLD; }
    }
    // Undirected: fold each key to (min, max) and count runs.
    for ( usize i = 0u; i < n; ++i ) {
        const u64 a = directed.pData[i] >> 32u, b = directed.pData[i] & 0xFFFFFFFFu;
        directed.pData[i] = a < b ? ( a << 32u ) | b : ( b << 32u ) | a;
    }
    std::sort( directed.pData, directed.pData + n );
    for ( usize i = 2u; i < n; ++i ) {
        if ( directed.pData[i] == directed.pData[i - 2u] ) { return geometry_status_t::NON_MANIFOLD; }
    }
    return geometry_status_t::OK;
}

// Rodrigues rotation of p about the line (origin, unit dir) by theta.
math::vec3d_t Rotate( math::vec3d_t p, math::vec3d_t origin, math::vec3d_t dir, f64 theta ) noexcept
{
    const math::vec3d_t v = math::Vec3d_Subtract( p, origin );
    const f64 c = std::cos( theta ), s = std::sin( theta );
    const math::vec3d_t r = math::Vec3d_Add( math::Vec3d_Add( math::Vec3d_Scale( v, c ), math::Vec3d_Scale( math::Vec3d_Cross( dir, v ), s ) ),
                                             math::Vec3d_Scale( dir, math::Vec3d_Dot( dir, v ) * ( 1.0 - c ) ) );
    return math::Vec3d_Add( origin, r );
}

void Extent( const mesh_source_description_t &d, u32 axis, f64 *pLo, f64 *pHi ) noexcept
{
    *pLo = d.vertices.nCount > 0u ? Comp( d.vertices.pData[0].position, axis ) : 0.0;
    *pHi = *pLo;
    for ( usize v = 1u; v < d.vertices.nCount; ++v ) {
        const f64 x = Comp( d.vertices.pData[v].position, axis );
        *pLo = std::min( *pLo, x );
        *pHi = std::max( *pHi, x );
    }
}

geometry_status_t RunStage( const modifier_t &m, work_t *pW, const allocator_t *pA ) noexcept
{
    mesh_source_description_t &d = pW->d;
    switch ( m.kind ) {
    case modifier_kind_t::MIRROR: {
        // Snap near-plane points exactly onto the plane first so both halves
        // meet there bit for bit.
        if ( m.bWeld ) {
            for ( usize v = 0u; v < d.vertices.nCount; ++v ) {
                math::vec3d_t &p = d.vertices.pData[v].position;
                if ( std::fabs( Comp( p, m.axis ) - m.offset ) <= m.weldDistance ) { math::Vec3d_SetComponent( &p, m.axis, m.offset ); }
            }
        }
        geometry_status_t st = AppendCopies( pW, 1u, true, [&]( math::vec3d_t p, u32 ) noexcept {
            math::Vec3d_SetComponent( &p, m.axis, 2.0 * m.offset - Comp( p, m.axis ) );
            return p;
        } );
        if ( st == geometry_status_t::OK && m.bWeld ) { st = Weld( pW, m.weldDistance, pA ); }
        return st;
    }
    case modifier_kind_t::LINEAR_ARRAY: {
        geometry_status_t st =
            AppendCopies( pW, m.cCopies - 1u, false, [&]( math::vec3d_t p, u32 k ) noexcept { return math::Vec3d_Add( p, math::Vec3d_Scale( m.step, k ) ); } );
        if ( st == geometry_status_t::OK && m.bWeld ) { st = Weld( pW, m.weldDistance, pA ); }
        return st;
    }
    case modifier_kind_t::RADIAL_ARRAY: {
        const math::vec3d_t dir = math::Vec3d_Scale( m.direction, 1.0 / std::sqrt( math::Vec3d_LengthSquared( m.direction ) ) );
        const bool bFullTurn = std::fabs( m.angle - 2.0 * math::CY_PI_D ) <= 1e-12;
        const f64 stepAngle = bFullTurn ? m.angle / m.cCopies : m.angle / ( m.cCopies - 1u );
        geometry_status_t st =
            AppendCopies( pW, m.cCopies - 1u, false, [&]( math::vec3d_t p, u32 k ) noexcept { return Rotate( p, m.origin, dir, stepAngle * k ); } );
        if ( st == geometry_status_t::OK && m.bWeld ) { st = Weld( pW, m.weldDistance, pA ); }
        return st;
    }
    case modifier_kind_t::BEND: {
        // Coordinates: x along alongAxis, z around `axis`, y the third
        // axis; the extent along x curls through `angle` on a circle of
        // radius L / angle, centred `origin` + R in y.
        const u32 ax = m.alongAxis, az = m.axis, ay = 3u - ax - az;
        f64 lo = 0.0, hi = 0.0;
        Extent( d, ax, &lo, &hi );
        const f64 L = hi - lo;
        if ( !( L > 0.0 ) || m.angle == 0.0 ) { return geometry_status_t::OK; }
        const f64 R = L / m.angle;
        const f64 x0 = Comp( m.origin, ax ), y0 = Comp( m.origin, ay );
        for ( usize v = 0u; v < d.vertices.nCount; ++v ) {
            math::vec3d_t &p = d.vertices.pData[v].position;
            const f64 x = Comp( p, ax ) - x0, y = Comp( p, ay ) - y0;
            const f64 phi = x / R;
            math::Vec3d_SetComponent( &p, ax, x0 + ( R - y ) * std::sin( phi ) );
            math::Vec3d_SetComponent( &p, ay, y0 + R - ( R - y ) * std::cos( phi ) );
        }
        return geometry_status_t::OK;
    }
    case modifier_kind_t::TAPER: {
        const u32 ax = m.alongAxis, a1 = ( ax + 1u ) % 3u, a2 = ( ax + 2u ) % 3u;
        f64 lo = 0.0, hi = 0.0;
        Extent( d, ax, &lo, &hi );
        if ( !( hi > lo ) ) { return geometry_status_t::OK; }
        const f64 c1 = Comp( m.origin, a1 ), c2 = Comp( m.origin, a2 );
        for ( usize v = 0u; v < d.vertices.nCount; ++v ) {
            math::vec3d_t &p = d.vertices.pData[v].position;
            const f64 s = 1.0 + ( m.factor - 1.0 ) * ( Comp( p, ax ) - lo ) / ( hi - lo );
            math::Vec3d_SetComponent( &p, a1, c1 + ( Comp( p, a1 ) - c1 ) * s );
            math::Vec3d_SetComponent( &p, a2, c2 + ( Comp( p, a2 ) - c2 ) * s );
        }
        return geometry_status_t::OK;
    }
    case modifier_kind_t::SUBDIVIDE: {
        subdivision_result_t r{};
        geometry_status_t st = SubdivisionResult_Init( &r, pA );
        if ( st == geometry_status_t::OK ) { st = Subdivision_TryEvaluateDescription( &d, m.subdivision, &r ); }
        vector_t<u32> vCopy{}, vSrc{}, fCopy{}, fSrc{};
        const usize cV = r.mesh.vertices.nCount, cF = r.mesh.faces.nCount;
        if ( st == geometry_status_t::OK && ( !Vector_Init( &vCopy, pA ) || !Vector_Init( &vSrc, pA ) || !Vector_Init( &fCopy, pA ) ||
                                              !Vector_Init( &fSrc, pA ) || !Vector_Resize( &vCopy, cV ) || !Vector_Resize( &vSrc, cV ) ||
                                              !Vector_Resize( &fCopy, cF ) || !Vector_Resize( &fSrc, cF ) ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
        }
        if ( st == geometry_status_t::OK ) {
            for ( usize v = 0u; v < cV; ++v ) {
                const subdivision_origin_t &o = r.vertexOrigins.pData[v];
                const bool bVertex = o.kind == subdivision_origin_kind_t::VERTEX;
                const bool bFace = o.kind == subdivision_origin_kind_t::FACE;
                vCopy.pData[v] = bFace ? pW->fCopy.pData[o.iSource] : pW->vCopy.pData[o.iSource];
                vSrc.pData[v] = bVertex ? pW->vSrc.pData[o.iSource] : CY_U32_MAX;
            }
            for ( usize f = 0u; f < cF; ++f ) {
                fCopy.pData[f] = pW->fCopy.pData[r.faceSources.pData[f]];
                fSrc.pData[f] = pW->fSrc.pData[r.faceSources.pData[f]];
            }
            // Take the result's arrays (allocation-free exchange).
            auto take = []( auto &dst, auto &src ) noexcept {
                std::swap( dst.pData, src.pData );
                std::swap( dst.nCount, src.nCount );
                std::swap( dst.nCapacity, src.nCapacity );
                std::swap( dst.pAllocator, src.pAllocator );
            };
            take( d.vertices, r.mesh.vertices );
            take( d.corners, r.mesh.corners );
            take( d.faces, r.mesh.faces );
            take( d.edges, r.mesh.edges );
            take( pW->vCopy, vCopy );
            take( pW->vSrc, vSrc );
            take( pW->fCopy, fCopy );
            take( pW->fSrc, fSrc );
        }
        SubdivisionResult_Shutdown( &r );
        return st;
    }
    }
    return geometry_status_t::INVALID_ARGUMENT;
}

void ClearResult( modifier_result_t *pR ) noexcept
{
    MeshSourceDescription_Clear( &pR->mesh, GEOMETRY_SOURCE_ID_INVALID );
    Vector_Clear( &pR->vertexCopies );
    Vector_Clear( &pR->vertexSources );
    Vector_Clear( &pR->faceCopies );
    Vector_Clear( &pR->faceSources );
}

} // namespace

geometry_status_t ModifierStack_Init( modifier_stack_t *pStack, const allocator_t *pAllocator ) noexcept
{
    if ( pStack == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    pStack->version = kModifierStackVersion;
    return Vector_Init( &pStack->stages, pAllocator ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
}

void ModifierStack_Shutdown( modifier_stack_t *pStack ) noexcept
{
    if ( pStack != nullptr ) { Vector_Shutdown( &pStack->stages ); }
}

geometry_status_t ModifierStack_TryPush( modifier_stack_t *pStack, const modifier_t &modifier ) noexcept
{
    if ( pStack == nullptr || pStack->stages.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    const geometry_status_t st = Modifier_Validate( modifier );
    if ( st != geometry_status_t::OK ) { return st; }
    if ( pStack->stages.nCount >= kModifierStagesMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    return Vector_PushBack( &pStack->stages, modifier ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t ModifierResult_Init( modifier_result_t *pResult, const allocator_t *pAllocator ) noexcept
{
    if ( pResult == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_status_t st = MeshSourceDescription_Init( &pResult->mesh, pAllocator, GEOMETRY_SOURCE_ID_INVALID );
    if ( st == geometry_status_t::OK && ( !Vector_Init( &pResult->vertexCopies, pAllocator ) || !Vector_Init( &pResult->vertexSources, pAllocator ) ||
                                          !Vector_Init( &pResult->faceCopies, pAllocator ) || !Vector_Init( &pResult->faceSources, pAllocator ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) { ModifierResult_Shutdown( pResult ); }
    return st;
}

void ModifierResult_Shutdown( modifier_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) { return; }
    MeshSourceDescription_Shutdown( &pResult->mesh );
    Vector_Shutdown( &pResult->vertexCopies );
    Vector_Shutdown( &pResult->vertexSources );
    Vector_Shutdown( &pResult->faceCopies );
    Vector_Shutdown( &pResult->faceSources );
}

geometry_status_t Modifier_Validate( const modifier_t &m ) noexcept
{
    if ( m.kind > modifier_kind_t::SUBDIVIDE || m.axis > 2u || m.alongAxis > 2u ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !std::isfinite( m.offset ) || !std::isfinite( m.weldDistance ) || m.weldDistance < 0.0 ) { return geometry_status_t::INVALID_ARGUMENT; }
    switch ( m.kind ) {
    case modifier_kind_t::MIRROR: return geometry_status_t::OK;
    case modifier_kind_t::LINEAR_ARRAY:
        return m.cCopies >= 2u && m.cCopies <= kModifierCopiesMax && math::Vec3d_IsFinite( m.step ) ? geometry_status_t::OK
                                                                                                     : geometry_status_t::INVALID_ARGUMENT;
    case modifier_kind_t::RADIAL_ARRAY: {
        const f64 len2 = math::Vec3d_LengthSquared( m.direction );
        const bool bOk = m.cCopies >= 2u && m.cCopies <= kModifierCopiesMax && math::Vec3d_IsFinite( m.origin ) && std::isfinite( len2 ) && len2 > 0.0 &&
                         std::isfinite( m.angle ) && m.angle > 0.0 && m.angle <= 2.0 * math::CY_PI_D + 1e-12;
        return bOk ? geometry_status_t::OK : geometry_status_t::INVALID_ARGUMENT;
    }
    case modifier_kind_t::BEND:
        return m.axis != m.alongAxis && std::isfinite( m.angle ) && std::fabs( m.angle ) <= 2.0 * math::CY_PI_D && math::Vec3d_IsFinite( m.origin )
                   ? geometry_status_t::OK
                   : geometry_status_t::INVALID_ARGUMENT;
    case modifier_kind_t::TAPER:
        return std::isfinite( m.factor ) && m.factor >= 0.0 && math::Vec3d_IsFinite( m.origin ) ? geometry_status_t::OK
                                                                                                 : geometry_status_t::INVALID_ARGUMENT;
    case modifier_kind_t::SUBDIVIDE: return Subdivision_ValidateDescriptor( m.subdivision );
    }
    return geometry_status_t::INVALID_ARGUMENT;
}

geometry_status_t ModifierStack_Validate( const modifier_stack_t *pStack ) noexcept
{
    if ( pStack == nullptr || pStack->stages.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pStack->version != kModifierStackVersion ) { return geometry_status_t::UNSUPPORTED; }
    if ( pStack->stages.nCount > kModifierStagesMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    for ( usize i = 0u; i < pStack->stages.nCount; ++i ) {
        const geometry_status_t st = Modifier_Validate( pStack->stages.pData[i] );
        if ( st != geometry_status_t::OK ) { return st; }
    }
    return geometry_status_t::OK;
}

geometry_status_t ModifierStack_TryEvaluate( const mesh_source_t *pCage, const modifier_stack_t *pStack, modifier_result_t *pResult ) noexcept
{
    if ( pCage == nullptr || pResult == nullptr || pResult->vertexCopies.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_status_t st = ModifierStack_Validate( pStack );
    if ( st != geometry_status_t::OK ) { return st; }
    const allocator_t *pA = pResult->vertexCopies.pAllocator;
    ClearResult( pResult );
    work_t w{};
    st = w.Init( pA ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( pCage, &w.d ); }
    const usize cV = w.d.vertices.nCount, cF = w.d.faces.nCount;
    if ( st == geometry_status_t::OK && ( !Vector_Resize( &w.vCopy, cV ) || !Vector_Resize( &w.vSrc, cV ) || !Vector_Resize( &w.fCopy, cF ) ||
                                          !Vector_Resize( &w.fSrc, cF ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st == geometry_status_t::OK ) {
        for ( usize v = 0u; v < cV; ++v ) {
            w.vCopy.pData[v] = 0u;
            w.vSrc.pData[v] = static_cast<u32>( v );
        }
        for ( usize f = 0u; f < cF; ++f ) {
            w.fCopy.pData[f] = 0u;
            w.fSrc.pData[f] = static_cast<u32>( f );
        }
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < pStack->stages.nCount; ++i ) {
        if ( pStack->stages.pData[i].bEnabled ) { st = RunStage( pStack->stages.pData[i], &w, pA ); }
    }
    if ( st == geometry_status_t::OK ) { st = CheckManifold( w.d, pA ); }
    if ( st == geometry_status_t::OK ) {
        auto take = []( auto &dst, auto &src ) noexcept {
            std::swap( dst.pData, src.pData );
            std::swap( dst.nCount, src.nCount );
            std::swap( dst.nCapacity, src.nCapacity );
            std::swap( dst.pAllocator, src.pAllocator );
        };
        take( pResult->mesh.vertices, w.d.vertices );
        take( pResult->mesh.corners, w.d.corners );
        take( pResult->mesh.faces, w.d.faces );
        take( pResult->mesh.edges, w.d.edges );
        pResult->mesh.sourceId = w.d.sourceId;
        take( pResult->vertexCopies, w.vCopy );
        take( pResult->vertexSources, w.vSrc );
        take( pResult->faceCopies, w.fCopy );
        take( pResult->faceSources, w.fSrc );
    }
    w.Shutdown();
    return st;
}

geometry_status_t ModifierStack_TryCollapse( const mesh_source_t *pCage, const modifier_stack_t *pStack, const allocator_t *pAllocator,
                                             geometry_source_id_allocator_t *pIdAllocator, mesh_source_t *pOut ) noexcept
{
    if ( pIdAllocator == nullptr || pOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    modifier_result_t r{};
    geometry_status_t st = ModifierResult_Init( &r, pAllocator );
    if ( st == geometry_status_t::OK ) { st = ModifierStack_TryEvaluate( pCage, pStack, &r ); }
    geometry_source_id_allocator_t ids = *pIdAllocator;
    auto fresh = [&]( geometry_source_id_t *pId ) noexcept {
        if ( st != geometry_status_t::OK || GeometrySourceId_IsValid( *pId ) ) { return; }
        const geometry_source_id_result_t n = GeometrySourceIdAllocator_Allocate( &ids );
        st = n.status;
        *pId = n.id;
    };
    for ( usize v = 0u; v < r.mesh.vertices.nCount; ++v ) { fresh( &r.mesh.vertices.pData[v].sourceId ); }
    for ( usize f = 0u; f < r.mesh.faces.nCount; ++f ) { fresh( &r.mesh.faces.pData[f].sourceId ); }
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryBuild( &r.mesh, pAllocator, pOut ); }
    if ( st == geometry_status_t::OK ) { *pIdAllocator = ids; }
    ModifierResult_Shutdown( &r );
    return st;
}

} // namespace cypher::editor::geometry
