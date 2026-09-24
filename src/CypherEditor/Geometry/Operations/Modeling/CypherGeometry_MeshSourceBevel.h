//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceBevel.h
//  Purpose: Identity-addressed wrapper for the multi-edge, multi-segment
//           bevel (MeshBevel_Edges) on a mesh source.
//  Details: Faces the bevel reshapes are the same faces afterwards: they
//           keep their source IDs and attributes, and the corners that moved
//           get UVs from the face's own mapping (the attribute engine fits
//           it). Strip and corner-patch faces are new faces that take the
//           attributes of the face they were built from (see
//           mesh_bevel_face_t), so a bevel of a textured box continues its
//           materials instead of producing untextured strips.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_BEVEL_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_BEVEL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshAttributeTransfer.h"
#include "CypherGeometry_MeshBevel.h"

namespace cypher::editor::geometry
{

// Bevels the edges given as vertex-ID pairs (a0, b0, a1, b1, ...). See
// MeshBevel_Edges for supported configurations and rejections; a rejected
// bevel leaves the source unchanged.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryBevelEdges(
    mesh_source_t *pSource,
    common::span_t<const geometry_source_id_t> edgeVertexIds,
    const mesh_bevel_params_t &params,
    mesh_edit_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_BEVEL_H
