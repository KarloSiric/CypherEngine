//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SubdivisionSurface.cpp
//  Purpose: Implements retained subdivision.
//  Details: Works on flat indexed polygons (the mesh description) rather
//           than the half-edge mesh: each level is rebuilt wholesale, so
//           there is nothing to gain from incremental topology, and flat
//           arrays make every level a few linear passes plus one sort of
//           the edge table.
//
//           Level layout: the new vertices are [vertex points | edge points
//           | face points], so a parent vertex, edge or face index maps to
//           its child point by an offset and no lookup table is needed.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SubdivisionSurface.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct edge_rec_t {
    u32 a{ 0u }, b{ 0u }; // a < b
    f64 crease{ 0.0 };
    mesh_edge_attributes_t attributes{};
    subdivision_origin_t origin{}; // EDGE (on a source edge) or FACE (inside a source face)
};

u64 EdgeKey( u32 a, u32 b ) noexcept
{
    const u32 lo = a < b ? a : b, hi = a < b ? b : a;
    return ( static_cast<u64>( lo ) << 32u ) | hi;
}

struct level_t {
    vector_t<math::vec3d_t> pos{};
    vector_t<subdivision_origin_t> origin{};
    vector_t<geometry_source_id_t> ids{};
    vector_t<u32> corners{};
    vector_t<mesh_corner_attributes_t> cornerAttr{};
    vector_t<u32> faceStart{}; // cFaces + 1
    vector_t<u32> faceSource{};
    vector_t<mesh_face_attributes_t> faceAttr{};
    vector_t<edge_rec_t> edges{}; // sorted by EdgeKey

    bool Init( const allocator_t *pA ) noexcept
    {
        return Vector_Init( &pos, pA ) && Vector_Init( &origin, pA ) && Vector_Init( &ids, pA ) && Vector_Init( &corners, pA ) &&
               Vector_Init( &cornerAttr, pA ) && Vector_Init( &faceStart, pA ) && Vector_Init( &faceSource, pA ) &&
               Vector_Init( &faceAttr, pA ) && Vector_Init( &edges, pA );
    }
    usize FaceCount() const noexcept { return faceStart.nCount > 0u ? faceStart.nCount - 1u : 0u; }
};

// Exchanges two levels' storage (vector_t has no swap; its fields are
// public, and a field-wise exchange moves ownership without allocating).
void Swap( level_t &x, level_t &y ) noexcept
{
    auto sw = []( auto &p, auto &q ) noexcept {
        std::swap( p.pData, q.pData );
        std::swap( p.nCount, q.nCount );
        std::swap( p.nCapacity, q.nCapacity );
        std::swap( p.pAllocator, q.pAllocator );
    };
    sw( x.pos, y.pos );
    sw( x.origin, y.origin );
    sw( x.ids, y.ids );
    sw( x.corners, y.corners );
    sw( x.cornerAttr, y.cornerAttr );
    sw( x.faceStart, y.faceStart );
    sw( x.faceSource, y.faceSource );
    sw( x.faceAttr, y.faceAttr );
    sw( x.edges, y.edges );
}

