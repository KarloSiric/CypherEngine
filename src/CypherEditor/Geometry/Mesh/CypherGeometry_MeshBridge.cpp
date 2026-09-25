//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBridge.cpp
//  Purpose: Implements face and edge-chain bridging.
//  Details: Both bridges reduce to the same shape: two rails of corners
//           (paired index by index) and cSegments rings of quads between
//           them. Rail A's edges are laid down in the direction that
//           stitches to what remains next to them (face A's own direction
//           for a face bridge, since the tube takes A's place; the border's
//           reverse for a chain bridge), and the quad windings follow.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBridge.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshRecordAccess.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace mesh_detail;
using math::vec3d_t;

namespace
{

mesh_bridge_result_t Failed( geometry_status_t s ) noexcept
{
    mesh_bridge_result_t r{};
    r.status = s;
    return r;
}

vec3d_t PositionOf( const editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t v ) noexcept
{
    return GenerationPool_Get( &pMesh->vertices, v )->position;
}

// Rails and the grid of corners between them. Ring s, column i is the
// corner at rail parameter s / cSegments: rail A (s = 0), rail B
// (s = cSegments), or a new vertex in between.
struct grid_t {
    vector_t<geometry_mesh_vertex_handle_t> railA{};
    vector_t<geometry_mesh_vertex_handle_t> railB{};
    vector_t<vec3d_t> newPositions{};
    u32 cColumns{ 0u };
    u32 cSegments{ 1u };
};

mesh_boundary_corner_t CornerAt( const grid_t &g, u32 s, u32 i ) noexcept
{
    mesh_boundary_corner_t c{};
    if ( s == 0u ) {
        c.hVertex = g.railA.pData[i];
    } else if ( s == g.cSegments ) {
        c.hVertex = g.railB.pData[i];
    } else {
        c.iNew = ( s - 1u ) * g.cColumns + i;
    }
    return c;
}

bool FillNewPositions( const editable_mesh_t *pMesh, grid_t *pG ) noexcept
{
    if ( !Vector_Reserve( &pG->newPositions, static_cast<usize>( pG->cSegments - 1u ) * pG->cColumns ) ) { return false; }
    for ( u32 s = 1u; s < pG->cSegments; ++s ) {
        const f64 t = static_cast<f64>( s ) / static_cast<f64>( pG->cSegments );
        for ( u32 i = 0u; i < pG->cColumns; ++i ) {
            const vec3d_t a = PositionOf( pMesh, pG->railA.pData[i] ), b = PositionOf( pMesh, pG->railB.pData[i] );
            (void)Vector_PushBack( &pG->newPositions, math::Vec3d_Add( a, math::Vec3d_Scale( math::Vec3d_Subtract( b, a ), t ) ) );
        }
    }
    return true;
}

// Builds the quads (with a source per column edge), reserves the outputs,
// and applies the whole bridge as one ReplaceFaces call. bClosed: the rails
// are loops (face bridge, quad edge i -> i + 1 mod n, laid in rail order);
// otherwise open chains laid in reverse (edge i + 1 -> i).
mesh_bridge_result_t Apply(
    editable_mesh_t *pMesh,
    const grid_t &g,
    bool bClosed,
    span_t<const geometry_mesh_face_handle_t> remove,
    const vector_t<geometry_mesh_face_handle_t> &columnSources,
    vector_t<mesh_bridge_face_t> *pFacesOut ) noexcept
{
    const allocator_t *pA = pMesh->pAllocator;
    const u32 cQuadColumns = bClosed ? g.cColumns : g.cColumns - 1u;
    const usize cQuads = static_cast<usize>( cQuadColumns ) * g.cSegments;
    vector_t<mesh_boundary_corner_t> corners{};
    vector_t<u32> sizes{};
    vector_t<geometry_mesh_face_handle_t> created{};
    if ( !Vector_Init( &corners, pA ) || !Vector_Init( &sizes, pA ) || !Vector_Init( &created, pA ) ||
         !Vector_Reserve( &corners, 4u * cQuads ) || !Vector_Reserve( &sizes, cQuads ) || !Vector_Reserve( &created, cQuads ) ||
         ( pFacesOut != nullptr && !Vector_Reserve( pFacesOut, pFacesOut->nCount + cQuads ) ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( u32 s = 0u; s < g.cSegments; ++s ) {
        for ( u32 i = 0u; i < cQuadColumns; ++i ) {
            const u32 j = bClosed ? ( i + 1u ) % g.cColumns : i + 1u;
            // Closed: bottom edge i -> j (face A's direction). Open: bottom
            // edge j -> i (against the border it stitches to).
            const u32 first = bClosed ? i : j, second = bClosed ? j : i;
            (void)Vector_PushBack( &corners, CornerAt( g, s, first ) );
            (void)Vector_PushBack( &corners, CornerAt( g, s, second ) );
            (void)Vector_PushBack( &corners, CornerAt( g, s + 1u, second ) );
            (void)Vector_PushBack( &corners, CornerAt( g, s + 1u, first ) );
            (void)Vector_PushBack( &sizes, 4u );
        }
    }
    const mesh_boundary_replace_result_t rr = MeshBoundary_ReplaceFaces(
        pMesh, remove, span_t<const mesh_boundary_corner_t>{ corners.pData, corners.nCount },
        span_t<const u32>{ sizes.pData, sizes.nCount }, span_t<const vec3d_t>{ g.newPositions.pData, g.newPositions.nCount }, &created );
    if ( rr.status != geometry_status_t::OK ) { return Failed( rr.status ); }
    if ( pFacesOut != nullptr ) {
        for ( usize q = 0u; q < created.nCount; ++q ) {
            (void)Vector_PushBack( pFacesOut, mesh_bridge_face_t{ created.pData[q], columnSources.pData[q % cQuadColumns] } );
        }
    }
    mesh_bridge_result_t r{};
    r.cFacesRemoved = rr.cFacesRemoved;
    r.cFacesCreated = rr.cFacesCreated;
    r.cVerticesCreated = rr.cVerticesCreated;
    r.status = geometry_status_t::OK;
    return r;
}

// The loop of a face as vertices and the half-edges leaving them.
bool LoopOf( const editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace, vector_t<geometry_mesh_vertex_handle_t> *pVerts,
             vector_t<geometry_mesh_half_edge_handle_t> *pEdges ) noexcept
{
    const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, hFace );
    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        if ( !Vector_PushBack( pVerts, pH->hOrigin ) || ( pEdges != nullptr && !Vector_PushBack( pEdges, h ) ) ) { return false; }
        h = pH->hNext;
    }
    return true;
}

} // namespace

