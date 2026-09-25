//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceSlice.cpp
//  Purpose: Implements the identity-addressed plane cut.
//  Details: The parent list is reserved before the cut runs, so once the
//           mesh has changed nothing can fail before the attribute resolve.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceSlice.h"

#include "CypherGeometry_MeshEditBracket.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

geometry_status_t MeshSourceEdit_TrySliceByPlane(
    mesh_source_t *pSource,
    const mesh_slice_params_t &params,
    vector_t<mesh_slice_face_t> *pFacesOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pFacesOut != nullptr && pFacesOut->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    // The cut reserves and fills the face list itself before it changes the
    // mesh; the caller's list is used directly when there is one.
    vector_t<mesh_slice_face_t> local{};
    if ( pFacesOut == nullptr && !Vector_Init( &local, pSource->attributes.faces.pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    vector_t<mesh_slice_face_t> *pFaces = pFacesOut != nullptr ? pFacesOut : &local;
    const usize cBefore = pFaces->nCount;
    // Upper bound on pieces (caps have no parent): a face with c corners
    // has at most c crossing points, so at most c / 2 + 1 pieces - bounded
    // by its corner count plus one.
    const usize cBound = GenerationPool_Count( &pSource->mesh.halfEdges ) + EditableMesh_FaceCount( &pSource->mesh );
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            if ( !Vector_Reserve( pParents, pParents->nCount + cBound ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            const mesh_slice_result_t r = MeshSlice_ByPlane( &pSource->mesh, params, pFaces );
            if ( r.status != geometry_status_t::OK ) { return r.status; }
            for ( usize i = cBefore; i < pFaces->nCount; ++i ) {
                const mesh_slice_face_t &f = pFaces->pData[i];
                if ( f.role == mesh_slice_face_role_t::PIECE ) {
                    (void)Vector_PushBack( pParents, mesh_edit_face_parent_t{ f.hFace, f.hSource, f.bLargestPiece } );
                }
            }
            return geometry_status_t::OK;
        },
        pReportOut );
    if ( st != geometry_status_t::OK ) { (void)Vector_Resize( pFaces, cBefore ); } // shrinking cannot fail
    return st;
}

} // namespace cypher::editor::geometry
