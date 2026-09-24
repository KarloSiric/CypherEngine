//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshDelta.cpp
//  Purpose: Implements mesh delta lifecycle, inversion, and checked
//           application.
//  Details: Application builds the target mesh from its description before
//           touching the document, then hands it to the DocumentMeshes
//           atomic step. Every failure therefore happens before any
//           document state (including the revision) changes.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshDelta.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// True when the document's mesh `meshId` describes exactly as *pExpected.
geometry_status_t MatchesDescription(
    const geometry_document_t *pDocument,
    geometry_source_id_t meshId,
    const mesh_source_description_t *pExpected,
    bool *pMatches ) noexcept
{
    *pMatches = false;
    const mesh_source_t *pMesh = GeometryDocument_FindMesh( pDocument, meshId );
    if ( pMesh == nullptr ) { return geometry_status_t::OK; }
    mesh_source_description_t current{};
    geometry_status_t st = MeshSourceDescription_Init( &current, pDocument->pAllocator, meshId );
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( pMesh, &current ); }
    if ( st == geometry_status_t::OK ) { *pMatches = MeshSourceDescription_Equal( &current, pExpected ); }
    MeshSourceDescription_Shutdown( &current );
    return st;
}

} // namespace

geometry_status_t GeometryMeshDelta_Init( geometry_mesh_delta_t *pDelta, const allocator_t *pAllocator ) noexcept
{
    if ( pDelta == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( GeometryMeshDelta_IsInitialized( pDelta ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    geometry_status_t st = MeshSourceDescription_Init( &pDelta->before, pAllocator, GEOMETRY_SOURCE_ID_INVALID );
    if ( st == geometry_status_t::OK ) {
        st = MeshSourceDescription_Init( &pDelta->after, pAllocator, GEOMETRY_SOURCE_ID_INVALID );
    }
    if ( st != geometry_status_t::OK ) {
        GeometryMeshDelta_Shutdown( pDelta );
        return st;
    }
    pDelta->kind = geometry_mesh_delta_kind_t::INVALID;
    pDelta->meshId = GEOMETRY_SOURCE_ID_INVALID;
    return geometry_status_t::OK;
}

void GeometryMeshDelta_Shutdown( geometry_mesh_delta_t *pDelta ) noexcept
{
    if ( pDelta == nullptr ) { return; }
    MeshSourceDescription_Shutdown( &pDelta->before );
    MeshSourceDescription_Shutdown( &pDelta->after );
    pDelta->kind = geometry_mesh_delta_kind_t::INVALID;
    pDelta->meshId = GEOMETRY_SOURCE_ID_INVALID;
}

bool GeometryMeshDelta_IsInitialized( const geometry_mesh_delta_t *pDelta ) noexcept
{
    return pDelta != nullptr && MeshSourceDescription_IsInitialized( &pDelta->before ) &&
           MeshSourceDescription_IsInitialized( &pDelta->after );
}

void GeometryMeshDelta_Invert( geometry_mesh_delta_t *pDelta ) noexcept
{
    if ( !GeometryMeshDelta_IsInitialized( pDelta ) ) { return; }
    // Three-way move through a temporary: pure pointer transfers.
    mesh_source_description_t tmp{};
    MeshSourceDescription_Move( &tmp, &pDelta->before );
    MeshSourceDescription_Move( &pDelta->before, &pDelta->after );
    MeshSourceDescription_Move( &pDelta->after, &tmp );
    switch ( pDelta->kind ) {
    case geometry_mesh_delta_kind_t::MESH_ADDED:
        pDelta->kind = geometry_mesh_delta_kind_t::MESH_REMOVED;
        break;
    case geometry_mesh_delta_kind_t::MESH_REMOVED:
        pDelta->kind = geometry_mesh_delta_kind_t::MESH_ADDED;
        break;
    default:
        break;
    }
}

geometry_status_t GeometryMeshDelta_TryApply(
    const geometry_mesh_delta_t *pDelta,
    geometry_document_t *pDocument,
    geometry_revision_t *pNewRevisionOut ) noexcept
{
    if ( !GeometryMeshDelta_IsInitialized( pDelta ) || !GeometrySourceId_IsValid( pDelta->meshId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const geometry_mesh_delta_kind_t kind = pDelta->kind;
    if ( kind != geometry_mesh_delta_kind_t::MESH_ADDED && kind != geometry_mesh_delta_kind_t::MESH_REMOVED &&
         kind != geometry_mesh_delta_kind_t::MESH_REPLACED ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Precondition.
    if ( kind == geometry_mesh_delta_kind_t::MESH_ADDED ) {
        if ( GeometryDocument_FindMesh( pDocument, pDelta->meshId ) != nullptr ) {
            return geometry_status_t::STALE_REVISION;
        }
    } else {
        bool bMatches = false;
        const geometry_status_t st = MatchesDescription( pDocument, pDelta->meshId, &pDelta->before, &bMatches );
        if ( st != geometry_status_t::OK ) { return st; }
        if ( !bMatches ) { return geometry_status_t::STALE_REVISION; }
    }

    geometry_status_t st = geometry_status_t::OK;
    if ( kind == geometry_mesh_delta_kind_t::MESH_REMOVED ) {
        st = GeometryDocument_TryRemoveMesh( pDocument, pDelta->meshId );
    } else {
        if ( pDelta->after.sourceId.value != pDelta->meshId.value ) { return geometry_status_t::INVALID_ARGUMENT; }
        mesh_source_t target{};
        st = MeshSource_TryBuild( &pDelta->after, pDocument->pAllocator, &target );
        if ( st == geometry_status_t::OK ) {
            st = kind == geometry_mesh_delta_kind_t::MESH_ADDED ? GeometryDocument_TryAddMesh( pDocument, &target )
                                                               : GeometryDocument_TryReplaceMesh( pDocument, &target );
        }
        MeshSource_Shutdown( &target );
    }
    if ( st != geometry_status_t::OK ) { return st; }
    pDocument->revision += 1u;
    if ( pNewRevisionOut ) { *pNewRevisionOut = pDocument->revision; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
