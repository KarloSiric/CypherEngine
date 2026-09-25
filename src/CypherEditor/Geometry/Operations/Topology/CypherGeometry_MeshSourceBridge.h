//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceBridge.h
//  Purpose: Identity-addressed wrappers for bridging two faces or two open
//           edge chains (MeshBridge.h).
//  Details: The bridged faces are removed (their IDs retire at commit). The
//           new quads are new faces without an ID; each takes the
//           attributes of its reported source face (the wall the tube
//           continues, or the face along the bridged border), so a texture
//           carries on across the bridge instead of starting blank.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_BRIDGE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_BRIDGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshAttributeTransfer.h"
#include "CypherGeometry_MeshBridge.h"

namespace cypher::editor::geometry
{

// Bridges faces faceA and faceB with a tube (see MeshBridge_Faces). An
// unknown face ID is INVALID_HANDLE. *pCreatedOut (optional) receives the
// number of quads made.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryBridgeFaces(
    mesh_source_t *pSource,
    geometry_source_id_t faceA,
    geometry_source_id_t faceB,
    common::u32 cSegments,
    common::u32 *pCreatedOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// Bridges two open border paths, each given as the vertex IDs along it
// (v0, v1, ..., vm: m edges; either direction). See MeshBridge_EdgeChains.
// Two consecutive IDs that name no edge are INVALID_HANDLE.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryBridgeEdgeChains(
    mesh_source_t *pSource,
    common::span_t<const geometry_source_id_t> pathA,
    common::span_t<const geometry_source_id_t> pathB,
    common::u32 cSegments,
    common::u32 *pCreatedOut,
    mesh_edit_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_BRIDGE_H
