//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceBridge.cpp
//  Purpose: Implements the identity-addressed bridge wrappers.
//  Details: Parent entries are reserved for the largest possible quad count
//           (corners x segments) before the bridge runs, so nothing can
//           fail between the bridge and the attribute resolve.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceBridge.h"

#include "CypherGeometry_MeshEditBracket.h"
#include "CypherGeometry_MeshSourceTopology.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// bridge: mesh_bridge_result_t( vector_t<mesh_bridge_face_t> *pFaces )
template <typename bridge_t>
geometry_status_t BracketBridge( mesh_source_t *pSource, usize cBound, bridge_t &&bridge, u32 *pCreatedOut,
                                 mesh_edit_report_t *pReportOut ) noexcept
{
    if ( pCreatedOut ) { *pCreatedOut = 0u; }
    vector_t<mesh_bridge_face_t> faces{};
    if ( !Vector_Init( &faces, pSource->attributes.faces.pAllocator ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    u32 cCreated = 0u;
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            if ( !Vector_Reserve( pParents, pParents->nCount + cBound ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            const mesh_bridge_result_t r = bridge( &faces );
            if ( r.status != geometry_status_t::OK ) { return r.status; }
            cCreated = r.cFacesCreated;
            for ( usize i = 0u; i < faces.nCount; ++i ) {
                (void)Vector_PushBack( pParents, mesh_edit_face_parent_t{ faces.pData[i].hFace, faces.pData[i].hSource, false } );
            }
            return geometry_status_t::OK;
        },
        pReportOut );
    if ( st == geometry_status_t::OK && pCreatedOut ) { *pCreatedOut = cCreated; }
    return st;
}

geometry_status_t ResolvePath( mesh_source_t *pSource, span_t<const geometry_source_id_t> path, vector_t<geometry_mesh_edge_handle_t> *pOut ) noexcept
{
    if ( path.pData == nullptr || path.nCount < 2u ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Resize( pOut, path.nCount - 1u ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i + 1u < path.nCount; ++i ) {
        if ( !MeshSourceEdit_TryFindEdge( pSource, path.pData[i], path.pData[i + 1u], &pOut->pData[i] ) ) {
            return geometry_status_t::INVALID_HANDLE;
        }
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t MeshSourceEdit_TryBridgeFaces(
    mesh_source_t *pSource,
    geometry_source_id_t faceA,
    geometry_source_id_t faceB,
    u32 cSegments,
    u32 *pCreatedOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_face_handle_t a{}, b{};
    if ( !MeshSource_TryFindFace( pSource, faceA, &a ) || !MeshSource_TryFindFace( pSource, faceB, &b ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    const usize cBound = static_cast<usize>( kMeshSourceCornersPerFaceMax ) * kMeshBridgeSegmentsMax;
    return BracketBridge(
        pSource, cBound,
        [&]( vector_t<mesh_bridge_face_t> *pFaces ) noexcept { return MeshBridge_Faces( &pSource->mesh, a, b, cSegments, pFaces ); },
        pCreatedOut, pReportOut );
}

geometry_status_t MeshSourceEdit_TryBridgeEdgeChains(
    mesh_source_t *pSource,
    span_t<const geometry_source_id_t> pathA,
    span_t<const geometry_source_id_t> pathB,
    u32 cSegments,
    u32 *pCreatedOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const allocator_t *pA = pSource->attributes.faces.pAllocator;
    vector_t<geometry_mesh_edge_handle_t> chainA{}, chainB{};
    if ( !Vector_Init( &chainA, pA ) || !Vector_Init( &chainB, pA ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    geometry_status_t s = ResolvePath( pSource, pathA, &chainA );
    if ( s == geometry_status_t::OK ) { s = ResolvePath( pSource, pathB, &chainB ); }
    if ( s != geometry_status_t::OK ) { return s; }
    const usize cBound = chainA.nCount * static_cast<usize>( cSegments <= kMeshBridgeSegmentsMax ? cSegments : 0u );
    return BracketBridge(
        pSource, cBound,
        [&]( vector_t<mesh_bridge_face_t> *pFaces ) noexcept {
            return MeshBridge_EdgeChains( &pSource->mesh, span_t<const geometry_mesh_edge_handle_t>{ chainA.pData, chainA.nCount },
                                          span_t<const geometry_mesh_edge_handle_t>{ chainB.pData, chainB.nCount }, cSegments, pFaces );
        },
        pCreatedOut, pReportOut );
}

} // namespace cypher::editor::geometry
