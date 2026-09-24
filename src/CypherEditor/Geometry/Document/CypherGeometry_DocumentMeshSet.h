//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentMeshSet.h
//  Purpose: Declares atomic N-to-M publication of ordered mesh sets.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_MESH_SET_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_MESH_SET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_DocumentMeshes.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

// Atomically removes the mesh roots in removeMeshIds, inserts exact deep
// copies built from replacementMeshes, and publishes the order named by
// finalMeshOrder.
//
// finalMeshOrder must contain every retained mesh root and every replacement
// root exactly once. A replacement may reuse identities belonging to removed
// meshes or restore retired identities. It may not collide with a retained
// mesh, brush, or another replacement. Fresh identities must be at or above
// the document registry high-water mark.
//
// All descriptions, aggregate limits, identities, copies, the final pointer
// vector, and a complete replacement registry are prepared before the first
// document write. Success is published by allocation-free ownership swaps.
// Failure preserves mesh order, object addresses, registry storage and state,
// and revision exactly. Success does not advance revision; the owning command
// or delta publishes one revision.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryPublishMeshSetExact(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> removeMeshIds,
    common::span_t<const mesh_source_description_t *const> replacementMeshes,
    common::span_t<const geometry_source_id_t> finalMeshOrder ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_MESH_SET_H
