//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshVertexMove.cpp
//  Purpose: Implements the validated multi-vertex move.
//  Details: New positions are staged in a slot-indexed table so each
//           affected face can be checked with its post-move corners without
//           touching the mesh; faces are visited once each (marked by slot).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshVertexMove.h"
#include "CypherGeometry_MeshPlanar.h"
#include "CypherGeometry_MeshRecordAccess.h"
#include "CypherGeometry_MeshSource.h"

#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace mesh_detail;

geometry_status_t MeshVertices_TryMove(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_vertex_handle_t> vertices,
    span_t<const math::vec3d_t> positions,
    u32 *pFacesUpdatedOut ) noexcept
{
    if ( pFacesUpdatedOut != nullptr ) { *pFacesUpdatedOut = 0u; }
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( vertices.nCount == 0u || vertices.nCount != positions.nCount || vertices.pData == nullptr || positions.pData == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const allocator_t *pA = pMesh->pAllocator;
    vector_t<u8> moved{}, faceSeen{};
    vector_t<math::vec3d_t> staged{}, ring{};
    vector_t<math::vec2d_t> flat{};
    vector_t<geometry_mesh_face_handle_t> faces{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &moved );
        Vector_Shutdown( &faceSeen );
        Vector_Shutdown( &staged );
        Vector_Shutdown( &ring );
        Vector_Shutdown( &flat );
        Vector_Shutdown( &faces );
    };
    auto fail = [&]( geometry_status_t st ) noexcept {
        cleanup();
        return st;
    };
    if ( !Vector_Init( &moved, pA ) || !Vector_Init( &faceSeen, pA ) || !Vector_Init( &staged, pA ) || !Vector_Init( &ring, pA ) ||
         !Vector_Init( &flat, pA ) || !Vector_Init( &faces, pA ) || !Vector_Resize( &moved, pMesh->vertices.cSlots ) ||
         !Vector_Resize( &staged, pMesh->vertices.cSlots ) || !Vector_Resize( &faceSeen, pMesh->faces.cSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < moved.nCount; ++i ) { moved.pData[i] = 0u; }
    for ( usize i = 0u; i < faceSeen.nCount; ++i ) { faceSeen.pData[i] = 0u; }
    for ( usize i = 0u; i < vertices.nCount; ++i ) {
        const geometry_mesh_vertex_handle_t h = vertices.pData[i];
        if ( !GenerationPool_Contains( &pMesh->vertices, h ) ) { return fail( geometry_status_t::INVALID_HANDLE ); }
        if ( moved.pData[h.nSlot] != 0u ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
        if ( !math::Vec3d_IsFinite( positions.pData[i] ) ) { return fail( geometry_status_t::NUMERIC_FAILURE ); }
        moved.pData[h.nSlot] = 1u;
        staged.pData[h.nSlot] = positions.pData[i];
    }

    // Faces touching a moved vertex, each checked once with staged corners.
    bool bOk = true;
    geometry_status_t verdict = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
            const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
            if ( pL == nullptr ) { return true; }
            Vector_Clear( &ring );
            bool bTouched = false;
            geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
            for ( u32 k = 0u; k < pL->cHalfEdges && bOk; ++k ) {
                const mesh_half_edge_record_t *pH = He( pMesh, h );
                const bool bMoved = moved.pData[pH->hOrigin.nSlot] != 0u;
                bTouched = bTouched || bMoved;
                bOk = Vector_PushBack( &ring, bMoved ? staged.pData[pH->hOrigin.nSlot]
                                                     : GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position );
                h = pH->hNext;
            }
            if ( !bOk || !bTouched ) { return bOk; }
            const u32 n = static_cast<u32>( ring.nCount );
            const math::vec3d_t newell = NewellOf( ring.pData, n );
            if ( !FaceAreaDescribable( newell ) ) {
                verdict = geometry_status_t::DEGENERATE;
                return false;
            }
            const u32 axis = DominantAxis( newell );
            bOk = Vector_Resize( &flat, n );
            if ( !bOk ) { return false; }
            for ( u32 i = 0u; i < n; ++i ) { flat.pData[i] = Project( ring.pData[i], axis ); }
            if ( !IsSimple( flat.pData, n ) ) {
                verdict = geometry_status_t::SELF_INTERSECTING;
                return false;
            }
            faceSeen.pData[hF.nSlot] = 1u;
            bOk = Vector_PushBack( &faces, hF );
            return bOk;
        } );
    if ( !bOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    if ( verdict != geometry_status_t::OK ) { return fail( verdict ); }
    u32 cMax = 0u;
    for ( usize i = 0u; i < faces.nCount; ++i ) {
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, GenerationPool_Get( &pMesh->faces, faces.pData[i] )->hOuterLoop );
        cMax = std::max( cMax, pL->cHalfEdges );
    }
    if ( !Vector_Resize( &ring, cMax ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }

    // ---- Mutation ----
    for ( usize i = 0u; i < vertices.nCount; ++i ) {
        GenerationPool_Get( &pMesh->vertices, vertices.pData[i] )->position = positions.pData[i];
    }
    for ( usize i = 0u; i < faces.nCount; ++i ) { RecomputeNormal( pMesh, faces.pData[i], ring.pData, cMax ); }
    if ( pFacesUpdatedOut != nullptr ) { *pFacesUpdatedOut = static_cast<u32>( faces.nCount ); }
    cleanup();
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
