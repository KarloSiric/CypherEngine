//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceMerge.cpp
//  Purpose: Implements the identity-addressed merge wrappers.
//  Details: One bracket helper runs any of the three merges: it reserves a
//           parent entry per face (a merge rebuilds at most every face)
//           before the merge runs, then records each rebuilt face as its
//           old face with identity, so nothing can fail between the merge
//           and the attribute resolve.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceMerge.h"

#include "CypherGeometry_MeshEditBracket.h"
#include "CypherGeometry_MeshSourceTopology.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// merge: mesh_merge_result_t( vector_t<mesh_merge_face_t> *pFaces )
template <typename merge_t>
geometry_status_t BracketMerge( mesh_source_t *pSource, merge_t &&merge, u32 *pMergedOut, mesh_edit_report_t *pReportOut ) noexcept
{
    if ( pMergedOut ) { *pMergedOut = 0u; }
    vector_t<mesh_merge_face_t> faces{};
    if ( !Vector_Init( &faces, pSource->attributes.faces.pAllocator ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    const usize cBound = EditableMesh_FaceCount( &pSource->mesh );
    u32 cMerged = 0u;
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            if ( !Vector_Reserve( pParents, pParents->nCount + cBound ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            const mesh_merge_result_t r = merge( &faces );
            if ( r.status != geometry_status_t::OK ) { return r.status; }
            cMerged = r.cVerticesMerged;
            for ( usize i = 0u; i < faces.nCount; ++i ) {
                (void)Vector_PushBack( pParents, mesh_edit_face_parent_t{ faces.pData[i].hFace, faces.pData[i].hSource, true } );
            }
            return geometry_status_t::OK;
        },
        pReportOut );
    if ( st == geometry_status_t::OK && pMergedOut ) { *pMergedOut = cMerged; }
    return st;
}

} // namespace

geometry_status_t MeshSourceEdit_TryMergeVertices(
    mesh_source_t *pSource,
    span_t<const geometry_source_id_t> groupVertexIds,
    span_t<const u32> groupSizes,
    mesh_merge_target_t target,
    u32 *pMergedOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( groupVertexIds.pData == nullptr || groupVertexIds.nCount == 0u ) { return geometry_status_t::INVALID_ARGUMENT; }
    vector_t<geometry_mesh_vertex_handle_t> handles{};
    if ( !Vector_Init( &handles, pSource->attributes.faces.pAllocator ) || !Vector_Resize( &handles, groupVertexIds.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < groupVertexIds.nCount; ++i ) {
        if ( !MeshSource_TryFindVertex( pSource, groupVertexIds.pData[i], &handles.pData[i] ) ) {
            return geometry_status_t::INVALID_HANDLE;
        }
    }
    return BracketMerge(
        pSource,
        [&]( vector_t<mesh_merge_face_t> *pFaces ) noexcept {
            return MeshMerge_Vertices( &pSource->mesh, span_t<const geometry_mesh_vertex_handle_t>{ handles.pData, handles.nCount },
                                       groupSizes, target, pFaces );
        },
        pMergedOut, pReportOut );
}

geometry_status_t MeshSourceEdit_TrySewEdges(
    mesh_source_t *pSource,
    span_t<const geometry_source_id_t> edgeVertexIds,
    mesh_merge_target_t target,
    u32 *pMergedOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( edgeVertexIds.pData == nullptr || edgeVertexIds.nCount == 0u || ( edgeVertexIds.nCount % 4u ) != 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const usize cEdges = edgeVertexIds.nCount / 2u;
    vector_t<geometry_mesh_edge_handle_t> handles{};
    if ( !Vector_Init( &handles, pSource->attributes.faces.pAllocator ) || !Vector_Resize( &handles, cEdges ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cEdges; ++i ) {
        if ( !MeshSourceEdit_TryFindEdge( pSource, edgeVertexIds.pData[2u * i], edgeVertexIds.pData[2u * i + 1u], &handles.pData[i] ) ) {
            return geometry_status_t::INVALID_HANDLE;
        }
    }
    return BracketMerge(
        pSource,
        [&]( vector_t<mesh_merge_face_t> *pFaces ) noexcept {
            return MeshMerge_SewEdges( &pSource->mesh, span_t<const geometry_mesh_edge_handle_t>{ handles.pData, handles.nCount },
                                       target, pFaces );
        },
        pMergedOut, pReportOut );
}

geometry_status_t MeshSourceEdit_TryMergeByDistance(
    mesh_source_t *pSource,
    f64 tolerance,
    u32 *pMergedOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    return BracketMerge(
        pSource,
        [&]( vector_t<mesh_merge_face_t> *pFaces ) noexcept { return MeshMerge_ByDistance( &pSource->mesh, tolerance, pFaces ); },
        pMergedOut, pReportOut );
}

} // namespace cypher::editor::geometry
