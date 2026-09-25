//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceMerge.h
//  Purpose: Identity-addressed wrappers for vertex merging (MeshMerge.h):
//           merge groups, sew open edges, merge by distance.
//  Details: A rebuilt face is the same face with corners replaced, so it
//           keeps its source ID and attributes. Each group's survivor keeps
//           its vertex ID; the merged-away vertices' IDs retire at commit.
//           Corners at a survivor that moved (CENTER) get UVs refit from
//           their face's layout, like any moved corner.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_MERGE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_MERGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshAttributeTransfer.h"
#include "CypherGeometry_MeshMerge.h"

namespace cypher::editor::geometry
{

// Merges each group of vertex IDs into its first (see MeshMerge_Vertices).
// groupVertexIds holds the groups back to back; groupSizes their sizes.
// An unknown ID is INVALID_HANDLE. *pMergedOut (optional) receives the
// number of vertices merged away.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryMergeVertices(
    mesh_source_t *pSource,
    common::span_t<const geometry_source_id_t> groupVertexIds,
    common::span_t<const common::u32> groupSizes,
    mesh_merge_target_t target,
    common::u32 *pMergedOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// Sews open edges in pairs; edges are given as vertex-ID pairs, two edges
// per pair (a0, b0, a1, b1, ...: edge a0-b0 sews to edge a1-b1). See
// MeshMerge_SewEdges. An ID pair that names no edge is INVALID_HANDLE.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySewEdges(
    mesh_source_t *pSource,
    common::span_t<const geometry_source_id_t> edgeVertexIds,
    mesh_merge_target_t target,
    common::u32 *pMergedOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// Merges vertices chained within `tolerance` (see MeshMerge_ByDistance).
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryMergeByDistance(
    mesh_source_t *pSource,
    common::f64 tolerance,
    common::u32 *pMergedOut,
    mesh_edit_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_MERGE_H
