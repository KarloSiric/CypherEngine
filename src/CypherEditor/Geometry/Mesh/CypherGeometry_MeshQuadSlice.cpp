//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshQuadSlice.cpp
//  Purpose: Implements Quad Slice.
//  Details: Edge points are created once per sliced edge, ordered from the
//           edge record's own half-edge origin, and read back in either
//           direction; that single ordering is what lets the two faces of
//           an edge (two cells, or a cell and a rebuilt neighbour) share
//           the same vertices.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshQuadSlice.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshRecordAccess.h"
#include "CypherGeometry_MeshSource.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace mesh_detail;
using math::vec3d_t;

namespace
{

mesh_quad_slice_result_t Failed( geometry_status_t s ) noexcept
{
    mesh_quad_slice_result_t r{};
    r.status = s;
    return r;
}

struct quad_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_vertex_handle_t c[4]{};
    geometry_mesh_half_edge_handle_t h[4]{};
    u32 iFirstInterior{ 0u }; // into newPositions
};

} // namespace

mesh_quad_slice_result_t MeshQuadSlice_Faces(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    u32 cU,
    u32 cV,
    vector_t<mesh_quad_slice_face_t> *pFacesOut ) noexcept
{
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return Failed( geometry_status_t::NOT_INITIALIZED ); }
    if ( cU == 0u || cV == 0u || cU > kMeshQuadSliceCutsMax || cV > kMeshQuadSliceCutsMax || faces.pData == nullptr ||
         faces.nCount == 0u || ( pFacesOut != nullptr && pFacesOut->pAllocator == nullptr ) ) {
        return Failed( geometry_status_t::INVALID_ARGUMENT );
    }
    const allocator_t *pA = pMesh->pAllocator;
    vector_t<u8> selected{};
    if ( !Vector_Init( &selected, pA ) || !Vector_Resize( &selected, pMesh->faces.cSlots ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < selected.nCount; ++i ) { selected.pData[i] = 0u; }
    for ( usize i = 0u; i < faces.nCount; ++i ) {
        if ( !GenerationPool_Contains( &pMesh->faces, faces.pData[i] ) || selected.pData[faces.pData[i].nSlot] != 0u ) {
            return Failed( geometry_status_t::INVALID_HANDLE );
        }
        selected.pData[faces.pData[i].nSlot] = 1u;
    }
    if ( cU == 1u && cV == 1u ) {
        mesh_quad_slice_result_t r{};
        r.status = geometry_status_t::OK; // one cell each: nothing to cut
        return r;
    }

    // Quads, and how many pieces each of their edges gets.
    vector_t<quad_t> quads{};
    vector_t<u32> pieces{}, firstNew{};
    vector_t<vec3d_t> newPositions{};
    if ( !Vector_Init( &quads, pA ) || !Vector_Init( &pieces, pA ) || !Vector_Init( &firstNew, pA ) || !Vector_Init( &newPositions, pA ) ||
         !Vector_Reserve( &quads, faces.nCount ) || !Vector_Resize( &pieces, pMesh->edges.cSlots ) ||
         !Vector_Resize( &firstNew, pMesh->edges.cSlots ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < pieces.nCount; ++i ) { pieces.pData[i] = 0u; }
    for ( usize i = 0u; i < faces.nCount; ++i ) {
        const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, faces.pData[i] );
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
        if ( pL->cHalfEdges != 4u ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }
        quad_t q{};
        q.hFace = faces.pData[i];
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( u32 k = 0u; k < 4u; ++k ) {
            const mesh_half_edge_record_t *pH = He( pMesh, h );
            q.c[k] = pH->hOrigin;
            q.h[k] = h;
            // Edges 0 and 2 run along U, 1 and 3 along V.
            const u32 want = ( k % 2u ) == 0u ? cU : cV;
            u32 &have = pieces.pData[pH->hEdge.nSlot];
            if ( have != 0u && have != want ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }
            have = want;
            h = pH->hNext;
        }
        (void)Vector_PushBack( &quads, q );
    }

    // Edge points, ordered from each edge record's own half-edge origin.
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->edges, [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
        const u32 n = pieces.pData[hE.nSlot];
        if ( n <= 1u ) { return true; }
        const mesh_half_edge_record_t *pH = He( pMesh, e.hHalfEdge );
        const vec3d_t a = GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position;
        const vec3d_t b = GenerationPool_Get( &pMesh->vertices, DestOf( pMesh, *pH ) )->position;
        firstNew.pData[hE.nSlot] = static_cast<u32>( newPositions.nCount );
        for ( u32 k = 1u; k < n && bOk; ++k ) {
            const f64 t = static_cast<f64>( k ) / static_cast<f64>( n );
            bOk = Vector_PushBack( &newPositions, math::Vec3d_Add( a, math::Vec3d_Scale( math::Vec3d_Subtract( b, a ), t ) ) );
        }
        return bOk;
    } );
    if ( !bOk ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
    // Point k (1 .. n - 1) along half-edge h, counted from h's origin.
    auto along = [&]( geometry_mesh_half_edge_handle_t h, u32 k ) noexcept {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        const u32 slot = pH->hEdge.nSlot, n = pieces.pData[slot];
        const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, pH->hEdge );
        const bool bForward = Same( He( pMesh, pE->hHalfEdge )->hOrigin, pH->hOrigin );
        mesh_boundary_corner_t c{};
        c.iNew = firstNew.pData[slot] + ( bForward ? k - 1u : n - 1u - k );
        return c;
    };
    // Interior points (bilinear in the corners).
    for ( usize qi = 0u; qi < quads.nCount; ++qi ) {
        quad_t &q = quads.pData[qi];
        q.iFirstInterior = static_cast<u32>( newPositions.nCount );
        vec3d_t p[4];
        for ( u32 k = 0u; k < 4u; ++k ) { p[k] = GenerationPool_Get( &pMesh->vertices, q.c[k] )->position; }
        for ( u32 j = 1u; j < cV; ++j ) {
            for ( u32 i = 1u; i < cU; ++i ) {
                const f64 s = static_cast<f64>( i ) / cU, t = static_cast<f64>( j ) / cV;
                const vec3d_t bottom = math::Vec3d_Add( math::Vec3d_Scale( p[0], 1.0 - s ), math::Vec3d_Scale( p[1], s ) );
                const vec3d_t top = math::Vec3d_Add( math::Vec3d_Scale( p[3], 1.0 - s ), math::Vec3d_Scale( p[2], s ) );
                if ( !Vector_PushBack( &newPositions, math::Vec3d_Add( math::Vec3d_Scale( bottom, 1.0 - t ), math::Vec3d_Scale( top, t ) ) ) ) {
                    return Failed( geometry_status_t::ALLOCATION_FAILED );
                }
            }
        }
    }

    // Rings: every quad's cells, then every touched neighbour.
    vector_t<mesh_boundary_corner_t> corners{};
    vector_t<u32> sizes{};
    vector_t<mesh_quad_slice_face_t> meta{};
    vector_t<geometry_mesh_face_handle_t> remove{};
    if ( !Vector_Init( &corners, pA ) || !Vector_Init( &sizes, pA ) || !Vector_Init( &meta, pA ) || !Vector_Init( &remove, pA ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize qi = 0u; qi < quads.nCount; ++qi ) {
        const quad_t &q = quads.pData[qi];
        auto point = [&]( u32 i, u32 j ) noexcept {
            mesh_boundary_corner_t c{};
            if ( ( i == 0u || i == cU ) && ( j == 0u || j == cV ) ) {
                c.hVertex = q.c[j == 0u ? ( i == 0u ? 0u : 1u ) : ( i == 0u ? 3u : 2u )];
                return c;
            }
            if ( j == 0u ) { return along( q.h[0], i ); }      // c0 -> c1
            if ( i == cU ) { return along( q.h[1], j ); }      // c1 -> c2
            if ( j == cV ) { return along( q.h[2], cU - i ); } // c2 -> c3
            if ( i == 0u ) { return along( q.h[3], cV - j ); } // c3 -> c0
            c.iNew = q.iFirstInterior + ( j - 1u ) * ( cU - 1u ) + ( i - 1u );
            return c;
        };
        if ( !Vector_PushBack( &remove, q.hFace ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
        for ( u32 j = 0u; j < cV; ++j ) {
            for ( u32 i = 0u; i < cU; ++i ) {
                const mesh_boundary_corner_t cell[4] = { point( i, j ), point( i + 1u, j ), point( i + 1u, j + 1u ), point( i, j + 1u ) };
                for ( const mesh_boundary_corner_t &c : cell ) {
                    if ( !Vector_PushBack( &corners, c ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
                }
                mesh_quad_slice_face_t m{};
                m.hSource = q.hFace;
                m.role = mesh_quad_slice_role_t::CELL;
                m.bKeepsIdentity = i == 0u && j == 0u;
                if ( !Vector_PushBack( &sizes, 4u ) || !Vector_PushBack( &meta, m ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
            }
        }
    }
    u32 cNeighbors = 0u;
    geometry_status_t st = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pMesh->faces, [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
        if ( selected.pData[hF.nSlot] != 0u ) { return true; }
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
        bool bTouched = false;
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( u32 k = 0u; k < pL->cHalfEdges && !bTouched; ++k ) {
            const mesh_half_edge_record_t *pH = He( pMesh, h );
            bTouched = pieces.pData[pH->hEdge.nSlot] > 1u;
            h = pH->hNext;
        }
        if ( !bTouched ) { return true; }
        const usize iStart = corners.nCount;
        h = pL->hFirstHalfEdge;
        for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
            const mesh_half_edge_record_t *pH = He( pMesh, h );
            mesh_boundary_corner_t c{};
            c.hVertex = pH->hOrigin;
            if ( !Vector_PushBack( &corners, c ) ) {
                st = geometry_status_t::ALLOCATION_FAILED;
                return false;
            }
            const u32 n = pieces.pData[pH->hEdge.nSlot];
            for ( u32 m = 1u; m < n; ++m ) {
                if ( !Vector_PushBack( &corners, along( h, m ) ) ) {
                    st = geometry_status_t::ALLOCATION_FAILED;
                    return false;
                }
            }
            h = pH->hNext;
        }
        const usize cCorners = corners.nCount - iStart;
        if ( cCorners > kMeshSourceCornersPerFaceMax ) {
            st = geometry_status_t::LIMIT_EXCEEDED;
            return false;
        }
        mesh_quad_slice_face_t m{};
        m.hSource = hF;
        m.role = mesh_quad_slice_role_t::NEIGHBOR;
        m.bKeepsIdentity = true;
        if ( !Vector_PushBack( &sizes, static_cast<u32>( cCorners ) ) || !Vector_PushBack( &meta, m ) || !Vector_PushBack( &remove, hF ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
            return false;
        }
        ++cNeighbors;
        return true;
    } );
    if ( st != geometry_status_t::OK ) { return Failed( st ); }

    vector_t<geometry_mesh_face_handle_t> created{};
    if ( !Vector_Init( &created, pA ) || !Vector_Reserve( &created, sizes.nCount ) ||
         ( pFacesOut != nullptr && !Vector_Reserve( pFacesOut, pFacesOut->nCount + sizes.nCount ) ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    const mesh_boundary_replace_result_t rr = MeshBoundary_ReplaceFaces(
        pMesh, span_t<const geometry_mesh_face_handle_t>{ remove.pData, remove.nCount },
        span_t<const mesh_boundary_corner_t>{ corners.pData, corners.nCount }, span_t<const u32>{ sizes.pData, sizes.nCount },
        span_t<const vec3d_t>{ newPositions.pData, newPositions.nCount }, &created );
    if ( rr.status != geometry_status_t::OK ) { return Failed( rr.status ); }
    if ( pFacesOut != nullptr ) {
        for ( usize i = 0u; i < created.nCount; ++i ) {
            mesh_quad_slice_face_t m = meta.pData[i];
            m.hFace = created.pData[i];
            (void)Vector_PushBack( pFacesOut, m );
        }
    }
    mesh_quad_slice_result_t r{};
    r.cQuadsSliced = static_cast<u32>( quads.nCount );
    r.cNeighborsRebuilt = cNeighbors;
    r.cVerticesCreated = rr.cVerticesCreated;
    r.status = geometry_status_t::OK;
    return r;
}

} // namespace cypher::editor::geometry
