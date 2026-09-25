//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshObjectCommands.h
//  Purpose: Declares document commands for exact mesh Join and Separate.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_OBJECT_COMMANDS_H
#define CYPHER_EDITOR_GEOMETRY_MESH_OBJECT_COMMANDS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSourceComposition.h"
#include "CypherGeometry_MeshSetDelta.h"

namespace cypher::editor::geometry
{

struct mesh_object_command_report_t {
    geometry_source_id_t retainedRootId{};
    geometry_source_id_t firstNewRootId{};
    common::u32 cInputObjects{ 0u };
    common::u32 cOutputObjects{ 0u };
    common::u32 cNewRoots{ 0u };
    common::u32 cVertices{ 0u };
    common::u32 cFaces{ 0u };
    common::u32 cShells{ 0u };
};

// Joins two or more document mesh objects without welding or Boolean union.
// retainedRootId must name exactly one input and explicitly chooses the root
// that survives. The result occupies that root's previous document position;
// donor objects disappear and all unrelated objects keep relative order and
// storage addresses. Vertex/face identities and all authored attributes are
// preserved exactly. Selection provenance is therefore deterministic: change
// the meshId of donor selections to retainedRootId; component IDs do not
// change.
CYPHER_NODISCARD geometry_status_t GeometryMeshObjectCommand_TryJoinExact(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> inputRootIds,
    geometry_source_id_t retainedRootId,
    geometry_mesh_set_delta_t *pDeltaOut,
    mesh_object_command_report_t *pReportOut,
    geometry_revision_t *pNewRevisionOut ) noexcept;

// Separates every topological shell of one mesh into its own document object.
// retainedShellFaceId explicitly selects the shell that keeps sourceRootId.
// That object remains at the source's document position. Other shell objects
// follow it in ascending lowest-face-ID order and receive staged monotonic
// root IDs in that order. Existing vertex/face identities and every corner,
// face, and edge attribute are preserved exactly; only new object roots are
// allocated. A one-shell source returns DEGENERATE without changing anything.
//
// Selection provenance: every component ID remains unchanged. A selection's
// destination root is the output description containing that ID; the ordered
// after-set in pDeltaOut is the authoritative mapping.
CYPHER_NODISCARD geometry_status_t
GeometryMeshObjectCommand_TrySeparateByShell(
    geometry_document_t *pDocument,
    geometry_source_id_t sourceRootId,
    geometry_source_id_t retainedShellFaceId,
    geometry_mesh_set_delta_t *pDeltaOut,
    mesh_object_command_report_t *pReportOut,
    geometry_revision_t *pNewRevisionOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_OBJECT_COMMANDS_H
