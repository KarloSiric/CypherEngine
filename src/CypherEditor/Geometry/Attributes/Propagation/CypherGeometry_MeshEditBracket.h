//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshEditBracket.h
//  Purpose: Declares the capture -> op -> resolve bracket shared by every
//           identity-addressed mesh edit.
//  Details: A header-only template so each edit can pass its op as a
//           lambda that may also append explicit parent entries (for
//           example "the extruded cap is the original face"). Keeping the
//           bracket in one place means every edit gets the same attribute
//           and identity rules and the same failure behaviour.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_EDIT_BRACKET_H
#define CYPHER_EDITOR_GEOMETRY_MESH_EDIT_BRACKET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshAttributeTransfer.h"

namespace cypher::editor::geometry
{

// op: geometry_status_t( common::vector_t<mesh_edit_face_parent_t> *pParents )
// Runs capture, the op, and (only if the op succeeded) resolve.
template <typename op_t>
CYPHER_NODISCARD geometry_status_t MeshEdit_Bracket(
    mesh_source_t *pSource,
    op_t &&op,
    mesh_edit_report_t *pReportOut ) noexcept
{
    mesh_edit_resolve_stats_t *pStatsOut = pReportOut ? &pReportOut->stats : nullptr;
    mesh_edit_provenance_t *pProvenance = pReportOut ? pReportOut->pProvenance : nullptr;
    if ( pStatsOut ) { *pStatsOut = mesh_edit_resolve_stats_t{}; }
    MeshEditProvenance_Clear( pProvenance );
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const common::allocator_t *pAllocator = pSource->attributes.faces.pAllocator;
    mesh_edit_capture_t capture{};
    common::vector_t<mesh_edit_face_parent_t> parents{};
    geometry_status_t st = MeshEditCapture_Init( &capture, pAllocator );
    if ( st == geometry_status_t::OK && !common::Vector_Init( &parents, pAllocator ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st == geometry_status_t::OK ) { st = MeshEditCapture_TryCapture( &capture, pSource ); }
    if ( st == geometry_status_t::OK ) { st = op( &parents ); }
    if ( st == geometry_status_t::OK ) {
        st = MeshEditCapture_TryResolve(
            &capture, pSource,
            common::span_t<const mesh_edit_face_parent_t>{ parents.pData, parents.nCount }, pStatsOut, pProvenance );
    }
    common::Vector_Shutdown( &parents );
    MeshEditCapture_Shutdown( &capture );
    return st;
}

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_EDIT_BRACKET_H
