//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceBevel.cpp
//  Purpose: Implements MeshSourceEdit_TryBevelEdges on top of the attribute
//           bracket.
//  Details: Every parent entry is reserved before the bevel runs: once the
//           mesh has changed, nothing may fail, or the bracket would skip
//           attribute resolution on a mesh that was already edited.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceBevel.h"

#include "CypherGeometry_MeshEditBracket.h"
#include "CypherGeometry_MeshSourceTopology.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

geometry_status_t MeshSourceEdit_TryBevelEdges(
    mesh_source_t *pSource,
    span_t<const geometry_source_id_t> edgeVertexIds,
    const mesh_bevel_params_t &params,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( edgeVertexIds.pData == nullptr || edgeVertexIds.nCount == 0u || ( edgeVertexIds.nCount % 2u ) != 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const allocator_t *pA = pSource->attributes.faces.pAllocator;
    const usize cEdges = edgeVertexIds.nCount / 2u;
    vector_t<geometry_mesh_edge_handle_t> handles{};
    vector_t<mesh_bevel_face_t> faces{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &handles );
        Vector_Shutdown( &faces );
    };
    if ( !Vector_Init( &handles, pA ) || !Vector_Init( &faces, pA ) || !Vector_Resize( &handles, cEdges ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cEdges; ++i ) {
        if ( !MeshSourceEdit_TryFindEdge( pSource, edgeVertexIds.pData[2u * i], edgeVertexIds.pData[2u * i + 1u],
                                          &handles.pData[i] ) ) {
            cleanup();
            return geometry_status_t::INVALID_HANDLE;
        }
    }
    // Upper bound on new faces: every existing face reshaped, s strip faces
    // per edge, and at most 2 s patch faces per edge (each edge closes at
    // most two corners, each contributing s fan triangles for it).
    const usize cBound = EditableMesh_FaceCount( &pSource->mesh ) + 3u * cEdges * params.cSegments;
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            if ( params.cSegments > kMeshBevelSegmentsMax || !Vector_Reserve( pParents, pParents->nCount + cBound ) ) {
                return params.cSegments > kMeshBevelSegmentsMax ? geometry_status_t::INVALID_ARGUMENT
                                                                : geometry_status_t::ALLOCATION_FAILED;
            }
            const mesh_bevel_result_t r = MeshBevel_Edges(
                &pSource->mesh, span_t<const geometry_mesh_edge_handle_t>{ handles.pData, handles.nCount }, params, &faces );
            if ( r.status != geometry_status_t::OK ) { return r.status; }
            for ( usize i = 0u; i < faces.nCount; ++i ) {
                const mesh_bevel_face_t &f = faces.pData[i];
                // A reshaped face is the original face: it keeps its ID.
                (void)Vector_PushBack(
                    pParents, mesh_edit_face_parent_t{ f.hFace, f.hSource, f.role == mesh_bevel_face_role_t::RESHAPED } );
            }
            return geometry_status_t::OK;
        },
        pReportOut );
    cleanup();
    return st;
}

} // namespace cypher::editor::geometry