usize FindEdge( const level_t &L, u32 a, u32 b ) noexcept
{
    const u64 key = EdgeKey( a, b );
    usize lo = 0u, hi = L.edges.nCount;
    while ( lo < hi ) {
        const usize mid = ( lo + hi ) / 2u;
        const u64 k = EdgeKey( L.edges.pData[mid].a, L.edges.pData[mid].b );
        if ( k < key ) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo < L.edges.nCount && EdgeKey( L.edges.pData[lo].a, L.edges.pData[lo].b ) == key ? lo : CY_INVALID_SIZE;
}

bool SortEdges( level_t *pL ) noexcept
{
    std::sort( pL->edges.pData, pL->edges.pData + pL->edges.nCount,
               []( const edge_rec_t &x, const edge_rec_t &y ) { return EdgeKey( x.a, x.b ) < EdgeKey( y.a, y.b ); } );
    for ( usize i = 1u; i < pL->edges.nCount; ++i ) {
        if ( EdgeKey( pL->edges.pData[i - 1u].a, pL->edges.pData[i - 1u].b ) == EdgeKey( pL->edges.pData[i].a, pL->edges.pData[i].b ) ) {
            return false;
        }
    }
    return true;
}

u32 AverageColor( const u32 *pColors, u32 n ) noexcept
{
    u32 out = 0u;
    for ( u32 shift = 0u; shift < 32u; shift += 8u ) {
        u32 sum = 0u;
        for ( u32 i = 0u; i < n; ++i ) { sum += ( pColors[i] >> shift ) & 0xFFu; }
        out |= ( ( sum + n / 2u ) / n ) << shift;
    }
    return out;
}

mesh_corner_attributes_t Mix2( const mesh_corner_attributes_t &x, const mesh_corner_attributes_t &y ) noexcept
{
    mesh_corner_attributes_t m{};
    m.uv0 = math::vec2d_t{ 0.5 * ( x.uv0.x + y.uv0.x ), 0.5 * ( x.uv0.y + y.uv0.y ) };
    m.uv1 = math::vec2d_t{ 0.5 * ( x.uv1.x + y.uv1.x ), 0.5 * ( x.uv1.y + y.uv1.y ) };
    const u32 c[2] = { x.colorRgba, y.colorRgba };
    m.colorRgba = AverageColor( c, 2u );
    return m;
}

// Level 0: the cage, with every edge (not only the non-default ones the
// description lists) so later passes can look any edge up.
geometry_status_t LoadCage( const mesh_source_description_t &d, level_t *pL ) noexcept
{
    const usize cV = d.vertices.nCount, cF = d.faces.nCount, cC = d.corners.nCount;
    if ( !Vector_Resize( &pL->pos, cV ) || !Vector_Resize( &pL->origin, cV ) || !Vector_Resize( &pL->ids, cV ) ||
         !Vector_Resize( &pL->corners, cC ) || !Vector_Resize( &pL->cornerAttr, cC ) || !Vector_Resize( &pL->faceStart, cF + 1u ) ||
         !Vector_Resize( &pL->faceSource, cF ) || !Vector_Resize( &pL->faceAttr, cF ) || !Vector_Reserve( &pL->edges, cC ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    Vector_Clear( &pL->edges );
    for ( usize i = 0u; i < cV; ++i ) {
        pL->pos.pData[i] = d.vertices.pData[i].position;
        pL->ids.pData[i] = d.vertices.pData[i].sourceId;
        pL->origin.pData[i] = subdivision_origin_t{ subdivision_origin_kind_t::VERTEX, static_cast<u32>( i ), 0u };
    }
    u32 c = 0u;
    for ( usize f = 0u; f < cF; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        pL->faceStart.pData[f] = c;
        pL->faceSource.pData[f] = static_cast<u32>( f );
        pL->faceAttr.pData[f] = face.attributes;
        for ( u32 k = 0u; k < face.cCorners; ++k, ++c ) {
            const mesh_source_corner_t &corner = d.corners.pData[face.iFirstCorner + k];
            pL->corners.pData[c] = corner.iVertex;
            pL->cornerAttr.pData[c] = corner.attributes;
            const u32 next = d.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex;
            // Interior edges are met twice; duplicates are dropped after the sort.
            edge_rec_t e{};
            e.a = std::min( corner.iVertex, next );
            e.b = std::max( corner.iVertex, next );
            e.origin = subdivision_origin_t{ subdivision_origin_kind_t::EDGE, e.a, e.b };
            (void)Vector_PushBack( &pL->edges, e );
        }
    }
    pL->faceStart.pData[cF] = c;
    std::sort( pL->edges.pData, pL->edges.pData + pL->edges.nCount,
               []( const edge_rec_t &x, const edge_rec_t &y ) { return EdgeKey( x.a, x.b ) < EdgeKey( y.a, y.b ); } );
    usize w = 0u;
    for ( usize i = 0u; i < pL->edges.nCount; ++i ) {
        if ( w == 0u || EdgeKey( pL->edges.pData[w - 1u].a, pL->edges.pData[w - 1u].b ) != EdgeKey( pL->edges.pData[i].a, pL->edges.pData[i].b ) ) {
            pL->edges.pData[w++] = pL->edges.pData[i];
        }
    }
    pL->edges.nCount = w;
    for ( usize i = 0u; i < d.edges.nCount; ++i ) {
        const mesh_source_edge_t &se = d.edges.pData[i];
        const usize e = FindEdge( *pL, se.iVertexA, se.iVertexB );
        if ( e == CY_INVALID_SIZE ) { return geometry_status_t::CORRUPT_STATE; }
        pL->edges.pData[e].crease = se.creaseWeight;
        pL->edges.pData[e].attributes = se.attributes;
    }
    return geometry_status_t::OK;
}

// Scratch reused across levels.
struct scratch_t {
    vector_t<u32> cornerEdge{};
    vector_t<u32> edgeFaces{}; // two per edge (CY_U32_MAX when absent)
    vector_t<u32> edgeFaceCount{};
    vector_t<math::vec3d_t> facePoint{};
    vector_t<math::vec3d_t> edgePoint{};
    // Per vertex accumulators.
    vector_t<math::vec3d_t> sumFace{}, sumMid{}, sumSharpNeighbour{};
    vector_t<u32> cFaces{}, cEdges{}, cSharp{};
    vector_t<f64> sumSharpW{}, maxSharpW{};
    vector_t<u8> onBoundary{};

    bool Init( const allocator_t *pA ) noexcept
    {
        return Vector_Init( &cornerEdge, pA ) && Vector_Init( &edgeFaces, pA ) && Vector_Init( &edgeFaceCount, pA ) &&
               Vector_Init( &facePoint, pA ) && Vector_Init( &edgePoint, pA ) && Vector_Init( &sumFace, pA ) && Vector_Init( &sumMid, pA ) &&
               Vector_Init( &sumSharpNeighbour, pA ) && Vector_Init( &cFaces, pA ) && Vector_Init( &cEdges, pA ) && Vector_Init( &cSharp, pA ) &&
               Vector_Init( &sumSharpW, pA ) && Vector_Init( &maxSharpW, pA ) && Vector_Init( &onBoundary, pA );
    }
};

geometry_status_t Step( const level_t &L, const subdivision_descriptor_t &desc, scratch_t *pS, level_t *pN ) noexcept
{
    const usize cV = L.pos.nCount, cE = L.edges.nCount, cF = L.FaceCount(), cC = L.corners.nCount;
    const usize cV2 = cV + cE + cF;
    if ( cV2 > kSubdivisionVerticesMax || 4u * cC > kSubdivisionCornersMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    if ( !Vector_Resize( &pS->cornerEdge, cC ) || !Vector_Resize( &pS->edgeFaces, 2u * cE ) || !Vector_Resize( &pS->edgeFaceCount, cE ) ||
         !Vector_Resize( &pS->facePoint, cF ) || !Vector_Resize( &pS->edgePoint, cE ) || !Vector_Resize( &pS->sumFace, cV ) ||
         !Vector_Resize( &pS->sumMid, cV ) || !Vector_Resize( &pS->sumSharpNeighbour, cV ) || !Vector_Resize( &pS->cFaces, cV ) ||
         !Vector_Resize( &pS->cEdges, cV ) || !Vector_Resize( &pS->cSharp, cV ) || !Vector_Resize( &pS->sumSharpW, cV ) ||
         !Vector_Resize( &pS->maxSharpW, cV ) || !Vector_Resize( &pS->onBoundary, cV ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const bool bLinear = desc.scheme == subdivision_scheme_t::LINEAR;
    // Adjacency.
    for ( usize e = 0u; e < cE; ++e ) {
        pS->edgeFaceCount.pData[e] = 0u;
        pS->edgeFaces.pData[2u * e] = pS->edgeFaces.pData[2u * e + 1u] = CY_U32_MAX;
    }
    for ( usize f = 0u; f < cF; ++f ) {
        const u32 first = L.faceStart.pData[f], n = L.faceStart.pData[f + 1u] - first;
        math::vec3d_t sum{};
        for ( u32 k = 0u; k < n; ++k ) {
            const u32 v = L.corners.pData[first + k], w = L.corners.pData[first + ( k + 1u ) % n];
            const usize e = FindEdge( L, v, w );
            if ( e == CY_INVALID_SIZE ) { return geometry_status_t::CORRUPT_STATE; }
            pS->cornerEdge.pData[first + k] = static_cast<u32>( e );
            u32 &count = pS->edgeFaceCount.pData[e];
            if ( count >= 2u ) { return geometry_status_t::NON_MANIFOLD; }
            pS->edgeFaces.pData[2u * e + count++] = static_cast<u32>( f );
            sum = math::Vec3d_Add( sum, L.pos.pData[v] );
        }
        pS->facePoint.pData[f] = math::Vec3d_Scale( sum, 1.0 / n );
    }
    auto sharpness = [&]( usize e ) noexcept -> f64 {
        const edge_rec_t &r = L.edges.pData[e];
        if ( pS->edgeFaceCount.pData[e] < 2u ) { return 1.0; } // boundary
        const f64 hard = desc.bHardEdgesAreCreases && ( r.attributes.flags & MESH_EDGE_FLAG_HARD ) != 0u ? 1.0 : 0.0;
        return std::fmax( std::fmin( std::fmax( r.crease, 0.0 ), 1.0 ), hard );
    };
    // Edge points and per-vertex sums.
    for ( usize v = 0u; v < cV; ++v ) {
        pS->sumFace.pData[v] = pS->sumMid.pData[v] = pS->sumSharpNeighbour.pData[v] = math::vec3d_t{};
        pS->cFaces.pData[v] = pS->cEdges.pData[v] = pS->cSharp.pData[v] = 0u;
        pS->sumSharpW.pData[v] = pS->maxSharpW.pData[v] = 0.0;
        pS->onBoundary.pData[v] = 0u;
    }
    for ( usize e = 0u; e < cE; ++e ) {
        const edge_rec_t &r = L.edges.pData[e];
        const math::vec3d_t pa = L.pos.pData[r.a], pb = L.pos.pData[r.b];
        const math::vec3d_t mid = math::Vec3d_Scale( math::Vec3d_Add( pa, pb ), 0.5 );
        const f64 w = sharpness( e );
        math::vec3d_t ep = mid;
        if ( !bLinear && pS->edgeFaceCount.pData[e] == 2u ) {
            const math::vec3d_t smooth = math::Vec3d_Scale(
                math::Vec3d_Add( math::Vec3d_Add( pa, pb ), math::Vec3d_Add( pS->facePoint.pData[pS->edgeFaces.pData[2u * e]],
                                                                             pS->facePoint.pData[pS->edgeFaces.pData[2u * e + 1u]] ) ),
                0.25 );
            ep = math::Vec3d_Lerp( smooth, mid, w );
        }
        pS->edgePoint.pData[e] = ep;
        for ( const u32 v : { r.a, r.b } ) {
            pS->sumMid.pData[v] = math::Vec3d_Add( pS->sumMid.pData[v], mid );
            pS->cEdges.pData[v] += 1u;
            if ( w > 0.0 ) {
                pS->cSharp.pData[v] += 1u;
                pS->sumSharpW.pData[v] += w;
                pS->maxSharpW.pData[v] = std::fmax( pS->maxSharpW.pData[v], w );
                pS->sumSharpNeighbour.pData[v] = math::Vec3d_Add( pS->sumSharpNeighbour.pData[v], L.pos.pData[v == r.a ? r.b : r.a] );
            }
            if ( pS->edgeFaceCount.pData[e] < 2u ) { pS->onBoundary.pData[v] = 1u; }
        }
    }
    for ( usize f = 0u; f < cF; ++f ) {
        for ( u32 c = L.faceStart.pData[f]; c < L.faceStart.pData[f + 1u]; ++c ) {
            const u32 v = L.corners.pData[c];
            pS->sumFace.pData[v] = math::Vec3d_Add( pS->sumFace.pData[v], pS->facePoint.pData[f] );
            pS->cFaces.pData[v] += 1u;
        }
    }

    // New level arrays.
    const usize cF2 = cC, cC2 = 4u * cC, cE2 = 2u * cE + cC;
    if ( !Vector_Resize( &pN->pos, cV2 ) || !Vector_Resize( &pN->origin, cV2 ) || !Vector_Resize( &pN->ids, cV2 ) ||
         !Vector_Resize( &pN->corners, cC2 ) || !Vector_Resize( &pN->cornerAttr, cC2 ) || !Vector_Resize( &pN->faceStart, cF2 + 1u ) ||
         !Vector_Resize( &pN->faceSource, cF2 ) || !Vector_Resize( &pN->faceAttr, cF2 ) || !Vector_Resize( &pN->edges, cE2 ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // Vertex points.
    for ( usize v = 0u; v < cV; ++v ) {
        const math::vec3d_t P = L.pos.pData[v];
        math::vec3d_t out = P;
        if ( !bLinear ) {
            const u32 n = pS->cEdges.pData[v], nSharp = pS->cSharp.pData[v];
            const bool bBoundary = pS->onBoundary.pData[v] != 0u;
            math::vec3d_t smooth = P;
            if ( !bBoundary && n > 0u && pS->cFaces.pData[v] > 0u ) {
                const math::vec3d_t F = math::Vec3d_Scale( pS->sumFace.pData[v], 1.0 / pS->cFaces.pData[v] );
                const math::vec3d_t R = math::Vec3d_Scale( pS->sumMid.pData[v], 1.0 / n );
                smooth = math::Vec3d_Scale( math::Vec3d_Add( math::Vec3d_Add( F, math::Vec3d_Scale( R, 2.0 ) ), math::Vec3d_Scale( P, n - 3.0 ) ),
                                            1.0 / n );
            }
            if ( bBoundary && n == 2u && desc.boundary == subdivision_boundary_t::PIN_CORNERS ) {
                out = P;
            } else if ( nSharp < 2u ) {
                out = smooth;
            } else if ( nSharp == 2u ) {
                const math::vec3d_t crease =
                    math::Vec3d_Scale( math::Vec3d_Add( pS->sumSharpNeighbour.pData[v], math::Vec3d_Scale( P, 6.0 ) ), 1.0 / 8.0 );
                out = bBoundary ? crease : math::Vec3d_Lerp( smooth, crease, 0.5 * pS->sumSharpW.pData[v] );
            } else {
                out = bBoundary ? P : math::Vec3d_Lerp( smooth, P, pS->maxSharpW.pData[v] );
            }
        }
        pN->pos.pData[v] = out;
        pN->origin.pData[v] = L.origin.pData[v];
        pN->ids.pData[v] = L.ids.pData[v];
    }
    for ( usize e = 0u; e < cE; ++e ) {
        pN->pos.pData[cV + e] = pS->edgePoint.pData[e];
        pN->origin.pData[cV + e] = L.edges.pData[e].origin;
        pN->ids.pData[cV + e] = GEOMETRY_SOURCE_ID_INVALID;
    }
    for ( usize f = 0u; f < cF; ++f ) {
        pN->pos.pData[cV + cE + f] = pS->facePoint.pData[f];
        pN->origin.pData[cV + cE + f] = subdivision_origin_t{ subdivision_origin_kind_t::FACE, L.faceSource.pData[f], 0u };
        pN->ids.pData[cV + cE + f] = GEOMETRY_SOURCE_ID_INVALID;
    }
    // Faces: one quad per parent corner, and the child edges.
    usize iEdge = 0u;
    for ( usize e = 0u; e < cE; ++e ) {
        const edge_rec_t &r = L.edges.pData[e];
        for ( const u32 end : { r.a, r.b } ) {
            edge_rec_t c = r;
            c.a = std::min( end, static_cast<u32>( cV + e ) );
            c.b = std::max( end, static_cast<u32>( cV + e ) );
            pN->edges.pData[iEdge++] = c;
        }
    }
    u32 c2 = 0u;
    for ( usize f = 0u; f < cF; ++f ) {
        const u32 first = L.faceStart.pData[f], n = L.faceStart.pData[f + 1u] - first;
        const u32 fp = static_cast<u32>( cV + cE + f );
        // Face-point surface data: the mean of the face's corners.
        mesh_corner_attributes_t centre{};
        u32 colors[kMeshSourceCornersPerFaceMax];
        const u32 cColors = std::min( n, kMeshSourceCornersPerFaceMax );
        for ( u32 k = 0u; k < n; ++k ) {
            const mesh_corner_attributes_t &a = L.cornerAttr.pData[first + k];
            centre.uv0 = math::vec2d_t{ centre.uv0.x + a.uv0.x / n, centre.uv0.y + a.uv0.y / n };
            centre.uv1 = math::vec2d_t{ centre.uv1.x + a.uv1.x / n, centre.uv1.y + a.uv1.y / n };
            if ( k < cColors ) { colors[k] = a.colorRgba; }
        }
        centre.colorRgba = AverageColor( colors, cColors );
        for ( u32 k = 0u; k < n; ++k ) {
            const u32 kPrev = ( k + n - 1u ) % n;
            const u32 v = L.corners.pData[first + k];
            const u32 eNext = pS->cornerEdge.pData[first + k], ePrev = pS->cornerEdge.pData[first + kPrev];
            const mesh_corner_attributes_t &a = L.cornerAttr.pData[first + k];
            const mesh_corner_attributes_t &aNext = L.cornerAttr.pData[first + ( k + 1u ) % n];
            const mesh_corner_attributes_t &aPrev = L.cornerAttr.pData[first + kPrev];
            const u32 f2 = static_cast<u32>( first + k );
            pN->faceStart.pData[f2] = c2;
            pN->faceSource.pData[f2] = L.faceSource.pData[f];
            pN->faceAttr.pData[f2] = L.faceAttr.pData[f];
            pN->corners.pData[c2] = v;
            pN->cornerAttr.pData[c2++] = a;
            pN->corners.pData[c2] = static_cast<u32>( cV + eNext );
            pN->cornerAttr.pData[c2++] = Mix2( a, aNext );
            pN->corners.pData[c2] = fp;
            pN->cornerAttr.pData[c2++] = centre;
            pN->corners.pData[c2] = static_cast<u32>( cV + ePrev );
            pN->cornerAttr.pData[c2++] = Mix2( aPrev, a );
            // The spoke from this corner's outgoing edge point to the face point.
            edge_rec_t spoke{};
            spoke.a = std::min( static_cast<u32>( cV + eNext ), fp );
            spoke.b = std::max( static_cast<u32>( cV + eNext ), fp );
            spoke.origin = subdivision_origin_t{ subdivision_origin_kind_t::FACE, L.faceSource.pData[f], 0u };
            pN->edges.pData[iEdge++] = spoke;
        }
    }
    pN->faceStart.pData[cF2] = c2;
    return SortEdges( pN ) ? geometry_status_t::OK : geometry_status_t::CORRUPT_STATE;
}

geometry_status_t WriteResult( const level_t &L, geometry_source_id_t rootId, subdivision_result_t *pR ) noexcept
{
    mesh_source_description_t &d = pR->mesh;
    const usize cV = L.pos.nCount, cF = L.FaceCount(), cC = L.corners.nCount;
    usize cSpecial = 0u;
    for ( usize e = 0u; e < L.edges.nCount; ++e ) { cSpecial += L.edges.pData[e].crease > 0.0 || L.edges.pData[e].attributes.flags != 0u ? 1u : 0u; }
    if ( !Vector_Resize( &d.vertices, cV ) || !Vector_Resize( &d.corners, cC ) || !Vector_Resize( &d.faces, cF ) ||
         !Vector_Resize( &d.edges, cSpecial ) || !Vector_Resize( &pR->vertexOrigins, cV ) || !Vector_Resize( &pR->faceSources, cF ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    d.sourceId = rootId;
    for ( usize v = 0u; v < cV; ++v ) {
        d.vertices.pData[v].position = L.pos.pData[v];
        d.vertices.pData[v].sourceId = L.ids.pData[v];
        pR->vertexOrigins.pData[v] = L.origin.pData[v];
    }
    for ( usize c = 0u; c < cC; ++c ) {
        d.corners.pData[c].iVertex = L.corners.pData[c];
        d.corners.pData[c].attributes = L.cornerAttr.pData[c];
    }
    for ( usize f = 0u; f < cF; ++f ) {
        mesh_source_face_t &face = d.faces.pData[f];
        face.iFirstCorner = L.faceStart.pData[f];
        face.cCorners = L.faceStart.pData[f + 1u] - L.faceStart.pData[f];
        face.sourceId = GEOMETRY_SOURCE_ID_INVALID;
        face.attributes = L.faceAttr.pData[f];
        pR->faceSources.pData[f] = L.faceSource.pData[f];
    }
    usize w = 0u;
    for ( usize e = 0u; e < L.edges.nCount; ++e ) {
        const edge_rec_t &r = L.edges.pData[e];
        if ( r.crease > 0.0 || r.attributes.flags != 0u ) {
            d.edges.pData[w++] = mesh_source_edge_t{ r.a, r.b, r.attributes, r.crease };
        }
    }
    return geometry_status_t::OK;
}

void ClearResult( subdivision_result_t *pR ) noexcept
{
    MeshSourceDescription_Clear( &pR->mesh, GEOMETRY_SOURCE_ID_INVALID );
    Vector_Clear( &pR->vertexOrigins );
    Vector_Clear( &pR->faceSources );
}

} // namespace

geometry_status_t SubdivisionResult_Init( subdivision_result_t *pResult, const allocator_t *pAllocator ) noexcept
{
    if ( pResult == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pResult->vertexOrigins.pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    geometry_status_t st = MeshSourceDescription_Init( &pResult->mesh, pAllocator, GEOMETRY_SOURCE_ID_INVALID );
    if ( st == geometry_status_t::OK && ( !Vector_Init( &pResult->vertexOrigins, pAllocator ) || !Vector_Init( &pResult->faceSources, pAllocator ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) { SubdivisionResult_Shutdown( pResult ); }
    return st;
}

void SubdivisionResult_Shutdown( subdivision_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) { return; }
    MeshSourceDescription_Shutdown( &pResult->mesh );
    Vector_Shutdown( &pResult->vertexOrigins );
    Vector_Shutdown( &pResult->faceSources );
}

geometry_status_t Subdivision_ValidateDescriptor( const subdivision_descriptor_t &d ) noexcept
{
    const bool bScheme = d.scheme == subdivision_scheme_t::CATMULL_CLARK || d.scheme == subdivision_scheme_t::LINEAR;
    const bool bBoundary = d.boundary == subdivision_boundary_t::SMOOTH || d.boundary == subdivision_boundary_t::PIN_CORNERS;
    return bScheme && bBoundary && d.cLevels <= kSubdivisionLevelsMax ? geometry_status_t::OK : geometry_status_t::INVALID_ARGUMENT;
}

geometry_status_t Subdivision_TryPredictCounts( const mesh_source_t *pSource, const subdivision_descriptor_t &descriptor, usize *pcVerticesOut,
                                                usize *pcFacesOut ) noexcept
{
    if ( pSource == nullptr || pcVerticesOut == nullptr || pcFacesOut == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_status_t st = Subdivision_ValidateDescriptor( descriptor );
    if ( st != geometry_status_t::OK ) { return st; }
    // V' = V + E + F, F' = C, E' = 2E + C, C' = 4C for every polygon mesh.
    usize V = EditableMesh_VertexCount( &pSource->mesh ), E = EditableMesh_EdgeCount( &pSource->mesh ), F = EditableMesh_FaceCount( &pSource->mesh );
    usize C = EditableMesh_HalfEdgeCount( &pSource->mesh ); // one half-edge per corner
    for ( u32 i = 0u; i < descriptor.cLevels; ++i ) {
        const usize V2 = V + E + F, F2 = C, E2 = 2u * E + C, C2 = 4u * C;
        if ( V2 > kSubdivisionVerticesMax || C2 > kSubdivisionCornersMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
        V = V2;
        F = F2;
        E = E2;
        C = C2;
    }
    *pcVerticesOut = V;
    *pcFacesOut = F;
    return geometry_status_t::OK;
}

namespace
{

geometry_status_t EvaluateCage( const mesh_source_description_t &cage, const subdivision_descriptor_t &descriptor, subdivision_result_t *pResult ) noexcept
{
    const allocator_t *pA = pResult->vertexOrigins.pAllocator;
    ClearResult( pResult );
    level_t L{}, N{};
    scratch_t S{};
    geometry_status_t st = L.Init( pA ) && N.Init( pA ) && S.Init( pA ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
    if ( st == geometry_status_t::OK ) { st = LoadCage( cage, &L ); }
    for ( u32 i = 0u; st == geometry_status_t::OK && i < descriptor.cLevels; ++i ) {
        st = Step( L, descriptor, &S, &N );
        if ( st == geometry_status_t::OK ) { Swap( L, N ); }
    }
    if ( st == geometry_status_t::OK ) { st = WriteResult( L, cage.sourceId, pResult ); }
    if ( st != geometry_status_t::OK ) { ClearResult( pResult ); }
    return st;
}

} // namespace

geometry_status_t Subdivision_TryEvaluate( const mesh_source_t *pSource, const subdivision_descriptor_t &descriptor, subdivision_result_t *pResult ) noexcept
{
    if ( pSource == nullptr || pResult == nullptr || pResult->vertexOrigins.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_status_t st = Subdivision_ValidateDescriptor( descriptor );
    if ( st != geometry_status_t::OK ) { return st; }
    usize cV = 0u, cF = 0u;
    st = Subdivision_TryPredictCounts( pSource, descriptor, &cV, &cF );
    if ( st != geometry_status_t::OK ) {
        ClearResult( pResult );
        return st;
    }
    mesh_source_description_t cage{};
    st = MeshSourceDescription_Init( &cage, pResult->vertexOrigins.pAllocator, GEOMETRY_SOURCE_ID_INVALID );
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( pSource, &cage ); }
    if ( st == geometry_status_t::OK ) {
        st = EvaluateCage( cage, descriptor, pResult );
    } else {
        ClearResult( pResult );
    }
    MeshSourceDescription_Shutdown( &cage );
    return st;
}

geometry_status_t Subdivision_TryEvaluateDescription( const mesh_source_description_t *pCage, const subdivision_descriptor_t &descriptor,
                                                      subdivision_result_t *pResult ) noexcept
{
    if ( pCage == nullptr || pResult == nullptr || pResult->vertexOrigins.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const geometry_status_t st = Subdivision_ValidateDescriptor( descriptor );
    if ( st != geometry_status_t::OK ) { return st; }
    for ( usize c = 0u; c < pCage->corners.nCount; ++c ) {
        if ( pCage->corners.pData[c].iVertex >= pCage->vertices.nCount ) { return geometry_status_t::INVALID_ARGUMENT; }
    }
    return EvaluateCage( *pCage, descriptor, pResult );
}

geometry_status_t Subdivision_TryCollapse( const mesh_source_t *pSource, const subdivision_descriptor_t &descriptor, const allocator_t *pAllocator,
                                           geometry_source_id_allocator_t *pIdAllocator, mesh_source_t *pOut ) noexcept
{
    if ( pSource == nullptr || pIdAllocator == nullptr || pOut == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    subdivision_result_t r{};
    geometry_status_t st = SubdivisionResult_Init( &r, pAllocator );
    if ( st == geometry_status_t::OK ) { st = Subdivision_TryEvaluate( pSource, descriptor, &r ); }
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
    SubdivisionResult_Shutdown( &r );
    return st;
}

} // namespace cypher::editor::geometry
