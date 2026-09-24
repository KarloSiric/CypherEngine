//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSetDelta.h
//  Purpose: Declares ordered mesh-set undo/redo records.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SET_DELTA_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SET_DELTA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_DocumentMeshSet.h"

namespace cypher::editor::geometry
{

struct geometry_mesh_description_list_t {
    // Individually allocated because mesh_source_description_t owns
    // non-movable vector_t members.
    common::vector_t<mesh_source_description_t *> meshes{};
};

// A complete ordered before/after mesh-set record. Full sets are intentional:
// they make order and N-to-M membership part of the checked precondition and
// keep inversion independent of operation-specific Join/Separate metadata.
struct geometry_mesh_set_delta_t {
    geometry_mesh_description_list_t before{};
    geometry_mesh_description_list_t after{};
    bool bHasPayload{ false };
};

CYPHER_NODISCARD geometry_status_t GeometryMeshSetDelta_Init(
    geometry_mesh_set_delta_t *pDelta,
    const common::allocator_t *pAllocator ) noexcept;

void GeometryMeshSetDelta_Shutdown(
    geometry_mesh_set_delta_t *pDelta ) noexcept;

CYPHER_NODISCARD bool GeometryMeshSetDelta_IsInitialized(
    const geometry_mesh_set_delta_t *pDelta ) noexcept;

// Deep-copies two complete ordered description sets. The descriptions are
// validated as buildable meshes with globally unique source IDs inside each
// set. Failure preserves the previous delta payload exactly.
CYPHER_NODISCARD geometry_status_t GeometryMeshSetDelta_TryAssign(
    geometry_mesh_set_delta_t *pDelta,
    common::span_t<const mesh_source_description_t *const> before,
    common::span_t<const mesh_source_description_t *const> after ) noexcept;

// Swaps before and after ownership in place. Never allocates.
void GeometryMeshSetDelta_Invert(
    geometry_mesh_set_delta_t *pDelta ) noexcept;

// Requires the document's complete mesh set and order to equal `before`.
// A mismatch returns STALE_REVISION. Success atomically publishes `after` and
// advances document revision exactly once.
CYPHER_NODISCARD geometry_status_t GeometryMeshSetDelta_TryApply(
    const geometry_mesh_set_delta_t *pDelta,
    geometry_document_t *pDocument,
    geometry_revision_t *pNewRevisionOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SET_DELTA_H