mesh_bridge_result_t MeshBridge_Faces(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFaceA,
    geometry_mesh_face_handle_t hFaceB,
    u32 cSegments,
    vector_t<mesh_bridge_face_t> *pFacesOut ) noexcept
{
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return Failed( geometry_status_t::NOT_INITIALIZED ); }
    if ( cSegments == 0u || cSegments > kMeshBridgeSegmentsMax || ( pFacesOut != nullptr && pFacesOut->pAllocator == nullptr ) ) {
        return Failed( geometry_status_t::INVALID_ARGUMENT );
    }
    if ( !GenerationPool_Contains( &pMesh->faces, hFaceA ) || !GenerationPool_Contains( &pMesh->faces, hFaceB ) ) {
        return Failed( geometry_status_t::INVALID_HANDLE );
    }
    if ( Same( hFaceA, hFaceB ) ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }
    const allocator_t *pA = pMesh->pAllocator;
    grid_t g{};
    vector_t<geometry_mesh_vertex_handle_t> loopB{};
    vector_t<geometry_mesh_half_edge_handle_t> edgesA{};
    vector_t<geometry_mesh_face_handle_t> sources{};
    if ( !Vector_Init( &g.railA, pA ) || !Vector_Init( &g.railB, pA ) || !Vector_Init( &g.newPositions, pA ) ||
         !Vector_Init( &loopB, pA ) || !Vector_Init( &edgesA, pA ) || !Vector_Init( &sources, pA ) ||
         !LoopOf( pMesh, hFaceA, &g.railA, &edgesA ) || !LoopOf( pMesh, hFaceB, &loopB, nullptr ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    const u32 n = static_cast<u32>( g.railA.nCount );
    if ( loopB.nCount != n ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }
    for ( u32 i = 0u; i < n; ++i ) {
        for ( u32 j = 0u; j < n; ++j ) {
            if ( Same( g.railA.pData[i], loopB.pData[j] ) ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }
        }
    }
    // A forward pairs with B backward; pick the rotation with the least
    // summed squared distance (the untwisted tube).
    u32 kBest = 0u;
    f64 best = 0.0;
    for ( u32 k = 0u; k < n; ++k ) {
        f64 sum = 0.0;
        for ( u32 i = 0u; i < n; ++i ) {
            const vec3d_t d = math::Vec3d_Subtract( PositionOf( pMesh, g.railA.pData[i] ), PositionOf( pMesh, loopB.pData[( k + n - i ) % n] ) );
            sum += math::Vec3d_LengthSquared( d );
        }
        if ( k == 0u || sum < best ) {
            best = sum;
            kBest = k;
        }
    }
    if ( !std::isfinite( best ) ) { return Failed( geometry_status_t::NUMERIC_FAILURE ); }
    if ( !Vector_Resize( &g.railB, n ) || !Vector_Resize( &sources, n ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
    for ( u32 i = 0u; i < n; ++i ) {
        g.railB.pData[i] = loopB.pData[( kBest + n - i ) % n];
        // The wall the tube continues: the face across A's edge i.
        const mesh_half_edge_record_t *pH = He( pMesh, edgesA.pData[i] );
        const geometry_mesh_face_handle_t hAcross = IsBoundaryHe( pMesh, *pH ) ? hFaceA : FaceOf( pMesh, pH->hTwin );
        sources.pData[i] = GeometryHandle_IsValid( hAcross ) ? hAcross : hFaceA;
    }
    g.cColumns = n;
    g.cSegments = cSegments;
    if ( !FillNewPositions( pMesh, &g ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
    const geometry_mesh_face_handle_t remove[] = { hFaceA, hFaceB };
    return Apply( pMesh, g, true, span_t<const geometry_mesh_face_handle_t>{ remove, 2u }, sources, pFacesOut );
}

namespace
{

// Ordered vertices of an open boundary chain in the border's direction, and
// the face owning each edge. INVALID_ARGUMENT for a non-boundary edge, a
// broken or self-touching chain, or a closed loop.
geometry_status_t ChainOf(
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_edge_handle_t> chain,
    vector_t<geometry_mesh_vertex_handle_t> *pVerts,
    vector_t<geometry_mesh_face_handle_t> *pFaces ) noexcept
{
    if ( chain.pData == nullptr || chain.nCount == 0u ) { return geometry_status_t::INVALID_ARGUMENT; }
    const usize m = chain.nCount;
    // An open edge's record points at its only half-edge.
    auto boundaryHandle = [&]( usize j ) noexcept {
        const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, chain.pData[j] );
        return pE ? pE->hHalfEdge : GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
    };
    auto boundaryHe = [&]( usize j ) noexcept { return He( pMesh, boundaryHandle( j ) ); };
    for ( usize j = 0u; j < m; ++j ) {
        const mesh_half_edge_record_t *pH = boundaryHe( j );
        if ( pH == nullptr ) { return geometry_status_t::INVALID_HANDLE; }
        if ( !IsBoundaryHe( pMesh, *pH ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    }
    // Given in the border's direction, or reversed?
    bool bReversed = false;
    if ( m > 1u ) {
        const mesh_half_edge_record_t *p0 = boundaryHe( 0u ), *p1 = boundaryHe( 1u );
        if ( Same( DestOf( pMesh, *p0 ), p1->hOrigin ) ) {
            bReversed = false;
        } else if ( Same( p0->hOrigin, DestOf( pMesh, *p1 ) ) ) {
            bReversed = true;
        } else {
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    for ( usize k = 0u; k < m; ++k ) {
        const usize j = bReversed ? m - 1u - k : k;
        const mesh_half_edge_record_t *pH = boundaryHe( j );
        if ( k == 0u && !Vector_PushBack( pVerts, pH->hOrigin ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        if ( k > 0u && !Same( pVerts->pData[pVerts->nCount - 1u], pH->hOrigin ) ) { return geometry_status_t::INVALID_ARGUMENT; }
        if ( !Vector_PushBack( pVerts, DestOf( pMesh, *pH ) ) || !Vector_PushBack( pFaces, FaceOf( pMesh, boundaryHandle( j ) ) ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    // No vertex twice (a loop, or a chain touching itself).
    for ( usize i = 0u; i < pVerts->nCount; ++i ) {
        for ( usize j = i + 1u; j < pVerts->nCount; ++j ) {
            if ( Same( pVerts->pData[i], pVerts->pData[j] ) ) { return geometry_status_t::INVALID_ARGUMENT; }
        }
    }
    return geometry_status_t::OK;
}

} // namespace

mesh_bridge_result_t MeshBridge_EdgeChains(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_edge_handle_t> chainA,
    span_t<const geometry_mesh_edge_handle_t> chainB,
    u32 cSegments,
    vector_t<mesh_bridge_face_t> *pFacesOut ) noexcept
{
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return Failed( geometry_status_t::NOT_INITIALIZED ); }
    if ( cSegments == 0u || cSegments > kMeshBridgeSegmentsMax || chainA.nCount != chainB.nCount ||
         ( pFacesOut != nullptr && pFacesOut->pAllocator == nullptr ) ) {
        return Failed( geometry_status_t::INVALID_ARGUMENT );
    }
    const allocator_t *pA = pMesh->pAllocator;
    grid_t g{};
    vector_t<geometry_mesh_vertex_handle_t> vertsB{};
    vector_t<geometry_mesh_face_handle_t> facesA{}, facesB{};
    if ( !Vector_Init( &g.railA, pA ) || !Vector_Init( &g.railB, pA ) || !Vector_Init( &g.newPositions, pA ) ||
         !Vector_Init( &vertsB, pA ) || !Vector_Init( &facesA, pA ) || !Vector_Init( &facesB, pA ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    geometry_status_t s = ChainOf( pMesh, chainA, &g.railA, &facesA );
    if ( s == geometry_status_t::OK ) { s = ChainOf( pMesh, chainB, &vertsB, &facesB ); }
    if ( s != geometry_status_t::OK ) { return Failed( s ); }
    const u32 cVerts = static_cast<u32>( g.railA.nCount );
    for ( u32 i = 0u; i < cVerts; ++i ) {
        for ( u32 j = 0u; j < cVerts; ++j ) {
            if ( Same( g.railA.pData[i], vertsB.pData[j] ) ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }
        }
    }
    // A forward pairs with B backward.
    if ( !Vector_Resize( &g.railB, cVerts ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
    for ( u32 i = 0u; i < cVerts; ++i ) { g.railB.pData[i] = vertsB.pData[cVerts - 1u - i]; }
    g.cColumns = cVerts;
    g.cSegments = cSegments;
    if ( !FillNewPositions( pMesh, &g ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
    return Apply( pMesh, g, false, {}, facesA, pFacesOut );
}

} // namespace cypher::editor::geometry
